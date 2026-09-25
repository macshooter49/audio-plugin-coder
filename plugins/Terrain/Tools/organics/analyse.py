"""analyse.py — the audio side of the Organics compiler (and of its test).

  envelope_db()       10 ms RMS envelope in dB
  find_onset()        the frame where the transient crosses −24 dB of the region peak (design §2.3 Attack)
  find_tail_loop()    decaying instruments: a stable 150–600 ms window near the −30 dB point, found by
                      normalised cross-correlation, played with an equal-power crossfade (design §2.4 Sustain)
  find_sustain_loop() sustaining instruments without authored loops (VSCO, VCSL): the longest well-matched
                      loop inside the steady part of the note
  seam_metric()       renders the loop join the way the runtime does (equal-power crossfade of the xf frames
                      before `le` with the xf frames before `ls`, then continues at `ls`) and compares the local
                      peak |Δ| at the join with the typical local peak |Δ| around it. Pass = ratio ≤ 1.5.
  polish_loop()       tp106: every sustain loop's final shape — a repeating swell flattened (loop_pump_db), the
                      cleanest crossfade length chosen (seam_click_db) and BAKED into the audio (bake_loop_xfade)
  read_smpl_loops()   WAV 'smpl' chunk (root note + loops) for libraries with pre-looped files
  Renderer            a tiny offline sampler over a compiled map.json, used for preview.flac and the
                      listen-by-numbers QA. It is NOT the runtime; it only has to be honest about levels.
"""
from __future__ import annotations

import math
import os
import struct
from typing import Dict, List, Optional, Tuple

import numpy as np
from scipy.signal import fftconvolve

HOP_S = 0.010


def to_mono(x: np.ndarray) -> np.ndarray:
    return x if x.ndim == 1 else x.mean(axis=1)


def db(v: float, floor: float = -200.0) -> float:
    return 20.0 * math.log10(v) if v > 1e-12 else floor


def envelope_db(mono: np.ndarray, sr: int, hop_s: float = HOP_S) -> np.ndarray:
    hop = max(1, int(sr * hop_s))
    n = len(mono) // hop
    if n == 0:
        return np.array([db(float(np.sqrt(np.mean(mono ** 2)))) if len(mono) else -200.0])
    seg = mono[: n * hop].reshape(n, hop)
    rms = np.sqrt(np.mean(seg.astype(np.float64) ** 2, axis=1))
    return 20.0 * np.log10(np.maximum(rms, 1e-10))


def find_onset(mono: np.ndarray, start: int, end: int, thresh_db: float = -24.0) -> int:
    """First frame ≥ start whose |x| reaches (region peak + thresh_db). Returns an absolute frame."""
    seg = np.abs(mono[start:end])
    if len(seg) == 0:
        return start
    pk = float(seg.max())
    if pk <= 0:
        return start
    lim = pk * (10.0 ** (thresh_db / 20.0))
    idx = np.flatnonzero(seg >= lim)
    return start + int(idx[0]) if len(idx) else start


def trim_end(mono: np.ndarray, sr: int, start: int, rel_db: float = -60.0, floor_abs_db: float = -96.0) -> int:
    """Frame after which the note stays below (peak + rel_db) — the −60 dB tail cut (design §5.2)."""
    env = envelope_db(mono[start:], sr)
    if len(env) == 0:
        return len(mono)
    pk = env.max()
    lim = max(pk + rel_db, floor_abs_db)
    above = np.flatnonzero(env >= lim)
    if len(above) == 0:
        return len(mono)
    hop = int(sr * HOP_S)
    return min(len(mono), start + (int(above[-1]) + 1) * hop)


def _ncc_search(mono: np.ndarray, t_center: int, lo: int, hi: int, half: int) -> Tuple[int, float]:
    """Best centre c in [lo, hi] maximising NCC between mono[t_center±half] and mono[c±half]."""
    T = mono[t_center - half: t_center + half].astype(np.float64)
    if len(T) < 2 * half or lo > hi:
        return -1, -1.0
    a = lo - half
    b = hi + half
    if a < 0 or b > len(mono):
        return -1, -1.0
    S = mono[a:b].astype(np.float64)
    T0 = T - T.mean()
    tn = np.sqrt(np.sum(T0 ** 2))
    if tn < 1e-9:
        return -1, -1.0
    num = fftconvolve(S, T0[::-1], mode="valid")          # len = len(S) - len(T) + 1 = hi-lo+1
    c1 = np.concatenate([[0.0], np.cumsum(S)])
    c2 = np.concatenate([[0.0], np.cumsum(S ** 2)])
    L = len(T)
    sums = c1[L:] - c1[:-L]
    sq = c2[L:] - c2[:-L]
    var = np.maximum(sq - sums ** 2 / L, 1e-12)
    ncc = num / (tn * np.sqrt(var))
    k = int(np.argmax(ncc))
    return lo + k, float(ncc[k])


