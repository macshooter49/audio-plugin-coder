"""tuning.py — pitch measurement and the tfix rules of the Organics compiler (tp108).

measure_pitch()  the pitch of one note (a sample region, or a note rendered by the runtime), robust enough for short
                 staccato / pizzicato takes:
    1. MULTI-WINDOW TIME DOMAIN over the steady part after the attack — every window gets a YIN (cumulative-mean-
       normalised difference) AND an MPM (McLeod normalised square difference) period, both searched only within
       ±150 ¢ of the expected period and refined on the k-th period multiple (k-fold resolution for high notes). A
       window counts when the two agree within 5 ¢. The steady part = the windows after the attack skip (60 ms / 6
       periods on a long note, 12 ms / 2 periods on a short one) while the level stays within 35 dB of the peak,
       outliers beyond 3 MAD dropped (a staccato's sharp scoop, a bow change).
    2. HARMONIC-TEMPLATE FIT on a zero-padded Hann FFT of that steady span: the partials near n·f (n ≤ 16, within 40 dB
       of the strongest, above the noise floor) are located to a fraction of a bin and fitted with
       f_n = n·f0·√(1 + B·n²) (B = 0 on harmonic sources; B ≥ 0 fitted on struck / plucked strings). A 40 ms staccato
       gives ±0.1 ¢-class partial positions from its upper harmonics where a period tracker has one or two frames.
    3. THE ESTIMATE, by kind (pitch_kind: harmonic = bowed / blown / sung / organ; string = struck / plucked; bar =
       mallets, bells, drums):
       - harmonic: ONE continuous estimator — the fundamental partial and the template blended by their quality, the
         template gaining weight as the span shortens; a pitch that moves across the span (a staccato's scoop, a sung
         glide) leans smoothly (10 → 30 ¢ of drift) on its later half: the pitch is heard where it settles. No
         threshold for a reading to flip across between a sample and its render a semitone away.
       - string: the fundamental PARTIAL f1 over the steady span (what a tuner reads; immune to inharmonicity and to a
         unison's beating), the stiff-string template on a short note, its f0 when the fundamental is > 24 dB down.
       - bar: the fundamental partial only (a bar's modes are not a harmonic series).
    4. OCTAVE ERRORS: the searches are confined to ±150 ¢ of the expected period / partial, so an estimate can never
       jump an octave; a sample sounding an octave off its root (a 4' organ rank) still measures its cents correctly
       (the template reads its partials as the even harmonics). A harmonic note whose period and partials disagree by
       > 12 ¢ is unreliable (why "partials vs period disagree").
    Returns {hz, cents, spread, conf, frames, agree, method, ok, strong, why, partials, resid, B, drift}.

assign_tfix()    the tfix rules (moved from torgc.compute_tfix, tp106 rules kept; tp108 adds the inheritance): one value
                 per note and RR take on struck / plucked instruments (median over its velocity layers), one per take on
                 performed ones; a deliberate stretch ("tfixMode": "stretch") is fitted per articulation and only the
                 per-note deviation from it is corrected (Salamander "Natural"). A take the detector still cannot trust
                 INHERITS, in order: its note's other layers / takes in the same articulation → the same key (or the
                 nearest measured key, ≤ 3 keys) of a SUSTAINED (looped) articulation of the same instrument and player
                 session (recipe "tfixSession": {artic: session}, default one session) → its own articulation's
                 measured neighbours within ±4 keys → 0. Every region records where its value came from.
"""
from __future__ import annotations

import math
from collections import OrderedDict, defaultdict
from typing import Dict, List, Optional

import numpy as np

import analyse as an

PERFORMED_CATEGORIES = ("Strings", "Winds", "Brass", "Choir & Voice")
INHARMONIC_CATEGORIES = ("Keys", "Plucked", "Mallets & Bells", "Percussion")
SEARCH_CENTS = 150.0
LIVE_DB = {"harmonic": 20.0, "string": 35.0, "bar": 35.0}   # the steady span: within this of the peak (a blown / bowed staccato's room tail is not its pitch; a struck note decays through it)
TFIX_MAX = 95.0            # the runtime clamps tfix to ±100 ¢ (OrganicsLibrary.cpp)


# --------------------------------------------------------------------------------------------- time domain
def _acf_parts(x: np.ndarray, tau_max: int):
    """r(τ) = Σ_{j<w} x_j·x_{j+τ} and m(τ) = Σ_{j<w} x_j² + x_{j+τ}², w = len − tau_max (type-II, fixed window)."""
    n = len(x)
    w = n - tau_max
    nfft = 1 << int(math.ceil(math.log2(n + w)))
    X = np.fft.rfft(x, nfft)
    Y = np.fft.rfft(x[:w], nfft)
    r = np.fft.irfft(X * np.conj(Y), nfft)[: tau_max + 1]
    c = np.concatenate([[0.0], np.cumsum(x * x)])
    e0 = c[w]
    et = c[np.arange(tau_max + 1) + w] - c[np.arange(tau_max + 1)]
    return r, e0 + et


