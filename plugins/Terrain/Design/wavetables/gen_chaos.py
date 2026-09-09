"""
gen_chaos.py — TERRAIN factory bank, CHAOS / IDM category (24 tables).

Everything here is computed from first principles: iterated maps, cellular automata,
integer expressions, number theory, fractal masks. No external audio, no sampled
material, no borrowed tables. The frame axis of every table is a real trajectory
through a parameter space — a bifurcation, a CA generation count, a permutation depth.
"""
import sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # fb606

import math
import numpy as np
import wtlib
from wtlib import FRAMES, SIZE, NH, t, n_ax, PHASE


# ══ helpers ═══════════════════════════════════════════════════════════════════════════
def _fsm(a, k=3):
    """Smooth along the FRAME axis with a triangular kernel — this is what turns a
    jumpy parameter sweep into a glide instead of a crackle."""
    if k <= 1:
        return np.asarray(a, float)
    ker = np.concatenate([np.arange(1, k + 1), np.arange(k - 1, 0, -1)]).astype(float)
    ker /= ker.sum()
    pad = len(ker) // 2
    b = np.pad(np.asarray(a, float), ((pad, pad), (0, 0)), mode='edge')
    out = np.zeros_like(np.asarray(a, float))
    for i in range(len(ker)):
        out += ker[i] * b[i:i + a.shape[0]]
    return out


def _hsm(m, w=3):
    """Smooth along the HARMONIC axis — gives spectral spikes a skirt so they read as
    partials with body rather than as bare sine tones."""
    if w < 1:
        return np.asarray(m, float)
    ker = np.concatenate([np.arange(1, w + 1), np.arange(w - 1, 0, -1)]).astype(float)
    ker /= ker.sum()
    m = np.asarray(m, float)
    out = np.empty_like(m)
    for i in range(m.shape[0]):
        out[i] = np.convolve(m[i], ker, mode='same')
    return out


def _enrich(fr, alpha=0.0, nmax=1000):
    """Band-limit to nmax and tilt the magnitude spectrum by n**alpha, keeping the
    original phase. Mandatory after any time-domain construction."""
    S = np.fft.rfft(np.asarray(fr, float), axis=1)
    g = np.ones(S.shape[1])
    idx = np.arange(S.shape[1])
    g[1:] = idx[1:].astype(float) ** alpha
    g[0] = 0.0
    if nmax + 1 < len(g):
        g[nmax + 1:] = 0.0
    return np.fft.irfft(S * g, n=SIZE, axis=1)


def _tilt_frames(fr, alphas, nmaxs):
    """Per-frame spectral tilt n**alpha plus a per-frame band limit, phase untouched.
    This is how a time-domain construction gets its centroid to travel without
    changing the waveform's identity."""
    fr = np.asarray(fr, float)
    S = np.fft.rfft(fr, axis=1)
    nb = S.shape[1]
    idx = np.arange(nb, dtype=float)
    alphas = np.broadcast_to(np.asarray(alphas, float), (fr.shape[0],))
    nmaxs = np.broadcast_to(np.asarray(nmaxs, float), (fr.shape[0],))
    out = np.empty_like(S)
    for i in range(fr.shape[0]):
        g = np.zeros(nb)
        g[1:] = idx[1:] ** alphas[i]
        nm = int(nmaxs[i])
        if nm + 1 < nb:
            g[nm + 1:] = 0.0
        out[i] = S[i] * g
    return np.fft.irfft(out, n=SIZE, axis=1)


def _rownorm(m):
    pk = np.abs(m).max(axis=1, keepdims=True)
    return np.divide(m, pk, out=np.zeros_like(m), where=pk > 1e-15)


def _floor(a=0.04, p=1.15):
    """A quiet full-series body so no frame is ever a bare spike or silent."""
    return a / n_ax ** p


def _bump(center, width_st, amp=1.0):
    """Gaussian resonance in log-harmonic space; width given in semitones."""
    lw = max(width_st, 0.5) / 12.0
    return amp * np.exp(-0.5 * ((np.log2(n_ax) - math.log2(max(center, 1.0))) / lw) ** 2)


def _loopify(seg, xf=448):
    """Close a length SIZE+xf trajectory window into a seamless cycle by crossfading the
    natural continuation over the head. Endpoint de-ramping is unusable on a spiky signal
    — one endpoint landing on a spike swings the whole frame — this is stable."""
    core = np.array(seg[:SIZE], dtype=float)
    tail = np.array(seg[SIZE:SIZE + xf], dtype=float)
    w = 0.5 - 0.5 * np.cos(np.pi * np.arange(xf) / xf)      # 0 -> 1
    core[:xf] = core[:xf] * w + tail * (1.0 - w)
    return core - core.mean()


def _nodead(fr, eps=1e-9):
    """Insurance: any numerically flat frame gets a whisper of harmonic content."""
    fr = np.asarray(fr, float)
    ripple = np.sin(2 * np.pi * t) + 0.4 * np.sin(6 * np.pi * t)
    for i in range(fr.shape[0]):
        s = fr[i].std()
        if not np.isfinite(s) or s < eps:
            fr[i] = 1e-3 * ripple * (1.0 + 0.01 * i)
    return fr