def _rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(x.astype(np.float64) ** 2))) if len(x) else 0.0


def seam_metric(x: np.ndarray, ls: int, le: int, xf: int) -> Dict[str, float]:
    """Render the loop join (equal-power, content crossfade from the lead-in before ls) and measure it."""
    mono = to_mono(x).astype(np.float64)
    M = 512
    xf = int(max(0, min(xf, ls, le - ls)))
    a = max(0, le - xf - M)
    pre = mono[a: le - xf]
    if xf > 0:
        t = (np.arange(xf) + 0.5) / xf
        cross = mono[le - xf: le] * np.cos(t * math.pi / 2) + mono[ls - xf: ls] * np.sin(t * math.pi / 2)
    else:
        cross = np.zeros(0)
    post = mono[ls: ls + M]
    y = np.concatenate([pre, cross, post])
    if len(y) < 32:
        return {"ratio": 0.0, "xfDb": 0.0}
    d = np.abs(np.diff(y))
    j = len(pre) + len(cross)                      # index of the first frame after the join
    w = 4
    seam_pk = float(d[max(0, j - 1 - w): j - 1 + w + 1].max()) if j - 1 < len(d) else 0.0
    # typical local peak |Δ|: max over 9-frame windows, median over the neighbourhood
    k = 2 * w + 1
    n = len(d) // k
    ref = float(np.median(d[: n * k].reshape(n, k).max(axis=1))) if n > 0 else float(d.max())
    ratio = seam_pk / max(ref, 1e-9)
    # crossfade level vs its neighbourhood (a dip = phase cancellation, a bump = +3 dB correlation)
    xf_db = 0.0
    if xf > 0:
        nb = _rms(np.concatenate([pre[-xf:], post[:xf]]))
        xf_db = db(_rms(cross)) - db(nb) if nb > 0 else 0.0
    return {"ratio": ratio, "xfDb": xf_db}


def find_tail_loop(x: np.ndarray, sr: int, start: int, end: int) -> Optional[Tuple[int, int, int, Dict]]:
    """Decaying note: a 150–600 ms loop near the −30 dB point (relative to the region peak)."""
    mono = to_mono(x).astype(np.float64)
    end = min(end, len(mono))
    if end - start < int(0.6 * sr):
        return None
    env = envelope_db(mono[start:end], sr)
    hop = int(sr * HOP_S)
    pk_i = int(np.argmax(env))
    pk = env[pk_i]
    below = np.flatnonzero(env[pk_i:] <= pk - 30.0)
    if len(below):
        c = start + (pk_i + int(below[0])) * hop
    else:
        c = start + int(len(env) * 0.7) * hop     # never reaches −30 dB inside the kept sample
    Lmin, Lmax = int(0.15 * sr), int(0.6 * sr)
    half = int(0.008 * sr)
    lo_ls = max(start + int(0.1 * sr), c - Lmax, half + 1)
    cands = []
    # candidate loop starts spread over [c - Lmax, c]; loop end searched in [ls+Lmin, ls+Lmax]
    for ls in np.linspace(lo_ls, max(lo_ls, c), 13).astype(int):
        hi = min(ls + Lmax, end - half - 1)
        lo = ls + Lmin
        if hi <= lo:
            continue
        le, ncc = _ncc_search(mono, int(ls), lo, hi, half)
        if le < 0:
            continue
        L = le - ls
        xf = int(min(0.3 * L, 0.06 * sr, ls))
        lvl = abs(db(_rms(mono[ls: ls + half * 2])) - db(_rms(mono[le - half * 2: le])))
        score = ncc - 0.08 * lvl
        cands.append((score, int(ls), int(le), xf, ncc, lvl))
    return _first_passing(mono, sr, cands, max_xf_s=0.12, min_ncc=0.3)