def _parab(y: np.ndarray, i: int):
    a, b, c = y[i - 1], y[i], y[i + 1]
    den = a - 2 * b + c
    off = 0.5 * (a - c) / den if abs(den) > 1e-12 else 0.0
    off = max(-0.5, min(0.5, off))
    return i + off, float(b - 0.25 * (a - c) * off)


def _window_period(fr: np.ndarray, T0: float, lo1: int, hi1: int, k: int, hi: int):
    """One analysis window → (period in samples | None, yin confidence, mpm clarity, octave-up flag)."""
    r, m = _acf_parts(fr, hi)
    d = np.maximum(m - 2.0 * r, 0.0)
    cm = np.cumsum(d[1:]) / np.arange(1, hi + 1)
    dn = np.ones(hi + 1)
    dn[1:] = d[1:] / np.maximum(cm, 1e-20)                          # YIN d'(τ)
    ns = 2.0 * r / np.maximum(m, 1e-20)                             # MPM n(τ)
    out = []
    for curve, sign in ((dn, 1.0), (ns, -1.0)):
        seg = sign * curve[lo1:hi1 + 1]
        i1 = int(np.argmin(seg)) + lo1
        if i1 <= lo1 or i1 >= hi1:
            out.append(None)
            continue
        lag, val = _parab(sign * curve, i1)
        val = sign * val
        if k > 1:
            c2 = k * lag
            lo2, hi2 = int(math.floor(c2 - lag / 3)), int(math.ceil(c2 + lag / 3))
            if hi2 + 1 <= hi:
                i2 = int(np.argmin(sign * curve[lo2:hi2 + 1])) + lo2
                if lo2 < i2 < hi2:
                    lag = _parab(sign * curve, i2)[0] / k
        out.append((lag, val))
    if out[0] is None or out[1] is None:
        return None, 0.0, 0.0, False
    (ly, vy), (lm, vm) = out
    conf, clar = 1.0 - vy, vm
    # octave up: periodic at HALF the period as well as at it (a sample sounding an octave over its root)
    h = 0.5 * lm
    hl, hh = max(1, int(math.floor(h - lm / 8))), int(math.ceil(h + lm / 8))
    oct_up = hh < len(ns) and float(ns[hl:hh + 1].max()) >= 0.9 * clar and clar > 0.3
    if abs(1200.0 * math.log2(ly / lm)) > 5.0:
        return None, conf, clar, oct_up
    return 0.5 * (ly + lm), conf, clar, oct_up