def _spec(mags):
    return wtlib.finalize(wtlib.cycles_from_mags(_rownorm(np.abs(mags))))


def _smoothstep(x):
    return x * x * (3.0 - 2.0 * x)


U = np.arange(FRAMES) / (FRAMES - 1.0)          # 0..1 frame parameter


# ══ 1. TERRA BIFURCATE ════════════════════════════════════════════════════════════════
# The logistic bifurcation diagram IS the spectrum. r sweeps the period-doubling cascade;
# the attractor's density histogram becomes the harmonic magnitudes.
def terra_bifurcate():
    mags = np.zeros((FRAMES, NH))
    rs = np.minimum(2.985 + 1.015 * U ** 0.85, 3.99985)
    for i, r in enumerate(rs):
        x = 0.37
        for _ in range(900):
            x = r * x * (1.0 - x)
        acc = np.zeros(NH)
        for _ in range(1400):
            x = r * x * (1.0 - x)
            k = int((0.015 + 0.965 * x ** 0.72) * (NH - 1))
            acc[k] += 1.0
        mags[i] = acc
    mags = _hsm(mags, 5) ** 0.85          # high contrast: the diagram's density peaks
    mags = _rownorm(mags)                 # stay as sharp bright ridges, not a saw
    mags = mags * (1.0 / n_ax ** (1.00 - 0.88 * U[:, None])) + _floor(0.02, 1.65)
    return _spec(_fsm(mags, 5))


# ══ 2. TERRA CASCADE ══════════════════════════════════════════════════════════════════
# The same cascade in the TIME domain: 64 held orbit values per cycle. Period-2 is a
# buzz at harmonic 32; every doubling drops it an octave; chaos fills the whole band.
def terra_cascade():
    NS = 64
    idx = (np.arange(SIZE) * NS) // SIZE
    fr = np.zeros((FRAMES, SIZE))
    rs = np.linspace(3.062, 3.99975, FRAMES)
    for i, r in enumerate(rs):
        x = 0.5
        for _ in range(4000):
            x = r * x * (1.0 - x)
        seq = np.empty(NS)
        for j in range(NS):
            x = r * x * (1.0 - x)
            seq[j] = x
        seq = seq - seq.mean()
        pk = np.abs(seq).max()
        seq = seq / pk if pk > 1e-12 else np.sin(2 * np.pi * np.arange(NS) / NS)
        fr[i] = seq[idx]
    # order is dark and pure, chaos is bright and harsh: the tilt rides the cascade
    fr = _tilt_frames(_fsm(fr, 3), -1.60 + 2.05 * U ** 0.8, 300.0 + 340.0 * U)
    return wtlib.finalize(_nodead(fr))


# ══ 3. TERRA HENON ════════════════════════════════════════════════════════════════════
# The Henon strange attractor's x-histogram used as a FRACTAL NOTCH SET carved out of a
# saw — where the attractor is dense, the harmonic is GONE. a sweeps from fixed point
# through the cascade to the folded attractor: one notch splits, splits again, then
# shatters into a fractal Cantor-dust of holes. The exact inverse of BIFURCATE's ridges.
def terra_henon():
    mags = np.zeros((FRAMES, NH))
    a_s = 0.06 + 1.36 * U ** 1.15
    for i, a in enumerate(a_s):
        x, y = 0.05, 0.05
        for _ in range(500):
            x, y = 1.0 - a * x * x + y, 0.3 * x
            if not np.isfinite(x) or abs(x) > 6:
                x, y = 0.05, 0.05
        h = np.zeros(NH)
        for _ in range(4200):
            x, y = 1.0 - a * x * x + y, 0.3 * x
            if not np.isfinite(x) or abs(x) > 6:
                x, y = 0.05, 0.05
                continue
            k = int((x + 1.6) / 3.3 * (NH - 1))
            if 0 <= k < NH:
                h[k] += 1.0
        mags[i] = h
    mags = _hsm(mags, 4)
    dens = np.sqrt(_rownorm(mags))
    notch = 1.0 - 0.985 * dens
    body = 1.0 / n_ax ** (1.30 - 0.72 * U[:, None])
    return _spec(_fsm(notch * body + _floor(0.006, 1.9), 3))


# ══ 4. TERRA LORENZ ═══════════════════════════════════════════════════════════════════
# One continuous Lorenz trajectory with rho drifting; overlapping 2048-sample windows
# become the frames, so the morph is literally the flow moving forward in time.
def terra_lorenz():
    dt, dec, hop, warm, xf = 0.00245, 4, 66, 3000, 176
    N = warm + (FRAMES - 1) * hop + SIZE + xf + 4
    x, y, z = 1.0, 1.2, 18.0
    sig, beta = 10.0, 8.0 / 3.0
    traj = np.empty(N)
    for i in range(N):
        rho = 23.0 + 25.0 * (i / N)
        for _ in range(dec):                      # sub-stepped so Euler stays honest
            dx = sig * (y - x)
            dy = x * (rho - z) - y
            dz = x * y - beta * z
            x += dt * dx; y += dt * dy; z += dt * dz
        traj[i] = x
    fr = np.zeros((FRAMES, SIZE))
    for i in range(FRAMES):
        fr[i] = _loopify(traj[warm + i * hop: warm + i * hop + SIZE + xf], xf)
    # rho climbing = the attractor getting more violent = the growl opening up
    fr = _tilt_frames(fr, -0.35 + 1.05 * U, 120.0 + 350.0 * U ** 1.3)
    return wtlib.finalize(_nodead(fr))