def _first_passing(mono, sr, cands, max_xf_s, min_ncc):
    """Best-scoring candidate whose rendered seam passes (ratio ≤ 1.5), trying a few crossfade lengths."""
    for score, ls, le, xf, ncc, lvl in sorted(cands, key=lambda t: -t[0]):
        if ncc < min_ncc:
            continue
        L = le - ls
        for xf_try in (xf, int(xf * 1.6), int(min(0.45 * L, max_xf_s * sr)), max(1, xf // 3)):
            xf_try = int(max(1, min(xf_try, ls, 0.45 * L)))
            m = seam_metric(mono, ls, le, xf_try)
            if m["ratio"] <= 1.45 and m["xfDb"] > -4.5:
                return ls, le, xf_try, dict(m, ncc=ncc, levelDiffDb=lvl)
    return None


def find_sustain_loop(x: np.ndarray, sr: int, start: int, end: int, onset: int) -> Optional[Tuple[int, int, int, Dict]]:
    """Sustaining note without an authored loop: the longest well-matched loop in the steady part."""
    mono = to_mono(x).astype(np.float64)
    end = min(end, len(mono))
    env = envelope_db(mono[start:end], sr)
    hop = int(sr * HOP_S)
    if len(env) < 40:
        return None
    a_i = max(int((onset - start) / hop) + int(0.25 / HOP_S), 1)
    if a_i >= len(env) - 10:
        return None
    steady = env[a_i:]
    med = float(np.median(steady[: max(10, len(steady) // 2)]))
    ok = np.flatnonzero(steady >= med - 6.0)
    if len(ok) == 0:
        return None
    b_i = a_i + int(ok[-1])
    a, b = start + a_i * hop, start + b_i * hop
    span = b - a
    if span < int(0.3 * sr):
        return None
    Lmin = int(min(0.4 * sr, span * 0.5))
    Lmax = int(min(3.0 * sr, span * 0.92))
    half = int(0.012 * sr)
    cands = []
    for ls in np.linspace(a, a + max(0, span - Lmin) * 0.35, 9).astype(int):
        lo = ls + Lmin
        hi = min(ls + Lmax, b - half - 1)
        if hi <= lo:
            continue
        for sub_lo, sub_hi in ((lo, hi), (lo, lo + (hi - lo) // 2), (lo + (hi - lo) // 2, hi)):
            if sub_hi <= sub_lo:
                continue
            le, ncc = _ncc_search(mono, int(ls), sub_lo, sub_hi, half)
            if le < 0:
                continue
            L = le - ls
            xf = int(min(0.25 * L, 0.1 * sr, ls))
            lvl = abs(db(_rms(mono[ls: ls + half * 4])) - db(_rms(mono[le - half * 4: le])))
            score = ncc - 0.06 * lvl + 0.05 * (L / Lmax)
            cands.append((score, int(ls), int(le), xf, ncc, lvl))
    return _first_passing(mono, sr, cands, max_xf_s=0.2, min_ncc=0.2)


def decay_rate_db_s(mono: np.ndarray, sr: int, a: int, b: int) -> float:
    """Least-squares decay slope (dB/s, positive = decaying) of the 10 ms envelope over [a, b)."""
    env = envelope_db(mono[a:b], sr)
    if len(env) < 5:
        return 0.0
    t = np.arange(len(env)) * HOP_S
    slope = float(np.polyfit(t, env, 1)[0])
    return -slope


def extend_decay(x: np.ndarray, sr: int, ls: int, le: int, xf: int, rate_db_s: float, ext: int) -> np.ndarray:
    """Continue a decaying note past `le` for `ext` frames by repeating its tail loop (equal-power seams),
    with the loop body's own decay flattened and the measured decay re-applied continuously."""
    x2 = x if x.ndim == 2 else x[:, None]
    n_total = le + ext
    idx = np.arange(ls - xf, le)
    g = 10.0 ** (rate_db_s * ((idx - ls) / sr) / 20.0)
    f = x2[ls - xf: le] * g[:, None]                       # flattened body incl. the lead-in
    L = le - ls
    body = f[xf:]                                           # f over [ls, le)
    cyc = body.copy()
    if xf > 0:
        t = ((np.arange(xf) + 0.5) / xf)[:, None]
        cyc[-xf:] = body[-xf:] * np.cos(t * math.pi / 2) + f[:xf] * np.sin(t * math.pi / 2)
    reps = int(math.ceil((n_total - ls) / L)) + 1
    s = np.concatenate([cyc] * reps)[: n_total - ls]
    m = np.arange(ls, n_total)
    z = s * (10.0 ** (-rate_db_s * ((m - ls) / sr) / 20.0))[:, None]
    out = np.concatenate([x2[:ls], z])
    return out if x.ndim == 2 else out[:, 0]


# --------------------------------------------------------------------------------------------- loop polish (tp106)
LOOP_PAD = 8          # frames after `le` that repeat the frames from `ls` (the interpolators read up to le + 3)


def loop_pump_db(mono: np.ndarray, sr: int, ls: int, le: int) -> float:
    """The slow level ripple a sustain loop REPEATS: 200 ms RMS every 25 ms over two passes of [ls, le) (the wrap
    included), max − min in dB. 200 ms averages vibrato / tremolo AM out; a bow change or swell inside the loop
    comes back every loop length and reads as pumping."""
    seg = np.concatenate([mono[ls:le], mono[ls:le]]).astype(np.float64)
    w, h = int(0.2 * sr), max(1, int(0.025 * sr))
    if len(seg) < w + h:
        return 0.0
    c = np.concatenate([[0.0], np.cumsum(seg ** 2)])
    idx = np.arange(0, len(seg) - w, h)
    e = 10.0 * np.log10((c[idx + w] - c[idx]) / w + 1e-20)
    return float(e.max() - e.min())


def _smooth_env(mono: np.ndarray, sr: int, a: int, le: int, win_s: float = 0.2) -> np.ndarray:
    """RMS envelope (linear) per frame over [a, le): a centred `win_s` window over the recording as it is (clipped at
    the file edges). Continuous by construction, so a gain derived from it never steps."""
    w2 = max(1, int(win_s * sr) // 2)
    lo0 = max(0, a - w2)
    seg = mono[lo0: min(len(mono), le + w2)].astype(np.float64)
    c = np.concatenate([[0.0], np.cumsum(seg ** 2)])
    j = np.arange(a, le) - lo0
    lo = np.clip(j - w2, 0, len(seg)); hi = np.clip(j + w2, 0, len(seg))
    return np.sqrt((c[hi] - c[lo]) / np.maximum(1, hi - lo))


def flatten_loop(x: np.ndarray, sr: int, ls: int, le: int, lead: int, max_db: float = 12.0) -> Tuple[np.ndarray, Dict[str, float]]:
    """Level the slow (200 ms) envelope of [ls − lead, le) to the level at ls − lead, so the loop no longer repeats a
    swell or a bow change (vibrato / tremolo / bow grain are faster and stay). Gain = target / envelope, which is 1 at
    ls − lead (the recording continues into it without a step); the level at `le` and at `ls` both land on the target,
    so the baked crossfade between them joins two equal levels. ±max_db."""
    x2 = (x if x.ndim == 2 else x[:, None]).astype(np.float64).copy()
    mono = x2.mean(axis=1)
    a = max(0, ls - lead)
    env = _smooth_env(mono, sr, a, le)
    target = env[0]
    g = target / np.maximum(env, 1e-9)
    lim = 10.0 ** (max_db / 20.0)
    g = np.clip(g, 1.0 / lim, lim)
    x2[a:le] *= g[:, None]
    info = {"flattenMinDb": round(db(float(g.min())), 2), "flattenMaxDb": round(db(float(g.max())), 2)}
    return (x2 if x.ndim == 2 else x2[:, 0]), info


def bake_loop_xfade(x: np.ndarray, ls: int, le: int, xf: int) -> Tuple[np.ndarray, float]:
    """Write the loop's crossfade INTO the audio: the xf frames before `le` become a blend of themselves and the xf
    frames before `ls`, with an energy-preserving law for the measured correlation ρ of the two (ρ = 1 → equal gain,
    ρ = 0 → equal power; a + b and a² + b² + 2ρab = 1 in between), and LOOP_PAD frames after `le` repeat the frames
    from `ls`. The runtime then wraps le → ls with no crossfade of its own (map xf = 0): the last frame of the blend
    IS the frame before `ls`, so the join is sample-continuous for every interpolator."""
    x2 = (x if x.ndim == 2 else x[:, None]).astype(np.float64).copy()
    A, B = x2[le - xf: le], x2[ls - xf: ls]
    den = math.sqrt(float(np.sum(A * A)) * float(np.sum(B * B)))
    rho = float(np.sum(A * B)) / den if den > 1e-20 else 1.0
    rho = min(1.0, max(0.0, rho))
    t = (np.arange(xf) + 1.0) / xf                     # reaches exactly 1 on the last blended frame
    b = t * t * (3.0 - 2.0 * t)
    a = -rho * b + np.sqrt(np.maximum(0.0, 1.0 - b * b * (1.0 - rho * rho)))
    x2[le - xf: le] = A * a[:, None] + B * b[:, None]
    need = le + LOOP_PAD
    if need > len(x2):
        x2 = np.concatenate([x2, np.zeros((need - len(x2), x2.shape[1]))])
    x2[le: need] = x2[ls: ls + LOOP_PAD]
    return (x2 if x.ndim == 2 else x2[:, 0]), rho


def seam_click_db(x: np.ndarray, sr: int, ls: int, le: int) -> Dict[str, float]:
    """A BAKED loop's join (wrap le → ls, no runtime crossfade) as the runtime plays it: the 200 ms before `le`
    followed by the 200 ms from `ls`, per channel. clickDb = the largest HF(8 kHz, 4th-order) peak within ±2 ms of
    the join over the largest HF peak anywhere else in that 400 ms (> 0 dB = the join is the sharpest event there);
    levelDb = the 20 ms RMS just after the join re just before (a step)."""
    from scipy.signal import butter, sosfilt
    x2 = x if x.ndim == 2 else x[:, None]
    M = int(0.2 * sr)
    a0 = max(0, le - M)
    sos = butter(4, 8000.0 / (sr / 2.0), btype="highpass", output="sos") if sr > 16000 else None
    worst, lvl = -200.0, 0.0
    for ch in range(x2.shape[1]):
        y = np.concatenate([x2[a0: le, ch], x2[ls: ls + M, ch]]).astype(np.float64)
        j = le - a0
        if sos is not None:
            h = np.abs(sosfilt(sos, y))
            k = int(0.002 * sr)
            near = h[max(0, j - k): j + k]
            far = np.concatenate([h[int(0.01 * sr): max(0, j - 4 * k)], h[j + 4 * k:]])
            if len(near) and len(far):
                worst = max(worst, db(float(near.max())) - db(float(far.max()) + 1e-20))
        q = int(0.02 * sr)
        lvl = max(lvl, abs(db(_rms(y[j: j + q])) - db(_rms(y[j - q: j]))))
    return {"clickDb": round(worst, 2), "levelDb": round(lvl, 2)}


def polish_loop(x: np.ndarray, sr: int, ls: int, le: int, xf0: int, onset: int, flatten: bool) -> Tuple[np.ndarray, int, Dict[str, float]]:
    """tp106 — the final shape of a sustain loop: (1) flatten a repeating swell (pump > 1.5 dB) when allowed, (2) pick
    the crossfade length that makes the cleanest join (10 ms … 200 ms, ≤ 45 % of the loop, lead-in after the attack),
    (3) bake it into the audio. Returns (new audio, xf for the map = 0, info)."""
    mono = to_mono(x).astype(np.float64)
    L = le - ls
    room = ls - (onset + int(0.08 * sr))                # the lead-in must not reach back into the attack
    cands = sorted({int(v) for v in (xf0, 0.010 * sr, 0.025 * sr, 0.05 * sr, 0.1 * sr, 0.2 * sr)
                    if 64 <= int(v) <= min(0.45 * L, max(64, room), ls)})
    info = {"pumpBeforeDb": round(loop_pump_db(mono, sr, ls, le), 2)}
    if not cands:
        cands = [int(v) for v in (min(ls, 0.45 * L),) if int(v) >= 8]
    if not cands:                                       # no lead-in before `ls` (a loop from the file's first frames)
        info.update(xfBaked=0, rho=1.0, seamClickDb=seam_click_db(x, sr, ls, le)["clickDb"], seamLevelDb=0.0,
                    pumpAfterDb=info["pumpBeforeDb"], noLeadIn=True)
        return x, xf0, info
    y = x
    if flatten and info["pumpBeforeDb"] > 1.5:
        y, fi = flatten_loop(x, sr, ls, le, max(cands))
        info.update(fi)
    best = None
    for xf in cands:
        z, rho = bake_loop_xfade(y, ls, le, xf)
        m = seam_click_db(z, sr, ls, le)
        score = max(0.0, m["clickDb"]) + 2.0 * m["levelDb"] + (0.5 if xf < 0.02 * sr else 0.0)
        if best is None or score < best[0]:
            best = (score, xf, z, rho, m)
    _, xf, z, rho, m = best
    info.update(xfBaked=int(xf), rho=round(rho, 3), seamClickDb=m["clickDb"], seamLevelDb=m["levelDb"],
                pumpAfterDb=round(loop_pump_db(to_mono(z).astype(np.float64), sr, ls, le), 2))
    return z, 0, info


def read_smpl_loops(path: str) -> Tuple[Optional[int], List[Tuple[int, int]]]:
    """(unity note, [(start, end_exclusive)]) from a WAV 'smpl' chunk; (None, []) otherwise."""
    if not path.lower().endswith(".wav"):
        return None, []
    try:
        with open(path, "rb") as f:
            hdr = f.read(12)
            if len(hdr) < 12 or hdr[:4] not in (b"RIFF", b"RF64") or hdr[8:12] != b"WAVE":
                return None, []
            while True:
                ch = f.read(8)
                if len(ch) < 8:
                    break
                cid, size = ch[:4], struct.unpack("<I", ch[4:])[0]
                if cid == b"smpl":
                    data = f.read(size)
                    if len(data) < 36:
                        return None, []
                    unity = struct.unpack("<I", data[12:16])[0]
                    nloops = struct.unpack("<I", data[28:32])[0]
                    loops = []
                    for i in range(nloops):
                        o = 36 + 24 * i
                        if o + 24 > len(data):
                            break
                        _, typ, s, e, _, _ = struct.unpack("<IIIIII", data[o:o + 24])
                        if e > s:
                            loops.append((int(s), int(e) + 1))
                    return (int(unity) if 0 < unity < 128 else None), loops
                f.seek(size + (size & 1), 1)
    except OSError:
        pass
    return None, []


# --------------------------------------------------------------------------------------------- pitch (tfix)
def _yin_cmnd(frame: np.ndarray, tau_max: int) -> np.ndarray:
    """YIN cumulative-mean-normalised difference d'(tau), tau = 0..tau_max (de Cheveigné & Kawahara 2002)."""
    n = len(frame)
    w = n - tau_max
    x = frame.astype(np.float64)
    # d(tau) = Σ_{j<w} (x_j − x_{j+tau})² = e0 + e_tau − 2 r(tau), with an FFT cross-correlation
    nfft = 1 << int(math.ceil(math.log2(n + w)))
    X = np.fft.rfft(x, nfft)
    Y = np.fft.rfft(x[:w], nfft)
    r = np.fft.irfft(X * np.conj(Y), nfft)[: tau_max + 1]
    c = np.concatenate([[0.0], np.cumsum(x ** 2)])
    e0 = c[w]
    et = c[np.arange(tau_max + 1) + w] - c[np.arange(tau_max + 1)]
    d = np.maximum(e0 + et - 2.0 * r, 0.0)
    cm = np.cumsum(d[1:]) / np.arange(1, tau_max + 1)
    out = np.ones(tau_max + 1)
    out[1:] = d[1:] / np.maximum(cm, 1e-20)
    return out


def measure_f0(mono: np.ndarray, sr: int, onset: int, end: int, f_expect: float,
               search_cents: float = 150.0) -> Dict[str, float]:
    """Pitch of the sustained part of a note, near f_expect (the region's root in equal temperament).

    YIN over up to 8 frames from ~onset+max(60 ms, 6 periods) while the level stays within 35 dB of the note's
    peak. The lag is searched only within ±search_cents of k·T0, where k is the smallest multiple of the
    expected period that spans ≥ 256 samples (a k-period lag has k-fold finer resolution for high notes), and
    refined by parabolic interpolation. Returns {hz, cents (vs f_expect), spread (IQR, cents), conf (1−d'),
    frames}; hz = 0 when nothing periodic was found."""
    res = {"hz": 0.0, "cents": 0.0, "spread": 0.0, "conf": 0.0, "frames": 0}
    if f_expect <= 0 or end - onset < int(0.03 * sr):
        return res
    T0 = sr / f_expect
    k = max(1, int(math.ceil(256.0 / T0)))
    lo1 = max(2, int(math.floor(T0 * 2 ** (-search_cents / 1200.0))) - 1)
    hi1 = int(math.ceil(T0 * 2 ** (search_cents / 1200.0))) + 1
    hi = int(math.ceil(k * hi1 + T0)) + 2
    win = int(max(3 * hi, 0.04 * sr))
    seg = mono[onset:end]
    env = envelope_db(seg, sr)
    if len(env) == 0:
        return res
    pk = float(env.max())
    skip = int(max(0.06 * sr, 6 * T0))
    if len(seg) - skip < win + hi:                    # short notes (staccato/pizz): start earlier
        skip = int(max(0.015 * sr, 2 * T0))
    hop_env = int(sr * HOP_S)
    starts = []
    s = skip
    step = max(int(0.05 * sr), win // 2)
    while s + win + hi <= len(seg) and len(starts) < 8:
        e0 = min(len(env) - 1, s // hop_env)
        e1 = min(len(env) - 1, (s + win) // hop_env)
        if env[e0:e1 + 1].min() < pk - 35.0:
            break
        starts.append(s)
        s += step

    def _parab(d, i):
        a, b, c = d[i - 1], d[i], d[i + 1]
        den = a - 2 * b + c
        off = 0.5 * (a - c) / den if abs(den) > 1e-12 else 0.0
        return i + max(-0.5, min(0.5, off)), float(b)

    ests, confs = [], []
    for s in starts:
        fr = seg[s:s + win + hi]
        d = _yin_cmnd(fr, hi)
        # stage 1: the single period, within ±search_cents of the expected one
        i1 = int(np.argmin(d[lo1:hi1 + 1])) + lo1
        if i1 <= lo1 or i1 >= hi1:
            continue                                  # minimum on the search edge: not the expected pitch
        lag1, b1 = _parab(d, i1)
        # stage 2: the k-th multiple of that period (k-fold resolution), searched within ±T/3
        if k > 1:
            c2 = k * lag1
            lo2, hi2 = int(math.floor(c2 - lag1 / 3)), int(math.ceil(c2 + lag1 / 3))
            if hi2 + 1 <= hi:
                i2 = int(np.argmin(d[lo2:hi2 + 1])) + lo2
                if lo2 < i2 < hi2:
                    lag2, _ = _parab(d, i2)
                    lag1 = lag2 / k
        ests.append(sr / lag1)
        confs.append(1.0 - b1)
    if not ests:
        return res
    cents = 1200.0 * np.log2(np.array(ests) / f_expect)
    good = np.array(confs) >= 0.6
    if good.sum() >= 1:
        cents = cents[good]
        confs = list(np.array(confs)[good])
    med = float(np.median(cents))
    q = np.percentile(cents, [25, 75]) if len(cents) > 1 else [med, med]
    res.update(hz=float(f_expect * 2 ** (med / 1200.0)), cents=med, spread=float(q[1] - q[0]),
               conf=float(np.median(confs)), frames=int(len(cents)), method="yin")
    # refinement: the frequency of the fundamental PARTIAL (what a tuner measures), from a long Hann-windowed FFT
    # around the YIN estimate — immune to the inharmonic upper partials that bias a period estimate on high piano
    # notes. Used when the fundamental is within 24 dB of the strongest partial (a weak-fundamental bass note keeps
    # the YIN period, which follows the low partials the ear uses there).
    a0 = starts[0]
    b0 = min(len(seg), max(starts[-1] + win, a0 + int(0.25 * sr)))       # the span the YIN frames covered
    e_ok = a0
    while e_ok + hop_env <= b0 and env[min(len(env) - 1, e_ok // hop_env)] >= pk - 35.0:
        e_ok += hop_env
    fr = seg[a0:e_ok]
    if len(fr) >= int(8 * T0) and len(fr) >= int(0.08 * sr):
        nfft = 1 << int(math.ceil(math.log2(len(fr) * 4)))
        X = np.abs(np.fft.rfft(fr * np.hanning(len(fr)), nfft))
        lx = 20 * np.log10(np.maximum(X, 1e-12))
        hz0 = res["hz"]
        i_lo = int(hz0 * 2 ** (-40 / 1200.0) * nfft / sr)
        i_hi = int(math.ceil(hz0 * 2 ** (40 / 1200.0) * nfft / sr))
        top = float(lx[int(20 * nfft / sr):].max())
        if 1 <= i_lo < i_hi < len(lx) - 1:
            i = int(np.argmax(lx[i_lo:i_hi + 1])) + i_lo
            if i_lo < i < i_hi and lx[i] >= top - 24.0:
                a, b, c = lx[i - 1], lx[i], lx[i + 1]
                den = a - 2 * b + c
                off = 0.5 * (a - c) / den if abs(den) > 1e-12 else 0.0
                hz_s = (i + max(-0.5, min(0.5, off))) * sr / nfft
                c_s = float(1200.0 * math.log2(hz_s / f_expect))
                # agree = |partial − YIN| in cents: a note whose pitch moves (a bent/sliding take) disagrees
                res.update(hz=float(hz_s), cents=c_s, method="partial", agree=abs(c_s - med))
    return res


def midi_hz(n: float) -> float:
    return 440.0 * 2.0 ** ((n - 69.0) / 12.0)


# --------------------------------------------------------------------------------------------- loudness
def _k_filter_coeffs(sr: int):
    """ITU-R BS.1770 K-weighting (shelf + RLB high-pass) for any sample rate (the standard's analog prototypes,
    bilinear-transformed — identical to the published 48 kHz coefficients at 48 kHz)."""
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = math.tan(math.pi * f0 / sr)
    Vh = 10 ** (G / 20.0)
    Vb = Vh ** 0.4996667741545416
    a0 = 1.0 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * f0 / sr)
    a0 = 1.0 + K / Q + K * K
    b2 = [1.0, -2.0, 1.0]
    a2 = [1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0]
    return (b1, a1), (b2, a2)


def loudness_k(x: np.ndarray, sr: int) -> float:
    """K-weighted loudness (LUFS-style, ungated) of a (frames, ch) block: −0.691 + 10·log10(Σ_ch mean(y²))."""
    from scipy.signal import lfilter
    x = x if x.ndim == 2 else x[:, None]
    (b1, a1), (b2, a2) = _k_filter_coeffs(sr)
    y = lfilter(b2, a2, lfilter(b1, a1, x, axis=0), axis=0)
    p = float(np.sum(np.mean(y ** 2, axis=0)))
    return -0.691 + 10.0 * math.log10(p) if p > 1e-20 else -200.0


# --------------------------------------------------------------------------------------------- renderer
class Renderer:
    """Offline mix of the regions a note would trigger. Linear-phase FFT resampling per region."""

    def __init__(self, inst_dir: str, out_sr: int = 48000, velo: float = 1.0):
        import json
        import soundfile as sf
        self.sf = sf
        self.dir = inst_dir
        with open(os.path.join(inst_dir, "map.json")) as f:
            self.map = json.load(f)
        self.sr = out_sr
        self.velo = velo                   # the runtime's Velocity knob: gain = 1 − velo·(1 − curve(vel))
        self._cache: Dict[int, Tuple[np.ndarray, int]] = {}

    def sample(self, smp: int) -> Tuple[np.ndarray, int]:
        if smp not in self._cache:
            x, sr = self.sf.read(os.path.join(self.dir, "samples", self.map["samples"][smp]), dtype="float32", always_2d=True)
            self._cache[smp] = (x, sr)
        return self._cache[smp]

    @staticmethod
    def vel_gain(curve, v):
        pts = np.array(curve, dtype=float)
        return float(np.interp(v, pts[:, 0], pts[:, 1]))

    def pick(self, note: int, vel: int, artic: int = 0, kind: str = "attack", xfade: bool = True) -> List[Tuple[dict, float]]:
        """Regions a note plays (first RR / first random slot) with their weights. Attack layers use the
        design's equal-power crossfade between the two bands bracketing `vel` (weight 1 at a band centre)."""
        rs = [r for r in self.map["regions"] if r["a"] == artic and r["kind"] == kind
              and r["lk"] <= note <= r["hk"] and r["rr"][0] == 0 and r["rand"][0] <= 1e-9]
        if kind != "attack" or not xfade:
            return [(r, 1.0) for r in rs if r["lv"] <= vel <= r["hv"]]
        bands = sorted({(r["lv"], r["hv"]) for r in rs})
        cur = [b for b in bands if b[0] <= vel <= b[1]]
        if not cur:
            return []
        i = bands.index(cur[0])
        ci = (cur[0][0] + cur[0][1]) / 2.0
        j = None
        if vel >= ci and i + 1 < len(bands):
            j = i + 1
        elif vel < ci and i > 0:
            j = i - 1
        out = []
        if j is None:
            wi, wj = 1.0, 0.0
        else:
            cj = (bands[j][0] + bands[j][1]) / 2.0
            t = min(1.0, max(0.0, abs(vel - ci) / max(1e-9, abs(cj - ci))))
            wi, wj = math.cos(t * math.pi / 2), math.sin(t * math.pi / 2)
        for r in rs:
            b = (r["lv"], r["hv"])
            if b == bands[i]:
                out.append((r, wi))
            elif j is not None and b == bands[j] and wj > 1e-6:
                out.append((r, wj))
        return out

    def render_region(self, r: dict, note: int, dur: float, vel: int, hold: float) -> np.ndarray:
        x, sr = self.sample(r["smp"])
        ratio = 2.0 ** ((note - r["root"]) / 12.0 + r["cents"] / 1200.0) * sr / self.sr
        n_out = int(dur * self.sr)
        need = int(n_out * ratio) + 8
        start, end = r["start"], r["end"]
        seg = x[start:end]
        if r["loop"] in ("sustain", "continuous") and r["le"] > r["ls"]:
            ls, le, xf = r["ls"] - start, r["le"] - start, r["xf"]
            body = seg[:le]
            loop = seg[ls:le].copy()
            if xf > 0 and ls >= xf:
                t = ((np.arange(xf) + 0.5) / xf)[:, None]
                loop[-xf:] = seg[le - xf:le] * np.cos(t * np.pi / 2) + seg[ls - xf:ls] * np.sin(t * np.pi / 2)
                body = np.concatenate([seg[:le - xf], loop[-xf:]])
            parts = [body]
            tot = len(body)
            while tot < need:
                parts.append(loop)
                tot += len(loop)
            seg = np.concatenate(parts)
        seg = seg[:need]
        if len(seg) < 4:
            return np.zeros((n_out, 2), dtype=np.float32)
        from scipy.signal import resample
        m = max(4, int(round(len(seg) / ratio)))
        y = resample(seg, m, axis=0)
        if y.shape[1] == 1:
            y = np.repeat(y, 2, axis=1)
        out = np.zeros((n_out, 2), dtype=np.float64)
        k = min(n_out, len(y))
        out[:k] = y[:k]
        g = 10 ** (r["gainDb"] / 20.0) * r["gainNorm"] * (1.0 - self.velo * (1.0 - self.vel_gain(r["velCurve"], vel)))
        p = max(-1.0, min(1.0, r["pan"] / 100.0))
        out[:, 0] *= g * math.cos((p + 1) * math.pi / 4) * math.sqrt(2)
        out[:, 1] *= g * math.sin((p + 1) * math.pi / 4) * math.sqrt(2)
        # envelope: attack ramp + release fade after `hold`
        a = max(r["env"]["a"], 0.001)
        na = min(n_out, int(a * self.sr))
        if na > 0:
            out[:na] *= np.linspace(0, 1, na)[:, None]
        if hold < dur:
            h = int(hold * self.sr)
            rel = max(r["env"]["r"], 0.05)
            t = np.arange(n_out - h) / self.sr
            out[h:] *= np.exp(-t * 6.9 / rel)[:, None]      # −60 dB over the release time
        return out

    def render(self, note: int, vel: int, dur: float = 3.0, hold: float = 2.0, artic: int = 0,
               with_release: bool = True, with_noise: bool = True) -> np.ndarray:
        out = np.zeros((int(dur * self.sr), 2))
        for r, w in self.pick(note, vel, artic, "attack"):
            out += w * self.render_region(r, note, dur, vel, hold)
        if with_noise:                                     # note-on mechanical noise (trig "on")
            for r, w in self.pick(note, vel, artic, "noise"):
                if r.get("trig") == "on":
                    out += w * self.render_region(dict(r, loop="no_loop"), note, dur, vel, dur)
        if with_release and hold < dur:
            h = int(hold * self.sr)
            for kind in ("release", "noise"):
                for r, w in self.pick(note, vel, artic, kind):
                    if kind == "noise" and (r.get("trig", "off") != "off" or not with_noise):
                        continue
                    y = w * self.render_region(dict(r, loop="no_loop"), note, dur - hold, vel, dur)
                    y *= 10 ** (-r["rtDecay"] * hold / 20.0)
                    out[h:h + len(y)] += y[: len(out) - h]
        # 30 ms fade at the very end
        f = min(len(out), int(0.03 * self.sr))
        out[-f:] *= np.linspace(1, 0, f)[:, None]
        return out