# --------------------------------------------------------------------------------------------- template fit
def _template(fr: np.ndarray, sr: int, f_est: float, inharmonic: bool) -> Optional[dict]:
    """Harmonic-template fit of one steady span; None when fewer than 2 usable partials."""
    n = len(fr)
    if n < 64 or f_est <= 0:
        return None
    nfft = 1 << int(math.ceil(math.log2(n * 8)))
    X = np.abs(np.fft.rfft((fr - fr.mean()) * np.hanning(n), nfft))
    lx = 20.0 * np.log10(np.maximum(X, 1e-12))
    bin_hz = sr / nfft
    fmax = min(0.45 * sr, 12000.0)
    nmax = int(min(16, fmax // f_est))
    if nmax < 1:
        return None
    i20 = max(1, int(20.0 / bin_hz))
    top = float(lx[i20:int(fmax / bin_hz)].max())
    floor = float(np.median(lx[max(i20, int(0.5 * f_est / bin_hz)):int(min(fmax, (nmax + 0.5) * f_est) / bin_hz)]))
    main_w = 2.0 * sr / n                                          # Hann main-lobe half width in Hz

    def find(fc, tol_hz):
        a, b = int((fc - tol_hz) / bin_hz), int(math.ceil((fc + tol_hz) / bin_hz))
        if a < 2 or b >= len(lx) - 2:
            return None
        i = int(np.argmax(lx[a:b + 1])) + a
        if i <= a or i >= b:
            return None
        if lx[i] < top - 40.0 or lx[i] < floor + 12.0:
            return None
        pos, val = _parab(lx, i)
        return pos * bin_hz, val

    B = 0.0
    parts = {}
    for it in range(3):
        parts = {}
        for h in range(1, nmax + 1):
            fc = h * f_est * math.sqrt(1.0 + B * h * h)
            tol = max(fc * (2 ** (35.0 / 1200.0) - 1.0), 0.6 * main_w)
            if tol > 0.45 * f_est:
                tol = 0.45 * f_est
            p = find(fc, tol)
            if p:
                parts[h] = p
        if len(parts) < 2 and not (1 in parts):
            return None
        hs = np.array(sorted(parts), float)
        fs = np.array([parts[int(h)][0] for h in hs])
        w = 10.0 ** (np.array([parts[int(h)][1] for h in hs]) / 20.0)
        w = w / w.max()
        Bc = 0.0
        if inharmonic and len(hs) >= 4:
            # (f_n / n)² = f0² + f0²·B·n²  — an exact straight line in n²: weighted least squares, B ≥ 0
            y, xx = (fs / hs) ** 2, hs * hs
            sw = np.sum(w)
            mx, my = np.sum(w * xx) / sw, np.sum(w * y) / sw
            sxx = np.sum(w * (xx - mx) ** 2)
            slope = np.sum(w * (xx - mx) * (y - my)) / sxx if sxx > 0 else 0.0
            icpt = my - slope * mx
            if slope > 0 and icpt > 0:
                Bc = float(slope / icpt)
        g = hs * np.sqrt(1.0 + Bc * hs * hs)
        f0 = float(np.sum(w * g * fs) / np.sum(w * g * g))
        rs_ = 1200.0 * np.log2(fs / (f0 * g))
        e = float(np.sqrt(np.sum(w * rs_ * rs_) / np.sum(w)))
        B = Bc
        f_est = f0
    hs = sorted(parts)
    f1 = f0 * math.sqrt(1.0 + B)
    fund_db = parts[1][1] - top if 1 in parts else -200.0
    # octave errors: odd partials missing (an octave up) / strong half-integer partials (an octave down)
    odd = [h for h in hs if h % 2 == 1]
    even = [h for h in hs if h % 2 == 0]
    oct_up = len(even) >= 2 and not odd
    half = 0.0
    tot = 0.0
    for h in range(1, min(nmax, 8) + 1):
        tot += 10.0 ** (float(lx[int(round(h * f0 / bin_hz))]) / 10.0)
        hp = (h - 0.5) * f0
        a, b = int((hp - 0.1 * f0) / bin_hz), int((hp + 0.1 * f0) / bin_hz)
        if b > a:
            half += 10.0 ** (float(lx[a:b + 1].max()) / 10.0)
    oct_dn = tot > 0 and half > 0.5 * tot
    return {"f1": f1, "f0": f0, "B": B, "resid": e, "partials": len(hs), "fundDb": fund_db,
            "snr": max(v for _, v in parts.values()) - floor, "octave": bool(oct_up or oct_dn)}


# --------------------------------------------------------------------------------------------- measure_pitch
def pitch_kind(category: str) -> str:
    """harmonic (bowed / blown / sung / organ pipes) · string (struck / plucked: stiff, inharmonic partials) · bar
    (mallets, bells, drums: the partials are not a harmonic series, only the fundamental partial is the pitch)."""
    if category in ("Mallets & Bells", "Percussion"):
        return "bar"
    if category in ("Keys", "Plucked"):
        return "string"
    return "harmonic"


def _fund_peak(fr: np.ndarray, sr: int, f_seed: float, tol_cents: float = 60.0) -> Optional[dict]:
    """The fundamental partial nearest f_seed: its frequency (zero-padded Hann FFT, parabolic on the dB peak), its level
    re the strongest partial and re the local noise floor."""
    n = len(fr)
    if n < 64 or f_seed <= 0:
        return None
    nfft = 1 << int(math.ceil(math.log2(n * 8)))
    X = np.abs(np.fft.rfft((fr - fr.mean()) * np.hanning(n), nfft))
    lx = 20.0 * np.log10(np.maximum(X, 1e-12))
    bin_hz = sr / nfft
    tol = max(f_seed * (2 ** (tol_cents / 1200.0) - 1.0), 0.6 * 2.0 * sr / n)
    tol = min(tol, 0.45 * f_seed)
    a, b = int((f_seed - tol) / bin_hz), int(math.ceil((f_seed + tol) / bin_hz))
    if a < 2 or b >= len(lx) - 2:
        return None
    i = int(np.argmax(lx[a:b + 1])) + a
    if i <= a or i >= b:
        return None
    pos, val = _parab(lx, i)
    i20 = max(1, int(20.0 / bin_hz))
    top = float(lx[i20:int(min(0.45 * sr, 12000.0) / bin_hz)].max())
    lo, hi = max(i20, int(0.5 * f_seed / bin_hz)), int(1.5 * f_seed / bin_hz)
    floor = float(np.median(lx[lo:hi])) if hi > lo + 8 else float(np.median(lx[i20:]))
    return {"hz": pos * bin_hz, "fundDb": val - top, "snr": val - floor}


def measure_pitch(mono: np.ndarray, sr: int, onset: int, end: int, f_expect: float,
                  kind: str = "harmonic", max_span_s: float = 0.8) -> Dict[str, float]:
    res = {"hz": 0.0, "cents": 0.0, "spread": 0.0, "conf": 0.0, "frames": 0, "agree": 0.0, "method": "none",
           "ok": False, "strong": False, "why": "no signal", "partials": 0, "resid": 0.0, "B": 0.0}
    if f_expect <= 0 or end - onset < int(0.02 * sr):
        res["why"] = "too short"
        return res
    seg = np.asarray(mono[onset:end], dtype=np.float64)
    env = an.envelope_db(seg, sr)
    if len(env) == 0 or env.max() < -110.0:
        return res
    pk = float(env.max())
    hop_env = int(sr * an.HOP_S)
    ipk = int(np.argmax(env))
    below = np.flatnonzero(env[ipk:] < pk - LIVE_DB.get(kind, 35.0))
    live = len(seg) if len(below) == 0 else (ipk + int(below[0])) * hop_env
    live = min(live, len(seg))
    T0 = sr / f_expect
    k = max(1, int(math.ceil(256.0 / T0)))
    lo1 = max(2, int(math.floor(T0 * 2 ** (-SEARCH_CENTS / 1200.0))) - 1)
    hi1 = int(math.ceil(T0 * 2 ** (SEARCH_CENTS / 1200.0))) + 1
    hi = int(math.ceil(k * hi1 + T0)) + 2
    long_note = live >= int(0.3 * sr)
    skip = int(max(0.06 * sr, 6 * T0)) if long_note else int(max(0.012 * sr, 2 * T0))
    win = int(max(3 * hi, (0.04 if long_note else 0.02) * sr))
    if skip + win + hi > live:                                     # very short: shrink the window to what exists
        win = max(int(1.5 * hi), live - skip - hi)
    step = max(int((0.05 if long_note else 0.01) * sr), win // 2)
    span_end = min(live, skip + int(max_span_s * sr))
    starts = list(range(skip, max(skip, span_end - win - hi) + 1, step)) if win > hi else []
    ests, confs, clars, octs, used = [], [], [], 0, []
    for s in starts[:24]:
        fr = seg[s:s + win + hi]
        if len(fr) < win + hi:
            break
        lag, conf, clar, ou = _window_period(fr, T0, lo1, hi1, k, hi)
        if ou and lag is not None:
            octs += 1
        if lag is None or conf < 0.5 or clar < 0.5:
            continue
        ests.append(sr / lag)
        confs.append(conf)
        clars.append(clar)
        used.append(s)
    td = None
    if ests:
        c = 1200.0 * np.log2(np.array(ests) / f_expect)
        med = float(np.median(c))
        mad = float(np.median(np.abs(c - med)))
        keep = np.abs(c - med) <= max(3.0 * 1.4826 * mad, 4.0)
        c, used_k = c[keep], [u for u, kk in zip(used, keep) if kk]
        confs = list(np.array(confs)[keep])
        med = float(np.median(c))
        q = np.percentile(c, [25, 75]) if len(c) > 1 else [med, med]
        td = {"cents": med, "spread": float(q[1] - q[0]), "frames": int(len(c)), "conf": float(np.median(confs)),
              "a": used_k[0], "b": used_k[-1] + win + hi}
    # THE STEADY SPAN (level within LIVE_DB of the peak, after the attack skip, ≤ max_span_s); a note too short for it
    # starts at 8 ms / 1.5 periods and runs to its end
    a0, b0 = skip, span_end
    if b0 - a0 < int(max(0.03 * sr, 6 * T0)):
        a0 = int(max(0.008 * sr, 1.5 * T0))
        b0 = live
    res.update(frames=td["frames"] if td else 0, spread=td["spread"] if td else 0.0, conf=td["conf"] if td else 0.0)
    if b0 - a0 < int(max(0.02 * sr, 4 * T0)):
        res["why"] = "too short"
        return res
    f_seed = f_expect * 2 ** ((td["cents"] if td else 0.0) / 1200.0)
    fp = _fund_peak(seg[a0:b0], sr, f_seed)
    tp = _template(seg[a0:b0], sr, f_seed, kind == "string") if kind != "bar" else None
    if tp and abs(1200.0 * math.log2(tp["f1"] / f_expect)) > SEARCH_CENTS:
        tp = None
    if tp:
        res.update(partials=tp["partials"], resid=round(tp["resid"], 2), B=tp["B"])
    long_span = b0 - a0 >= int(0.2 * sr)
    fund_ok = bool(fp and fp["fundDb"] >= -24.0 and fp["snr"] >= 20.0)
    tpl_fit = bool(tp and ((tp["partials"] >= 3 and tp["resid"] <= 3.0) or (tp["partials"] == 2 and tp["resid"] <= 1.5)))
    c_fp = 1200.0 * math.log2(fp["hz"] / f_expect) if fp else None
    drift, h2 = None, None
    if long_span and fund_ok:                                     # the fundamental over each half of the span
        mid = (a0 + b0) // 2
        h1, h2 = _fund_peak(seg[a0:mid], sr, fp["hz"]), _fund_peak(seg[mid:b0], sr, fp["hz"])
        if h1 and h2:
            drift = abs(1200.0 * math.log2(h2["hz"] / h1["hz"]))
    est, method = None, "none"
    if kind == "harmonic":
        # A PERFORMED NOTE: one continuous estimator (no method switch for a reading to flip across between the sample
        # and its render a semitone away): the fundamental partial and the harmonic template blended by their quality,
        # the template gaining weight as the span shortens; then, when the pitch moves across the span (a staccato's
        # scoop, a sung glide), leaning smoothly on the later half — the pitch is heard where it settles.
        def blend(lo, hi, seed):
            n = hi - lo
            if n < int(max(0.015 * sr, 4 * T0)):
                return None, 0.0
            f_ = _fund_peak(seg[lo:hi], sr, seed)
            t_ = _template(seg[lo:hi], sr, seed, False)
            span_s = n / sr
            wf = wt = fq = 0.0
            cf_ = ct_ = 0.0
            if f_:
                fq = min(1.0, max(0.0, (f_["fundDb"] + 30.0) / 6.0)) * min(1.0, max(0.0, (f_["snr"] - 14.0) / 6.0))
                wf = fq * min(1.0, span_s / 0.08)
                cf_ = 1200.0 * math.log2(f_["hz"] / f_expect)
            if t_ and t_["partials"] >= 2 and abs(1200.0 * math.log2(t_["f0"] / f_expect)) <= SEARCH_CENTS:
                wt = (min(1.0, max(0.0, (6.0 - t_["resid"]) / 3.0)) * (1.0 if t_["partials"] >= 3 else 0.5)
                      * (0.3 + 0.7 * max(min(1.0, max(0.0, (0.15 - span_s) / 0.1)), 1.0 - fq)))
                ct_ = 1200.0 * math.log2(t_["f0"] / f_expect)
            if wf + wt < 0.05:
                return None, 0.0
            return (wf * cf_ + wt * ct_) / (wf + wt), wf + wt
        c_all, q_all = blend(a0, b0, f_seed)
        if c_all is not None:
            est, method = c_all, "blend"
            mid = (a0 + b0) // 2
            seed2 = f_expect * 2 ** (c_all / 1200.0)
            c1, _ = blend(a0, mid, seed2)
            c2, q2 = blend(mid, b0, seed2)
            if c1 is not None and c2 is not None and q2 >= 0.3:
                drift = abs(c2 - c1)
                w = min(1.0, max(0.0, (drift - 10.0) / 20.0))
                if w > 0.0:
                    est, method = est + w * (c2 - est), "blend-settled"
            if q_all < 0.35:
                res.update(cents=float(est), method=method)
                res["why"] = f"weak partials (quality {q_all:.2f})"
                return res
        elif td and td["frames"] >= 3 and td["spread"] <= 12.0 and td["conf"] >= 0.6:
            est, method = td["cents"], "period"
    elif long_span and fund_ok:
        est, method = c_fp, "partial"
        # a performed pitch that slides across the span (a sung scoop, a lip bend) is heard where it SETTLES: lean on
        # the later half, smoothly from 10 ¢ of drift (none) to 30 ¢ (all) — no threshold for a reading to flip across
        if kind == "harmonic" and drift is not None and drift > 10.0 and h2 and h2["snr"] >= 20.0:
            w = min(1.0, (drift - 10.0) / 20.0)
            est = c_fp + w * (1200.0 * math.log2(h2["hz"] / f_expect) - c_fp)
            method = "partial-settled"
    elif tpl_fit and tp["fundDb"] >= -24.0:
        est, method = 1200.0 * math.log2(tp["f1"] / f_expect), "template"
    elif tpl_fit:                                                 # weak fundamental: the harmonics' common f0
        est, method = 1200.0 * math.log2(tp["f0"] / f_expect), "template-f0"
    elif fund_ok and not long_span and b0 - a0 >= max(int(0.03 * sr), int(10 * T0)):
        est, method = c_fp, "partial-short"
    elif td and td["frames"] >= 3 and td["spread"] <= 12.0 and td["conf"] >= 0.6:
        est, method = td["cents"], "period"
    if est is None:
        res["why"] = ("no clear fundamental or harmonic series near the root"
                      + (f" (template ±{tp['resid']:.1f} ¢ over {tp['partials']} partials)" if tp else ""))
        return res
    # a short take still settling (a staccato's sharp scoop): the SETTLED pitch — the later half of the span, when it
    # alone still measures — is the note's pitch (a glide is heard at its end)
    if kind == "harmonic" and not long_span and method in ("template", "template-f0", "partial-short"):
        mid = (a0 + b0) // 2
        if b0 - mid >= int(max(0.015 * sr, 5 * T0)):
            if method == "partial-short":
                p2 = _fund_peak(seg[mid:b0], sr, fp["hz"])
                c2 = 1200.0 * math.log2(p2["hz"] / f_expect) if p2 and p2["snr"] >= 20.0 else None
            else:
                t2 = _template(seg[mid:b0], sr, tp["f0"], kind == "string")
                c2 = (1200.0 * math.log2((t2["f1"] if method == "template" else t2["f0"]) / f_expect)
                      if t2 and t2["partials"] >= 3 and t2["resid"] <= 3.0 else None)
            if c2 is not None and 2.0 < abs(c2 - est) <= 30.0:
                est, method = c2, method + "-late"
    agree = abs(est - td["cents"]) if td and td["frames"] >= 3 else 0.0
    res.update(cents=float(est), method=method, agree=round(agree, 1), drift=None if drift is None else round(drift, 1))
    # a harmonic sound's period and its partials must agree (a stiff string / a bar's are sharp by physics)
    if kind == "harmonic" and td and td["frames"] >= 3 and agree > 12.0 and td["spread"] <= 12.0:
        res["why"] = f"partials vs period disagree by {agree:.0f} ¢"
        return res
    res["ok"] = True
    res["why"] = ""
    res["strong"] = bool((long_span and fund_ok and drift is not None and drift <= 5.0 and (agree <= 6.0 or kind != "harmonic"))
                         or (tp and method.startswith(("template", "blend"))
                             and ((tp["partials"] >= 5 and tp["resid"] <= 1.5) or (tp["partials"] >= 8 and tp["resid"] <= 2.5))))
    res["hz"] = float(f_expect * 2 ** (res["cents"] / 1200.0))
    return res


# --------------------------------------------------------------------------------------------- tfix rules
def sustained_artics(recs: List[dict], n_artics: int) -> List[bool]:
    out = []
    for a in range(n_artics):
        rs = [x for x in recs if x["a"] == a and x["kind"] == "attack"]
        out.append(bool(rs) and sum(1 for x in rs if x.get("loop") in ("sustain", "continuous")) >= 0.5 * len(rs))
    return out


def assign_tfix(recipe: dict, artic_names: List[str], recs: List[dict], f0s: Dict[int, dict],
                allow_root_fix: Optional[bool] = None, fixed_notes: Optional[set] = None) -> dict:
    """Write rec["tfix"] (and rec["_tfixFrom"]) for every attack/release record; f0s = {rec index: measurement}.
    May move rec["root"] by ±1 (a mislabelled sample). Returns the per-articulation statistics."""
    R = recipe
    modes = R.get("tfixMode", {})
    sessions = R.get("tfixSession", {})
    stats = OrderedDict()
    meas = {}
    root_fixes = []
    if allow_root_fix is None:
        allow_root_fix = R.get("rootFix", R["category"] not in ("Mallets & Bells", "Percussion"))
    per_region = R["category"] in PERFORMED_CATEGORIES
    # 1. the sample's own offset from its root (dev) — an authored whole-semitone transpose that exactly COMPENSATES it
    #    (MTG baritone: a C#2 take mapped to C2, root 36, −109 ¢) is a tuning correction, not a mislabelled root
    devs = {}
    for i, x in enumerate(recs):
        f0 = f0s.get(i)
        if x["kind"] != "attack" or R.get("unpitched") or not f0 or f0.get("hz", 0) <= 0:
            continue
        dev = 1200.0 * math.log2(f0["hz"] / an.midi_hz(x["root"]))
        cf = x["cents"] - 100.0 * math.trunc(x["cents"] / 100.0)
        # (also a fine tune that already takes back most of a semitone offset: Kawai A0, root 22 +81 ¢ on an A0 file)
        comp = ((abs(x["cents"] - cf) >= 100.0 and abs(dev + x["cents"]) <= 25.0)
                or (abs(dev) >= 50.0 and abs(dev + x["cents"]) <= 35.0))
        devs[i] = (dev, cf, comp)
    # 2. a sample a whole semitone off its declared root (a mislabelled file: Salamander's "C8" is a C#): move the root.
    #    Struck / plucked: the whole NOTE moves (every layer / take of that zone — its soft layers are the same string
    #    and often too quiet to measure), unless one of its reliable layers measures on the old root.
    cand = defaultdict(list)
    for i, (dev, cf, comp) in devs.items():
        f0 = f0s[i]
        n = int(round(dev / 100.0))
        if (not comp and abs(n) == 1 and allow_root_fix and f0.get("ok") and f0.get("strong")
                and abs(dev - 100.0 * n) <= 25.0):
            x = recs[i]
            cand[(x["a"], x["lk"], x["hk"], x["root"]) if not per_region else ("r", i)].append((i, n))
    for g, lst in cand.items():
        n = lst[0][1]
        if any(nn != n for _, nn in lst):
            continue
        if per_region:
            members = [lst[0][0]]
        else:
            a_, lk, hk, rt = g
            members = [j for j, y in enumerate(recs) if y["kind"] == "attack" and y["a"] == a_ and y["lk"] == lk
                       and y["hk"] == hk and y["root"] == rt]   # (a layer already moved is not "on the old root")
            if any(j in devs and f0s[j].get("ok") and abs(devs[j][0]) <= 25.0 for j in members):
                continue                                   # a reliable layer sits on the old root: not mislabelled
        for j in members:
            x = recs[j]
            root_fixes.append({"lk": x["lk"], "hk": x["hk"], "root": x["root"], "newRoot": x["root"] + n,
                               "measuredCents": round(devs[j][0], 1) if j in devs else None,
                               "artic": artic_names[x["a"]]})
            x["root"] += n
            if j in devs:
                devs[j] = (devs[j][0] - 100.0 * n, devs[j][1], devs[j][2])
    #    …and a struck note whose layers now sit on two roots (an earlier pass moved the layers it could read) follows
    #    its reliably-read root: the layers that cannot be read on their own root join it
    if not per_region and allow_root_fix:
        notes = defaultdict(list)
        for j, y in enumerate(recs):
            if y["kind"] == "attack":
                notes[(y["a"], y["lk"], y["hk"])].append(j)
        known = set(fixed_notes or ()) | {tuple(x) for x in R.get("rootFollow", [])}
        for (na, nlk, nhk), js in notes.items():
            if (artic_names[na], nlk, nhk) not in known:
                continue                                   # only a note the recipe names ("rootFollow": a file known mislabelled)
            roots = {recs[j]["root"] for j in js}
            if len(roots) < 2:
                continue
            good_roots = {recs[j]["root"] for j in js if j in devs and f0s[j].get("ok") and abs(devs[j][0]) <= 25.0}
            if len(good_roots) != 1:
                continue
            R_ = next(iter(good_roots))
            # only other velocity LAYERS of the same take (same RR slot): a round robin borrowed from the neighbour
            # note (a different slot, authored on its own root) keeps its root
            slots = {(tuple(recs[j]["rr"]), tuple(recs[j]["rand"])) for j in js if recs[j]["root"] == R_}
            for j in js:
                x = recs[j]
                if (x["root"] != R_ and abs(x["root"] - R_) == 1 and not (j in devs and f0s[j].get("ok"))
                        and (tuple(x["rr"]), tuple(x["rand"])) in slots):
                    root_fixes.append({"lk": x["lk"], "hk": x["hk"], "root": x["root"], "newRoot": R_,
                                       "measuredCents": None, "artic": artic_names[x["a"]], "followsNote": True})
                    x["root"] = R_
    # 3. the correction each measured region asks for; ±60 ¢ from any reliable reading, up to ±95 ¢ (the runtime takes
    #    ±100) only from a strong one (a long steady fundamental, or ≥ 5 partials fitting within 1.5 ¢)
    for i, (dev, cf, comp) in devs.items():
        f0 = f0s[i]
        tot = dev + recs[i]["cents"] if comp else dev + cf
        ok = bool(f0.get("ok")) and abs(tot) <= (95.0 if f0.get("strong") else 60.0)
        if ok and abs(tot) > 30.0 and not f0.get("strong"):
            ok = False                                     # a big correction needs a long / many-partial measurement
        meas[i] = (tot, ok, f0.get("why", "") or ("correction beyond ±60 ¢ from a weak reading" if not ok else ""))
    sus = sustained_artics(recs, len(artic_names))
    sess = [sessions.get(nm, "") for nm in artic_names]
    own = {}                                               # rec index → its own correction (reliable only)
    fits = {}
    for a, name in enumerate(artic_names):
        good = [i for i in meas if recs[i]["a"] == a and meas[i][1]]
        fit = None
        if modes.get(name) == "stretch" and len(good) >= 8:
            ks = np.array([recs[i]["root"] for i in good], float)
            ts = np.array([meas[i][0] for i in good], float)
            c = np.polyfit(ks, ts, 3)
            for _ in range(2):
                resid = ts - np.polyval(c, ks)
                keep = np.abs(resid) <= max(3.0, 2.5 * np.median(np.abs(resid)))
                if keep.sum() >= 8:
                    c = np.polyfit(ks[keep], ts[keep], 3)
            fit = c
        fits[a] = fit
        for i in good:
            own[i] = -(meas[i][0] - (float(np.polyval(fit, recs[i]["root"])) if fit is not None else 0.0))

    def _nk(x):
        return (x["a"], x["root"], tuple(x["rr"]), tuple(x["rand"]))

    per_note = defaultdict(list)
    per_root = defaultdict(list)                            # (artic, root) → corrections (all takes)
    for i, v in own.items():
        per_note[_nk(recs[i])].append(v)
        per_root[(recs[i]["a"], recs[i]["root"])].append(v)

    def clamp(v):
        return round(max(-TFIX_MAX, min(TFIX_MAX, float(v))), 1)

    def from_sustained(a, root):
        """The same key (or the nearest measured key ≤ 3 away) of a sustained articulation of the same session."""
        if modes.get(artic_names[a]) == "stretch":
            return None
        best = None
        for b in range(len(artic_names)):
            if b == a or not sus[b] or sess[b] != sess[a] or modes.get(artic_names[b]) == "stretch":
                continue
            roots = sorted({r for (bb, r) in per_root if bb == b})
            if not roots:
                continue
            d = min(abs(r - root) for r in roots)
            if d > 3:
                continue
            vals = [v for r in roots if abs(r - root) == d for v in per_root[(b, r)]]
            if best is None or d < best[0]:
                best = (d, float(np.median(vals)), artic_names[b])
        return best

    src_count = defaultdict(lambda: defaultdict(int))
    for i, x in enumerate(recs):
        if x["kind"] != "attack":
            continue
        a = x["a"]
        v, frm = None, ""
        if i in own:
            if per_region:
                v, frm = own[i], "measured"
            else:
                v, frm = float(np.median(per_note[_nk(x)])), "measured"
        elif R.get("unpitched"):
            v, frm = 0.0, "unpitched"
        else:
            pn = per_note.get(_nk(x)) or per_root.get((a, x["root"]))
            if pn:
                v, frm = float(np.median(pn)), "note"
            else:
                s = from_sustained(a, x["root"])
                if s:
                    v, frm = s[1], f"sustained:{s[2]}" + (f"±{s[0]}" if s[0] else "")
                else:
                    nb = [own[j] for j in own if recs[j]["a"] == a and abs(recs[j]["root"] - x["root"]) <= 4]
                    if len(nb) >= 2:
                        v, frm = float(np.median(nb)), "neighbours"
                    else:
                        v, frm = 0.0, "none"
        x["tfix"] = clamp(v)
        x["_tfixFrom"] = frm
        src_count[a][frm.split(":")[0]] += 1
    for a, name in enumerate(artic_names):
        idx = [i for i in meas if recs[i]["a"] == a]
        good = [i for i in idx if meas[i][1]]
        notes = defaultdict(list)
        for i, x in enumerate(recs):
            if x["kind"] == "attack" and x["a"] == a:
                notes[_nk(x)].append(x["tfix"])
        vals = [float(np.median(v)) for v in notes.values()]
        st = OrderedDict(measuredRegions=len(good), unreliableRegions=len(idx) - len(good), notes=len(notes),
                         mode="stretch" if fits.get(a) is not None else "equal", sources=dict(src_count[a]))
        why = defaultdict(int)
        for i in idx:
            if not meas[i][1]:
                why[meas[i][2] or "correction out of range"] += 1
        if why:
            st["unreliableWhy"] = dict(sorted(why.items(), key=lambda t: -t[1])[:6])
        if vals:
            w = max(vals, key=abs)
            wr = [k[1] for k, v in notes.items() if float(np.median(v)) == w][0]
            st.update(worst=round(w, 1), worstRoot=int(wr), medianAbs=round(float(np.median(np.abs(vals))), 1),
                      over10=int(sum(1 for v in vals if abs(v) > 10)))
        if fits.get(a) is not None:
            st["stretchCurveCents"] = {str(k): round(float(np.polyval(fits[a], k)), 1)
                                       for k in (21, 36, 48, 60, 72, 84, 96, 108)}
        stats[name] = st
    if root_fixes:
        stats["rootFixes"] = root_fixes
    # releases: the attack correction of the same (artic, root)
    by_root = defaultdict(list)
    for x in recs:
        if x["kind"] == "attack":
            by_root[(x["a"], x["root"])].append(x["tfix"])
    for x in recs:
        if x["kind"] == "release":
            v = by_root.get((x["a"], x["root"]))
            x["tfix"] = round(float(np.median(v)), 1) if v else 0.0
        elif x["kind"] == "noise":
            x["tfix"] = 0.0
    return stats