# ══ 5. TERRA TENT ═════════════════════════════════════════════════════════════════════
# An asymmetric tent map used as an iterated WAVESHAPER on a ramp. Iteration depth sweeps
# 1 -> 6 (each depth roughly doubles the fold rate) while the breakpoint slides off 0.5,
# which smears the exact frequency-multiplication into a dense series.
def terra_tent():
    def tent(u, c, mu):
        v = np.where(u < c, u / max(c, 1e-3), (1.0 - u) / max(1.0 - c, 1e-3))
        return np.clip(mu * v, 0.0, 1.0)

    def fold(u, c, mu, k):
        for _ in range(k):
            u = tent(u, c, mu)
        return u

    fr = np.zeros((FRAMES, SIZE))
    ramp = t.copy()
    for i in range(FRAMES):
        s = U[i]
        depth = 0.0 + 5.6 * s
        c = 0.5 - 0.21 * s
        mu = 1.12 + 0.83 * s
        k0 = int(np.floor(depth))
        w = depth - k0
        a = fold(ramp, c, mu, k0)
        b = tent(a, c, mu)
        fr[i] = (1.0 - w) * a + w * b - 0.5
    fr = _enrich(_fsm(fr, 3), 0.45, 620)
    return wtlib.finalize(_nodead(fr))


# ══ 6. TERRA RULE30 ═══════════════════════════════════════════════════════════════════
# Wolfram rule 30, one cell per harmonic, seeded at the bottom of the series with a hard
# left wall. The chaotic light-cone expands 8 harmonics per frame: the CA growth IS the
# brightness sweep.
_T30 = np.array([0, 1, 1, 1, 1, 0, 0, 0], dtype=np.int16)
_T110 = np.array([0, 1, 1, 1, 0, 1, 1, 0], dtype=np.int16)


def terra_rule30():
    cells = np.zeros(NH, dtype=np.int16)
    cells[1] = 1
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        for _ in range(8):
            l = np.concatenate(([0], cells[:-1]))
            r = np.concatenate((cells[1:], [0]))
            cells = _T30[4 * l + 2 * cells + r]
        mags[f] = cells
    mags = _hsm(mags, 3)
    mags = mags * (1.0 / n_ax ** 0.42) + _floor(0.02, 1.35)
    return _spec(_fsm(mags, 5))


# ══ 7. TERRA RULE110 ══════════════════════════════════════════════════════════════════
# Rule 110 from a seeded random row: gliders crawling through an ether. Constant density,
# so the travel comes from a bandpass window descending through the series while the
# glider traffic rewrites what is inside it.
def terra_rule110():
    rng = np.random.default_rng(0xBEEF11)
    cells = (rng.random(NH) < 0.36).astype(np.int16)
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        for _ in range(3):
            l = np.roll(cells, 1)
            r = np.roll(cells, -1)
            cells = _T110[4 * l + 2 * cells + r]
        mags[f] = cells
    mags = _hsm(mags, 4)
    for f in range(FRAMES):
        ctr = 880.0 * (0.052 ** U[f])
        win = _bump(ctr, 7.0 + 6.5 * U[f], 1.0)
        mags[f] = mags[f] * win + 0.10 * win ** 2 + _floor(0.02, 1.4)
    return _spec(_fsm(mags, 3))


# ══ 8. TERRA BYTEBEAT ═════════════════════════════════════════════════════════════════
# The classic one-line integer expression t*(t>>k|t>>m)&mask evaluated over one cycle,
# band-limited so it is playable. 17 parameter keyframes crossfaded across the frame axis.
def terra_bytebeat():
    ii = np.arange(SIZE, dtype=np.int64)
    keys = [(3, 7, 255), (4, 7, 255), (5, 8, 255), (4, 9, 127), (6, 9, 255),
            (5, 10, 255), (7, 10, 63), (6, 11, 255), (8, 11, 255), (7, 12, 127),
            (9, 12, 255), (8, 13, 255), (10, 13, 63), (9, 14, 255), (11, 14, 255),
            (10, 15, 127), (12, 15, 255)]
    kf = np.zeros((len(keys), SIZE))
    for j, (k, m, msk) in enumerate(keys):
        v = ((ii * ((ii >> k) | (ii >> m))) & msk).astype(float)
        v = v - v.mean()
        pk = np.abs(v).max()
        kf[j] = v / pk if pk > 1e-9 else np.sin(2 * np.pi * (j + 1) * t)
    fr = np.zeros((FRAMES, SIZE))
    pos = U * (len(keys) - 1)
    for i in range(FRAMES):
        a = int(np.floor(pos[i]))
        b = min(a + 1, len(keys) - 1)
        w = _smoothstep(pos[i] - a)
        fr[i] = (1.0 - w) * kf[a] + w * kf[b]
    fr = _enrich(fr, 0.2, 300)
    return wtlib.finalize(_nodead(fr))


