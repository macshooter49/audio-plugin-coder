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


# --------------------------------------------------------------------------------------------- renderer
class Renderer:
    """Offline mix of the regions a note would trigger. Linear-phase FFT resampling per region."""

    def __init__(self, inst_dir: str, out_sr: int = 48000):
        import json
        import soundfile as sf
        self.sf = sf
        self.dir = inst_dir
        with open(os.path.join(inst_dir, "map.json")) as f:
            self.map = json.load(f)
        self.sr = out_sr
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
        g = 10 ** (r["gainDb"] / 20.0) * r["gainNorm"] * self.vel_gain(r["velCurve"], vel)
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
               with_release: bool = True) -> np.ndarray:
        out = np.zeros((int(dur * self.sr), 2))
        for r, w in self.pick(note, vel, artic, "attack"):
            out += w * self.render_region(r, note, dur, vel, hold)
        if with_release and hold < dur:
            h = int(hold * self.sr)
            for kind in ("release", "noise"):
                for r, w in self.pick(note, vel, artic, kind):
                    y = w * self.render_region(dict(r, loop="no_loop"), note, dur - hold, vel, dur)
                    y *= 10 ** (-r["rtDecay"] * hold / 20.0)
                    out[h:h + len(y)] += y[: len(out) - h]
        # 30 ms fade at the very end
        f = min(len(out), int(0.03 * self.sr))
        out[-f:] *= np.linspace(1, 0, f)[:, None]
        return out