# ══ 9. TERRA SIERPINSKI ═══════════════════════════════════════════════════════════════
# Pascal's triangle mod 2 (Lucas: C(r,n) is odd iff n is a bit-submask of r) as the
# harmonic mask. Row r sweeps 0..1016, so the harmonic count pulses fractally between 2
# and 128 while the top of the spectrum climbs with r.
def terra_sierpinski():
    idxn = np.arange(NH)
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        r = int(round(f * 1016.0 / (FRAMES - 1)))
        mags[f] = ((idxn & r) == idxn).astype(float)
    mags = _hsm(mags, 4)
    mags = mags * (1.0 / n_ax ** (0.85 - 0.55 * U[:, None])) + _floor(0.025, 1.45)
    return _spec(_fsm(mags, 7))


# ══ 10. TERRA CANTOR ══════════════════════════════════════════════════════════════════
# Cantor middle-third removal applied to the harmonic axis, depth 0 -> 6.5 with fractional
# depths crossfaded, plus a darkening lowpass. A full sound eaten away into a fractal
# skeleton of itself.
def terra_cantor():
    u_h = (n_ax - 1) / (NH - 1.0)

    def cantor_keep(depth):
        keep = np.ones(NH)
        x = u_h.copy()
        for d in range(depth):
            keep *= ~((x > 1.0 / 3.0) & (x < 2.0 / 3.0))
            x = np.where(x >= 2.0 / 3.0, (x - 2.0 / 3.0) * 3.0, x * 3.0)
        return keep

    tiers = [cantor_keep(d) for d in range(8)]
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        d = 6.5 * U[f]
        a = int(np.floor(d))
        b = min(a + 1, 7)
        w = _smoothstep(d - a)
        keep = (1.0 - w) * tiers[a] + w * tiers[b]
        fc = 1010.0 * (0.075 ** U[f])
        lp = 1.0 / (1.0 + (n_ax / fc) ** 3.2)
        mags[f] = (0.07 + 0.93 * keep) * lp * (1.0 / n_ax ** 0.32)
    mags = _hsm(mags, 3)
    return _spec(_fsm(mags, 3))


# ══ 11. TERRA BITFLIP ═════════════════════════════════════════════════════════════════
# Bit-reversal permutation of a saw's harmonic amplitudes. The reversal depth sweeps
# 0 -> 10 bits, so the loudest partials migrate from the bottom of the series to indices
# scattered across the top. The multiset of magnitudes never changes — only where they sit.
def terra_bitflip():
    def revperm(bits):
        i = np.arange(NH)
        low = i & ((1 << bits) - 1)
        r = np.zeros(NH, dtype=np.int64)
        for b in range(bits):
            r |= ((low >> b) & 1) << (bits - 1 - b)
        return (i & ~((1 << bits) - 1)) | r

    base = 1.0 / n_ax ** 1.25
    perms = [revperm(b) for b in range(11)]
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        d = 10.0 * U[f] ** 0.9
        a = int(np.floor(d))
        b = min(a + 1, 10)
        w = _smoothstep(d - a)
        mags[f] = (1.0 - w) * base[perms[a]] + w * base[perms[b]]
    return _spec(_fsm(mags, 3) + _floor(0.008, 1.6))


# ══ 12. TERRA GRAYCODE ════════════════════════════════════════════════════════════════
# The Gray-code map g(i)=i^(i>>1) iterated k times AND rotated k bits inside a 10-bit
# window, permuting a HOLLOW (odd-harmonic) square spectrum. Gray code alone preserves the
# top bit and would not move any energy; the bit rotation is what drags the loud low
# partials up into the top of the series. Woody where BITFLIP is glassy.
def terra_graycode():
    i0 = np.arange(NH, dtype=np.int64)

    def rot(v, k):
        k %= 10
        return ((v << k) | (v >> (10 - k))) & 1023

    perms = []
    cur = i0.copy()
    for k in range(11):
        perms.append(rot(cur, k))
        cur = cur ^ (cur >> 1)
    base = np.where(n_ax % 2 == 1, 1.0 / n_ax ** 1.15, 0.0)
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        d = 10.0 * U[f]
        a = int(np.floor(d))
        b = min(a + 1, 10)
        w = _smoothstep(d - a)
        mags[f] = (1.0 - w) * base[perms[a]] + w * base[perms[b]]
    mags = _hsm(mags, 2)
    return _spec(_fsm(mags, 3) + _floor(0.01, 1.5))


# ══ 13. TERRA PRIMEGAP ════════════════════════════════════════════════════════════════
# Harmonic n is admitted only if its smallest prime factor is >= q, and primes are
# weighted by the gap that follows them. q falls from 97 to 2, so the series densifies
# from a thin prime lattice down through 5-smooth, 3-smooth, all integers.
def terra_primegap():
    N = NH + 8
    spf = np.zeros(N + 1, dtype=np.int64)
    for p in range(2, N + 1):
        if spf[p] == 0:
            for k in range(p, N + 1, p):
                if spf[k] == 0:
                    spf[k] = p
    spf[1] = 10 ** 9
    isp = np.array([spf[n] == n for n in range(N + 1)])
    primes = [n for n in range(2, N + 1) if isp[n]]
    gap = np.ones(N + 1)
    for a, b in zip(primes, primes[1:]):
        gap[a] = b - a
    gap[primes[-1]] = 2.0
    spf_n = spf[1:NH + 1].astype(float)
    gap_n = gap[1:NH + 1]
    qs = 97.0 * (2.0 / 97.0) ** (U ** 0.7)
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        adm = 1.0 / (1.0 + np.exp(-(spf_n - qs[f]) * 0.9))
        w = (0.25 + 0.75 * np.minimum(gap_n, 18.0) / 8.0)
        # thin bright lattice of high primes -> dense dark full series
        mags[f] = (0.02 + 0.98 * adm) * w / n_ax ** (0.42 + 1.20 * U[f])
    mags = _hsm(mags, 2)
    return _spec(_fsm(mags, 3))


# ══ 14. TERRA PIDIGITS ════════════════════════════════════════════════════════════════
# Digits of pi (computed here with Machin's formula in integer arithmetic) are the
# harmonic amplitudes. The read window scrolls 8 digits per frame, so the whole
# pseudo-random comb slides DOWN the series inside a fixed body, and the digit exponent
# sweeps the statistics from flat-dense to spiky-sparse.
def _pi_digits(nd):
    prec = nd + 24
    scale = 10 ** prec

    def atan_inv(x):
        total = term = scale // x
        x2 = x * x
        n = 1
        sign = -1
        while term:
            term //= x2
            cur = term // (2 * n + 1)
            if cur == 0:
                break
            total += sign * cur
            sign = -sign
            n += 1
        return total

    pi = 16 * atan_inv(5) - 4 * atan_inv(239)
    s = str(pi)
    return np.array([int(c) for c in s[1:nd + 1]], dtype=float)


def terra_pidigits():
    need = NH + 8 * FRAMES + 16
    d = _pi_digits(need)
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        s0 = 8 * f
        raw = (d[s0:s0 + NH] + 0.6) / 9.6
        g = 0.45 + 3.1 * U[f] ** 1.2
        tilt = 0.55 + 0.75 * U[f]
        mags[f] = raw ** g / n_ax ** tilt
    mags = _hsm(mags, 2)
    return _spec(_fsm(mags, 3) + _floor(0.012, 1.5))


# ══ 15. TERRA DIFFUSE ═════════════════════════════════════════════════════════════════
# A phase demonstration: frame 0 is all-cosine (a single impulsive spike, crest factor
# through the roof), frame 127 is fully scrambled with wtlib's phase set plus a quadratic
# chirp (a smooth diffuse wash). Underneath, a narrow resonance CLIMBS over a fixed bed,
# so the shape of the spectrum is a rising peak, not a closing filter.
def terra_diffuse():
    out = np.zeros((FRAMES, SIZE))
    bed0 = 1.0 / n_ax ** 1.25
    for i in range(FRAMES):
        s = U[i]
        # a broad bed at the impulsive end so frame 0 collapses to a real spike
        peak = _bump(13.0 * (54.0 ** s), 5.0, 1.0)
        m = ((1.0 - 0.80 * s) * bed0 + peak) * (1.0 / (1.0 + (n_ax / 960.0) ** 8.0))
        ph = (s ** 0.8) * PHASE + 0.85 * s * (n_ax / NH) ** 2 * 2 * np.pi
        spec = np.zeros(SIZE // 2 + 1, dtype=complex)
        spec[1:NH + 1] = m * np.exp(1j * ph)
        out[i] = np.fft.irfft(spec, n=SIZE)
    return wtlib.finalize(_nodead(out))


# ══ 16. TERRA DREDGE ══════════════════════════════════════════════════════════════════
# The 30-second horror drone. Everything moves at a glacial rate: a formant crawls from
# harmonic 340 down to 34, a beating pair structure (n against n+1) slowly detunes, and
# the low odd harmonics swell. Heavy frame smoothing — no event ever arrives.
def terra_dredge():
    mags = np.zeros((FRAMES, NH))
    beat = np.where(n_ax % 2 == 0, 1.0, 0.0)
    for f in range(FRAMES):
        s = U[f]
        body = 1.0 / n_ax ** (1.30 - 0.42 * s)
        low = (_bump(1.0, 9.0, 1.0) + 0.72 * _bump(3.0, 7.0, 1.0)
               + 0.5 * _bump(5.0, 6.0, 1.0)) * (0.55 + 0.6 * s)
        crawl = _bump(340.0 * (0.10 ** s), 9.0 + 7.0 * s, 0.85)
        crawl2 = _bump(97.0 * (0.42 ** s), 5.5, 0.4 * (0.3 + s))
        rough = 1.0 + 0.55 * np.cos(2 * np.pi * (0.5 * n_ax + 6.0 * s)) * beat
        mags[f] = (body * (0.35 + 0.65 * rough * 0.5) + low + crawl + crawl2) * (
            1.0 / (1.0 + (n_ax / (620.0 * (0.16 ** s))) ** 2.6))
    mags = _hsm(mags, 2)
    return _spec(_fsm(mags, 11) + _floor(0.02, 1.5))


# ══ 17. TERRA REVENANT ════════════════════════════════════════════════════════════════
# Four formants start in a plausible human vowel and slide to positions no throat can
# make — F1 collapses toward the fundamental while F3/F4 climb past 800 — with a
# roughness modulation that turns the choir into a rasp.
def terra_revenant():
    F0 = np.array([6.0, 11.0, 24.0, 31.0])
    F1 = np.array([1.8, 74.0, 330.0, 790.0])
    A0 = np.array([1.0, 0.72, 0.42, 0.28])
    A1 = np.array([0.55, 0.95, 0.85, 0.62])
    W0 = np.array([3.0, 3.6, 4.5, 5.5])
    W1 = np.array([7.0, 5.0, 6.5, 8.5])
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        s = _smoothstep(U[f])
        m = np.zeros(NH)
        for j in range(4):
            c = F0[j] * (F1[j] / F0[j]) ** s
            m += _bump(c, W0[j] + (W1[j] - W0[j]) * s, A0[j] + (A1[j] - A0[j]) * s)
        rough = 1.0 + 0.62 * s * np.cos(2 * np.pi * n_ax / (5.0 + 9.0 * s) + 1.1)
        m = m * rough + 0.10 / n_ax ** (1.6 - 0.7 * s)
        mags[f] = m
    mags = _hsm(mags, 2)
    return _spec(_fsm(mags, 5))


# ══ 18. TERRA GLITCHCOMB ══════════════════════════════════════════════════════════════
# A comb filter whose tooth spacing is driven frame-by-frame by a chaotic logistic
# sequence (smoothed so it glides), while the underlying tilt brightens. Metallic,
# rhythmic, never repeats the same tooth pattern twice.
def terra_glitchcomb():
    x = 0.201
    seq = np.empty(FRAMES)
    for i in range(FRAMES):
        for _ in range(3):
            x = 3.9012 * x * (1.0 - x)
        seq[i] = x
    seq = _fsm(seq.reshape(-1, 1), 7).ravel()
    d = 2.2 + 34.0 * (0.25 + 0.75 * seq) * (0.35 + 0.65 * U)
    p = 3.2 - 2.4 * U
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        comb = np.abs(np.cos(np.pi * n_ax / d[f])) ** p[f]
        tilt = 1.0 / n_ax ** (1.45 - 0.72 * U[f])
        mags[f] = (0.06 + 0.94 * comb) * tilt
    return _spec(_fsm(mags, 3) + _floor(0.01, 1.6))


# ══ 19. TERRA SHATTER ═════════════════════════════════════════════════════════════════
# Seeded stochastic spectra whose STATISTICS sweep: 13 keyframes interpolated with a
# smoothstep, going from very sparse and very spiky (a handful of glass shards) to dense,
# flat and noisy (a wall of static).
def terra_shatter():
    rng = np.random.default_rng(0x5A77E7)
    K = 13
    kf = np.zeros((K, NH))
    for j in range(K):
        s = j / (K - 1.0)
        g = 6.5 * (1.0 - s) + 0.55 * s
        dens = 0.035 + 0.9 * s ** 0.75
        r = rng.random(NH)
        gate = (rng.random(NH) < dens).astype(float)
        amp = r ** g
        tilt = 1.0 / n_ax ** (1.05 - 0.55 * s)
        kf[j] = (amp * gate) * tilt + 0.02 * tilt
    kf = _hsm(kf, 3)
    mags = np.zeros((FRAMES, NH))
    pos = U * (K - 1)
    for f in range(FRAMES):
        a = int(np.floor(pos[f]))
        b = min(a + 1, K - 1)
        w = _smoothstep(pos[f] - a)
        mags[f] = (1.0 - w) * kf[a] + w * kf[b]
    return _spec(_fsm(mags, 3))


# ══ 20. TERRA DEVIL ═══════════════════════════════════════════════════════════════════
# The sine circle map's devil's staircase. Omega sweeps but the rotation number LOCKS on
# rationals, so the perceived pitch climbs in steps and goes broadband where it unlocks.
def terra_devil():
    NS = 256
    idx = (np.arange(SIZE) * NS) // SIZE
    K = 1.06
    oms = np.linspace(0.168, 0.523, FRAMES)
    fr = np.zeros((FRAMES, SIZE))
    for i, O in enumerate(oms):
        th = 0.113
        for _ in range(400):
            th = th + O - (K / (2 * np.pi)) * math.sin(2 * np.pi * th)
        # advance to the next wrap so the saw resets at sample 0 in EVERY frame;
        # without this the whole waveform slides between frames and the morph clicks
        base, guard = math.floor(th), 0
        while math.floor(th) == base and guard < 8000:
            th = th + O - (K / (2 * np.pi)) * math.sin(2 * np.pi * th)
            guard += 1
        seq = np.empty(NS)
        for j in range(NS):
            th = th + O - (K / (2 * np.pi)) * math.sin(2 * np.pi * th)
            seq[j] = th % 1.0
        w = seq - 0.5
        w = w - w.mean()
        pk = np.abs(w).max()
        fr[i] = (w / pk if pk > 1e-9 else np.sin(2 * np.pi * np.arange(NS) / NS))[idx]
    fr = _enrich(_fsm(fr, 5), 0.18, 700)
    return wtlib.finalize(_nodead(fr))


# ══ 21. TERRA XORFOLD ═════════════════════════════════════════════════════════════════
# A 16-bit ramp XORed against shifted and multiplied copies of itself — a purely integer
# wavefolder. Shift depth crossfades across 15 keyframes; band-limited hard so the
# destruction stays playable.
def terra_xorfold():
    q = (t * 65535.0).astype(np.int64)
    keys = [(1, 3), (2, 5), (3, 7), (4, 9), (5, 11), (6, 13), (7, 17), (8, 19),
            (9, 23), (10, 29), (11, 31), (12, 37), (13, 41), (14, 43), (15, 47)]
    kf = np.zeros((len(keys), SIZE))
    for j, (sh, mul) in enumerate(keys):
        v = (q ^ (q >> sh) ^ ((q * mul) & 0xFFFF)) & 0xFFFF
        v = v.astype(float) - 32767.5
        pk = np.abs(v).max()
        kf[j] = v / pk if pk > 1e-9 else np.sin(2 * np.pi * (j + 2) * t)
    fr = np.zeros((FRAMES, SIZE))
    pos = U * (len(keys) - 1)
    for f in range(FRAMES):
        a = int(np.floor(pos[f]))
        b = min(a + 1, len(keys) - 1)
        w = _smoothstep(pos[f] - a)
        fr[f] = (1.0 - w) * kf[a] + w * kf[b]
    fr = _tilt_frames(fr, -0.70 + 1.45 * U, 200.0 + 360.0 * U)
    return wtlib.finalize(_nodead(fr))


# ══ 22. TERRA COLLATZ ═════════════════════════════════════════════════════════════════
# Collatz stopping time of n as the amplitude of harmonic n. Powers of two stop almost
# immediately, so the spectrum carries deep notches at 2, 4, 8, 16 ... — an octave-notched
# alien organ. The contrast exponent and a rising bandpass do the travelling.
def terra_collatz():
    N = NH
    c = np.zeros(N + 1, dtype=np.int64)
    for n in range(2, N + 1):
        x, s = n, 0
        while x != 1:
            x = x // 2 if x % 2 == 0 else 3 * x + 1
            s += 1
            if x <= N and c[x] > 0:
                s += c[x]
                break
        c[n] = s
    v = c[1:N + 1].astype(float)
    v = (v - v.min()) / (v.max() - v.min())
    mags = np.zeros((FRAMES, NH))
    for f in range(FRAMES):
        s = U[f]
        g = 2.6 - 2.15 * s
        ctr = 22.0 * (29.0 ** s)
        win = _bump(ctr, 9.5 + 8.0 * s, 1.0)
        mags[f] = (0.10 + 0.90 * v ** g) * win * (1.0 / n_ax ** 0.35)
    mags = _hsm(mags, 2)
    return _spec(_fsm(mags, 3) + _floor(0.012, 1.55))


# ══ 23. TERRA CHIRIKOV ════════════════════════════════════════════════════════════════
# The Chirikov standard map. 2048 particles are launched cold; as K rises the KAM tori
# break and momentum DIFFUSES. The SIGNED momentum histogram is the spectrum, centred at
# harmonic 512, so a near-pure mid tone blooms symmetrically outward into a chaotic cloud
# and darkens as it goes. Sci-fi transition.
def terra_chirikov():
    rng = np.random.default_rng(0xC417C0)
    NP = 2048
    th = rng.random(NP)
    p = rng.normal(0.0, 0.006, NP)
    mags = np.zeros((FRAMES, NH))
    Ks = 0.55 + 4.6 * U ** 1.25
    edges = np.linspace(0.0, 1.0, NH + 1)
    for f in range(FRAMES):
        K = Ks[f]
        for _ in range(6):
            p = p + (K / (2 * np.pi)) * np.sin(2 * np.pi * th)
            th = (th + p) % 1.0
        a = np.clip(0.5 + p / 7.0, 0.0, 0.9999)
        h, _ = np.histogram(a, bins=edges)
        mags[f] = h.astype(float)
    mags = _hsm(mags, 9) ** 0.55
    mags = _rownorm(mags) * (1.0 / n_ax ** (0.10 + 1.05 * U[:, None])) + _floor(0.02, 1.5)
    return _spec(_fsm(mags, 5))


# ══ 24. TERRA ROSSLER ═════════════════════════════════════════════════════════════════
# Rossler's z coordinate: flat for most of the orbit then a violent spike. c drifts
# through the period-doubling cascade while overlapping windows become the frames, so the
# spike pattern doubles, quadruples, then goes irregular. Impulsive and organic at once.
def terra_rossler():
    dt, dec, hop, warm, xf = 0.0030, 9, 58, 2500, 168
    N = warm + (FRAMES - 1) * hop + SIZE + xf + 4
    x, y, z = 0.6, 1.4, 0.2
    a, b = 0.2, 0.2
    traj = np.empty(N)
    for i in range(N):
        c = 2.6 + 6.6 * (i / N)
        for _ in range(dec):                      # ~8 orbits per cycle: the z spikes end
            dx = -y - z                            # up narrow, which is where the
            dy = x + a * y                         # harmonics come from
            dz = b + z * (x - c)
            x += dt * dx; y += dt * dy; z += dt * dz
            if not np.isfinite(z) or abs(z) > 1e4:
                x, y, z = 0.6, 1.4, 0.2
        traj[i] = z
    fr = np.zeros((FRAMES, SIZE))
    traj = np.maximum(traj, 0.0) ** 0.75      # tame the range, KEEP the spikes
    for i in range(FRAMES):
        fr[i] = _loopify(traj[warm + i * hop: warm + i * hop + SIZE + xf], xf)
    # c climbing through the cascade opens the band and sharpens the spikes
    fr = _tilt_frames(_fsm(fr, 3), -0.15 + 1.75 * U ** 0.85, 290.0 + 650.0 * U)
    return wtlib.finalize(_nodead(fr))


TABLES = [
    ("TERRA BIFURCATE",  terra_bifurcate),
    ("TERRA CASCADE",    terra_cascade),
    ("TERRA HENON",      terra_henon),
    ("TERRA LORENZ",     terra_lorenz),
    ("TERRA TENT",       terra_tent),
    ("TERRA RULE30",     terra_rule30),
    ("TERRA RULE110",    terra_rule110),
    ("TERRA BYTEBEAT",   terra_bytebeat),
    ("TERRA SIERPINSKI", terra_sierpinski),
    ("TERRA CANTOR",     terra_cantor),
    ("TERRA BITFLIP",    terra_bitflip),
    ("TERRA GRAYCODE",   terra_graycode),
    ("TERRA PRIMEGAP",   terra_primegap),
    ("TERRA PIDIGITS",   terra_pidigits),
    ("TERRA DIFFUSE",    terra_diffuse),
    ("TERRA DREDGE",     terra_dredge),
    ("TERRA REVENANT",   terra_revenant),
    ("TERRA GLITCHCOMB", terra_glitchcomb),
    ("TERRA SHATTER",    terra_shatter),
    ("TERRA DEVIL",      terra_devil),
    ("TERRA XORFOLD",    terra_xorfold),
    ("TERRA COLLATZ",    terra_collatz),
    ("TERRA CHIRIKOV",   terra_chirikov),
    ("TERRA ROSSLER",    terra_rossler),
]


# ══════════════════════════════════════════════════════════════════════════════════════
# fb606 — THE MERGED TEN. The bank shipped as eight generated folders; the browser showed a
# DIFFERENT set for its built-ins, so the same sound had two names depending on where you
# looked. The two sets are now ONE ten-folder taxonomy, and every module declares which of
# the ten each of its tables belongs to. gate.py reads CATEGORY and never guesses from the
# module name — a table with no entry here is a hard error, not a silent "GEN_WHATEVER".
# ══════════════════════════════════════════════════════════════════════════════════════

# fb606 — CHAOS ABSORBS THE OLD 'Experimental' CATEGORY. Maps, automata and integer
# arithmetic were the whole of what Experimental meant, and 'Chaos' says it out loud.
CATEGORY = {nm: "Chaos" for nm, _ in TABLES}


if __name__ == "__main__":
    fps, bad = {}, 0
    for name, fn in TABLES:
        frames = fn()
        ok, rep = wtlib.selfcheck(name, frames)
        print(rep)
        if not ok:
            bad += 1
        fps[name] = wtlib.fingerprint(frames)
    names = list(fps)
    worst = (1e9, "", "")
    pairs = []
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            d = wtlib.distance(fps[names[i]], fps[names[j]])
            pairs.append((d, names[i], names[j]))
            if d < worst[0]:
                worst = (d, names[i], names[j])
    pairs.sort()
    print("--- closest pairs ---")
    for d, a, b in pairs[:6]:
        print(f"    {d:6.2f} dB   {a} / {b}")
    print(f"MIN PAIRWISE DISTANCE {worst[0]:.2f} dB  ({worst[1]} vs {worst[2]})   tables below bar: {bad}")
