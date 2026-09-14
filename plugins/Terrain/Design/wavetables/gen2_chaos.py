"""
gen2_chaos.py — TERRAIN factory bank, CHAOS category, the 2026-09 expansion (candidates for gate500.py).

Every table is computed from first principles: flows integrated here (Duffing, Chua, Lorenz,
Hindmarsh-Rose, Mackey-Glass, three bodies, billiards), iterated maps (Ikeda, Clifford, de Jong,
Hopalong, the standard map, Julia and Newton fractals, Lyapunov space), spatially extended systems
whose STATE is the cycle (Kuramoto-Sivashinsky, Gray-Scott, Ginzburg-Landau, Burgers, coupled map
lattices, Ising, Life, sandpiles, earthquake faults) and seeded digital rot. No external audio, no
sampled material, no borrowed tables.

THE JOURNEY RULE: frame 0 is playable (usually a single clean cycle of the system in its periodic
regime), the frame axis drives the control parameter THROUGH its bifurcations or runs the system's
own clock forward, and the last frame is the system at its most violent.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import math
import numpy as np
import wtlib
from wtlib import FRAMES, SIZE, NH, t, n_ax, PHASE

F = FRAMES
U = np.arange(F) / (F - 1.0)              # 0..1 frame parameter
OSN = SIZE * 8                            # 8x oversampled cycle for time-domain nonlinearities


# ══ helpers ═══════════════════════════════════════════════════════════════════════════════════
def _ss(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def _rs(X, nmax=1000):
    """Fourier-resample periodic rows of ANY length to 2048 samples, band-limited to nmax harmonics."""
    X = np.atleast_2d(np.asarray(X, float))
    M = X.shape[1]
    S = np.fft.rfft(X, axis=1)
    k = min(nmax, (M - 1) // 2)
    out = np.zeros((X.shape[0], SIZE // 2 + 1), complex)
    out[:, 1:k + 1] = S[:, 1:k + 1]
    return np.fft.irfft(out, n=SIZE, axis=1) * (SIZE / M)


def _up(fr, M=OSN):
    """Fourier-upsample (F, n) periodic rows to (F, M)."""
    fr = np.atleast_2d(np.asarray(fr, float))
    S = np.fft.rfft(fr, axis=1)
    out = np.zeros((fr.shape[0], M // 2 + 1), complex)
    n = min(S.shape[1] - 1, M // 2)
    out[:, :n] = S[:, :n]
    return np.fft.irfft(out, n=M, axis=1) * (M / fr.shape[1])


def _shape(fr, fn, nmax=1000):
    """Apply a memoryless nonlinearity alias-free: upsample 8x, shape, band-limit back."""
    return _rs(fn(_up(fr)), nmax)


def _tilt(fr, alpha, nmax=1000):
    """Per-frame spectral tilt n**alpha (+ band limit), phase untouched."""
    fr = np.atleast_2d(np.asarray(fr, float))
    S = np.fft.rfft(fr, axis=1)
    idx = np.arange(S.shape[1], dtype=float)
    al = np.broadcast_to(np.asarray(alpha, float), (fr.shape[0],))
    nm = np.broadcast_to(np.asarray(nmax, float), (fr.shape[0],))
    g = np.zeros_like(S, dtype=float)
    g[:, 1:] = idx[None, 1:] ** al[:, None]
    g[idx[None, :] > nm[:, None]] = 0.0
    return np.fft.irfft(S * g, n=fr.shape[1], axis=1)


def _blur(fr, nc):
    """Per-frame Gaussian low-pass at nc harmonics (the 'focus' of a fractal coming into view)."""
    fr = np.atleast_2d(np.asarray(fr, float))
    S = np.fft.rfft(fr, axis=1)
    k = np.arange(S.shape[1], dtype=float)
    nc = np.broadcast_to(np.asarray(nc, float), (fr.shape[0],))
    return np.fft.irfft(S * np.exp(-(k[None, :] / nc[:, None]) ** 2), n=fr.shape[1], axis=1)


def _fsm(a, k=3):
    """Triangular smoothing along the FRAME axis."""
    a = np.asarray(a, float)
    if k <= 1:
        return a
    ker = np.concatenate([np.arange(1, k + 1), np.arange(k - 1, 0, -1)]).astype(float)
    ker /= ker.sum()
    pad = len(ker) // 2
    b = np.pad(a, ((pad, pad),) + ((0, 0),) * (a.ndim - 1), mode='edge')
    out = np.zeros_like(a)
    for i in range(len(ker)):
        out += ker[i] * b[i:i + a.shape[0]]
    return out


def _hsm(m, w=3):
    """Triangular smoothing along the harmonic/sample axis (circular)."""
    m = np.atleast_2d(np.asarray(m, float))
    if w <= 1:
        return m
    ker = np.concatenate([np.arange(1, w + 1), np.arange(w - 1, 0, -1)]).astype(float)
    ker /= ker.sum()
    out = np.zeros_like(m)
    c = len(ker) // 2
    for i, k in enumerate(ker):
        out += k * np.roll(m, i - c, axis=1)
    return out


def _rownorm(m):
    m = np.asarray(m, float)
    pk = np.abs(m).max(axis=1, keepdims=True)
    return np.divide(m, pk, out=np.zeros_like(m), where=pk > 1e-15)


def _close(seg, L, xf=None):
    """Close a trajectory window seg[:L] into a seamless cycle by crossfading its natural
    continuation seg[L:L+xf] over the head."""
    seg = np.asarray(seg, float)
    if xf is None:
        xf = max(4, int(0.08 * L))
    xf = int(min(xf, len(seg) - L, L // 2))
    core = seg[:L].copy()
    if xf > 0:
        w = 0.5 - 0.5 * np.cos(np.pi * np.arange(xf) / xf)
        core[:xf] = core[:xf] * w + seg[L:L + xf] * (1.0 - w)
    return core


def _nodead(fr, eps=1e-10):
    fr = np.array(fr, float)
    ripple = np.sin(2 * np.pi * t) + 0.3 * np.sin(6 * np.pi * t)
    for i in range(fr.shape[0]):
        s = fr[i].std()
        if not np.isfinite(s) or s < eps:
            fr[i] = 1e-3 * ripple
    return fr


def _seamfix(fr):
    """Rotate EVERY frame by the same amount so the wrap lands where the table is quietest
    (magnitudes, metrics and fingerprint are rotation-invariant)."""
    x = np.asarray(fr, float)
    d = np.abs(x - np.roll(x, 1, axis=1))
    k = max(1, int(round(0.01 * x.shape[1])))
    typ = np.partition(d, x.shape[1] - k, axis=1)[:, -k:].mean(axis=1)
    worst = (d / np.maximum(typ[:, None], 1e-12)).max(axis=0)
    r = int(np.argmin(worst))
    return np.roll(x, -r, axis=1)


def _fin(fr):
    fr = np.nan_to_num(np.asarray(fr, float), nan=0.0, posinf=0.0, neginf=0.0)
    fr = fr - fr.mean(axis=1, keepdims=True)
    return wtlib.finalize(_seamfix(_nodead(fr)))


def _spec(mags, phase=None):
    return wtlib.finalize(_seamfix(wtlib.cycles_from_mags(_rownorm(np.abs(mags)), phase)))


def _rk4(f, s, dt, n, rec=None, every=1):
    """Vectorised RK4: s is (dim, F). rec(s) -> (F,) recorded every `every` steps."""
    h2, h6 = 0.5 * dt, dt / 6.0
    out = None
    j = 0
    for i in range(n):
        k1 = f(s); k2 = f(s + h2 * k1); k3 = f(s + h2 * k2); k4 = f(s + dt * k3)
        s = s + h6 * (k1 + 2.0 * (k2 + k3) + k4)
        if rec is not None and (i + 1) % every == 0:
            r = rec(s)
            if out is None:
                out = np.empty((n // every, len(r)))
            if j < out.shape[0]:
                out[j] = r; j += 1
    return s, (out.T if rec is not None else None)


def _upx(sig, hyst=0.08):
    """Upward mean crossings with hysteresis (the signal must dip below -hyst*std before the next)."""
    s = sig - sig.mean()
    h = hyst * s.std()
    cand = np.where((s[:-1] < 0) & (s[1:] >= 0))[0] + 1
    if len(cand) == 0:
        return cand
    low = np.where(s < -h, np.arange(len(s)), -1)
    last_low = np.maximum.accumulate(low)
    keep, prev = [], -1
    for c in cand:
        if last_low[c - 1] > prev:
            keep.append(c); prev = c
    return np.array(keep, dtype=int)


def _cycles(rec, K, xf_frac=0.06, start=1, fallback=None):
    """Per frame: a window of K[i] consecutive cycles (upward mean crossings) of rec[i], closed
    into a loop and resampled to 2048. The periodic regime therefore loops PERFECTLY."""
    out = np.zeros((rec.shape[0], SIZE))
    for i in range(rec.shape[0]):
        x = rec[i]
        c = _upx(x)
        k = int(max(1, K[i]))
        if len(c) >= start + k + 1:
            a, b = c[start], c[start + k]
        else:
            L = int(fallback[i]) if fallback is not None else len(x) // 2
            a, b = 0, min(L, len(x) - 2)
        L = b - a
        xf = max(4, int(xf_frac * L))
        seg = x[a:min(len(x), b + xf)]
        out[i] = _rs(_close(seg, L, xf)[None])[0]
    return out


def _drive(fr, gain):
    """Per-frame tanh drive, alias-free."""
    fr = np.asarray(fr, float)
    fr = fr / np.maximum(np.abs(fr).max(axis=1, keepdims=True), 1e-12)
    g = np.broadcast_to(np.asarray(gain, float), (fr.shape[0],))[:, None]
    return _shape(fr, lambda x: np.tanh(g * x))


def _crest(y):
    y = y - y.mean(axis=1, keepdims=True)
    return np.abs(y).max(axis=1) / np.maximum(np.sqrt((y * y).mean(axis=1)), 1e-12)


def _drive_crest(pre, target, g0, iters=4):
    """_drive with a per-frame gain raised (never below g0) until the band-limited frame's crest
    (peak / RMS) is <= target: a soft limiter that only engages on spiky frames. Every frame is
    peak-normalised by finalize, so a spiky frame plays quiet; this lifts its body instead."""
    pre = np.asarray(pre, float)
    x = pre / np.maximum(np.abs(pre).max(axis=1, keepdims=True), 1e-12)
    g0 = np.broadcast_to(np.asarray(g0, float), (pre.shape[0],)).copy()

    def cr(xi, g):
        y = np.tanh(g * xi); y = y - y.mean()
        return np.abs(y).max() / max(np.sqrt((y * y).mean()), 1e-12)
    g = g0.copy()
    for i in range(pre.shape[0]):                  # bisection on the 2048-sample frame
        if cr(x[i], g0[i]) <= target:
            continue
        lo, hi = g0[i], g0[i] * 2
        while cr(x[i], hi) > target and hi < 400:
            lo, hi = hi, hi * 2
        for _ in range(30):
            m = 0.5 * (lo + hi)
            if cr(x[i], m) > target:
                lo = m
            else:
                hi = m
        g[i] = hi
    g = np.maximum(g0, _fsm(g, 2))
    g = np.maximum(g, _fsm(g, 2))
    for _ in range(iters):                         # correct for the band limit's overshoot
        y = _drive(pre, g)
        c = _crest(y)
        over = c > target * 1.02
        if not over.any():
            break
        g = np.where(over, g * (c / target) ** 2.5, g)
        g = np.maximum(g, _fsm(g, 2))
    return _drive(pre, g), g


def _saw_os(M=OSN, h=1):
    ph = (np.arange(M) / M * h) % 1.0
    return 1.0 - 2.0 * ph


# ══ FLOWS ═════════════════════════════════════════════════════════════════════════════════════
def duffing_swing():
    """DUFFING SWING — forced double-well oscillator x'' + 0.3x' - x + x^3 = g cos(1.2s), RK4.
    Frame axis: drive g 0.20 -> 0.56, i.e. period-1 swing -> period 2 -> 4 -> chaos -> the big
    cross-well swing. One frame = exactly two drive periods, so the period-2 regime loops perfectly
    and the sub-octave is BORN when the orbit doubles. Played through a tanh overdrive whose gain
    climbs with the drive: a rounded sway that ends as a clipped, cross-well chaotic slab."""
    w = 1.2; P = 2 * np.pi / w
    g = 0.20 + 0.36 * U ** 0.9

    def f(s):
        x, v, c = s
        return np.array([v, -0.3 * v + x - x ** 3 + g * np.cos(w * c), np.ones_like(c)])
    s = np.array([np.ones(F), np.zeros(F), np.zeros(F)])
    s, _ = _rk4(f, s, P / 128, 128 * 70)
    s[2] = 0.0
    s, rec = _rk4(f, s, P / 1024, 2048 + 200, rec=lambda s: s[0].copy())
    fr = np.array([_rs(_close(rec[i], 2048, 200)[None])[0] for i in range(F)])
    fr = _drive(fr, 1.0 + 30.0 * U ** 1.4)
    return _fin(_tilt(fr, 0.35 * U, 1000))


def chua_double_scroll():
    """CHUA DOUBLE SCROLL — Chua's circuit (piecewise-linear diode, beta 28), RK4. Frame axis:
    alpha 9.8 -> 16.2: Hopf limit cycle -> period doubling -> the single spiral scroll -> the
    double scroll jumping between two worlds. The window holds 1 -> 14 orbits, so frame 0 is one
    clean loop and the end is a pile of scroll-jumps; the diode's own kinks carry the top."""
    al = 9.8 + 6.4 * U ** 0.85
    m0, m1, be = -8.0 / 7.0, -5.0 / 7.0, 28.0

    def f(s):
        x, y, z = s
        fx = m1 * x + 0.5 * (m0 - m1) * (np.abs(x + 1) - np.abs(x - 1))
        return np.array([al * (y - x - fx), x - y + z, -be * y])
    s = np.array([np.full(F, 0.7), np.zeros(F), np.zeros(F)])
    s, _ = _rk4(f, s, 0.01, 6000)
    s, rec = _rk4(f, s, 0.005, 11000, rec=lambda s: s[0].copy())
    K = np.round(1 + 13 * U ** 1.4)
    fr = _cycles(rec, K)
    fr = _tilt(fr, 0.35 + 1.0 * U, 1000)
    return _fin(_drive(fr, 1.0 + 2.5 * U))


def van_der_pol_snap():
    """VAN DER POL SNAP — the relaxation oscillator x'' - mu(1-x^2)x' + x = A sin(2pi s/10).
    Frame axis: mu 0.25 -> 9 (a sine sharpens into a square with snapping edges), then the forcing
    A enters in the last third and the snap goes chaotic. One intrinsic cycle per frame (exact
    zero-crossing period, loops perfectly), rendered as x + its velocity spikes."""
    mu = 0.25 * (90.0 ** (U ** 0.8))
    A = 3.2 * _ss((U - 0.62) / 0.38)
    wf = 2 * np.pi / 10.0

    def f(s):
        x, v, c = s
        return np.array([v, mu * (1 - x * x) * v - x + A * np.sin(wf * c), np.ones_like(c)])
    s = np.array([np.full(F, 0.5), np.zeros(F), np.zeros(F)])
    s, _ = _rk4(f, s, 0.004, 15000)
    s, rec = _rk4(f, s, 0.004, 24000, rec=lambda s: np.concatenate([s[0], s[1]]))
    X, V = rec[:F], rec[F:]
    out = np.zeros((F, SIZE))
    for i in range(F):
        c = _upx(X[i])
        k = 2 if U[i] > 0.62 else 1
        a, b = (c[-1 - k], c[-1]) if len(c) > k else (0, len(X[i]) // 2)
        vv = V[i, a:b] / (np.abs(V[i, a:b]).max() + 1e-9)
        seg = X[i, a:b] / (np.abs(X[i, a:b]).max() + 1e-9) + 0.25 * vv * min(1.0, 0.3 + U[i])
        out[i] = _rs(seg[None])[0]
    y, _ = _drive_crest(_tilt(out, 0.3 + 0.7 * U, 1000), 4.0, 1.0 + 2.2 * U)   # fb638: the snap spikes no
    return _fin(y)                                                                # longer bury the square


def mackey_glass():
    """MACKEY GLASS — the blood-cell delay equation x' = 0.2 x(t-tau)/(1+x(t-tau)^n) - 0.1x.
    Frame axis: delay tau 6 -> 34 and exponent n 10 -> 22: slow periodic humps -> period doubling
    -> chaotic spike trains with hard shoulders. Each frame is 1 -> 7 of its own humps."""
    taus = 6.0 + 28.0 * U ** 1.1
    nn = 10.0 + 12.0 * U
    dt = 0.05
    D = (taus / dt).astype(int)
    B = int(D.max()) + 2
    hist = np.full((F, B), 0.9)
    x = np.full(F, 0.9)
    ar = np.arange(F)
    n_set, n_rec = 30000, 26000
    rec = np.empty((n_rec // 2, F))
    for i in range(n_set + n_rec):
        xd = hist[ar, (i - D) % B]
        x = x + dt * (0.2 * xd / (1.0 + xd ** nn) - 0.1 * x)
        hist[:, i % B] = x
        if i >= n_set and (i - n_set) % 2 == 0:
            rec[(i - n_set) // 2] = x
    rec = rec.T
    K = np.round(1 + 6 * U ** 1.3)
    fr = _cycles(rec, K)
    fr = _tilt(fr, 0.8 + 0.6 * U, 1000)
    return _fin(_drive(fr, 1.2 + 3.0 * U ** 2))


def neuron_burst():
    """NEURON BURST — a Hindmarsh-Rose neuron (x'=y-x^3+3x^2-z+I, y'=1-5x^2-y, z'=0.01(4(x+1.6)-z)).
    Frame axis: injected current I 1.55 -> 3.45. One slow burst cycle per frame: one spike per burst
    at frame 0, then the spike count per burst climbs, the bursts go chaotic, and the cell finally
    fires tonically. Membrane voltage IS the waveform: spike trains are needles of harmonics."""
    I = 1.55 + 1.9 * U ** 0.9
    rr = 0.01

    def f(s):
        x, y, z = s
        return np.array([y - x ** 3 + 3 * x * x - z + I, 1 - 5 * x * x - y, rr * (4 * (x + 1.6) - z)])
    s = np.array([np.full(F, -1.5), np.zeros(F), np.full(F, 2.0)])
    s, _ = _rk4(f, s, 0.05, 9000)
    s, rec = _rk4(f, s, 0.025, 18000, rec=lambda s: np.concatenate([s[0], s[2]]))
    X, Z = rec[:F], rec[F:]
    out = np.zeros((F, SIZE))
    for i in range(F):
        if U[i] < 0.93:
            c = _upx(Z[i])
            a, b = (c[1], c[2]) if len(c) > 2 else (0, 8000)
            xs = X[i, a:b]
            sp = np.where((xs[:-1] < 0.0) & (xs[1:] >= 0.0))[0]
            if len(sp):
                isi = (sp[-1] - sp[0]) / max(len(sp) - 1, 1) if len(sp) > 1 else 160.0
                lb = sp[-1] - sp[0] + isi
                rest = 0.6 * lb + 260
                a2 = int(max(0, a + sp[0] - rest / 2 - 0.3 * isi)); b2 = int(a + sp[-1] + rest / 2 + 0.7 * isi)
                a, b = a2, min(b2, X.shape[1] - 400)
        else:
            c = _upx(X[i])
            a, b = (c[1], c[7]) if len(c) > 7 else (0, 4000)
        L = b - a
        xf = max(4, int(0.05 * L))
        out[i] = _rs(_close(X[i, a:b + xf], L, xf)[None])[0]
    return _fin(_drive(_tilt(out, 0.1 + 0.3 * U, 1000), 1.8))


def lorenz_comparator():
    """LORENZ COMPARATOR — the Lorenz flow's x fed to a comparator (tanh of rising gain).
    Frame axis: rho 350 -> 28. At rho 350 the flow is a stable symmetric orbit, so frame 0 is ONE
    period of it = a soft square; as rho falls the orbit doubles and then lobe-switching goes
    chaotic, while the window grows from 1 to 26 switches: a random-telegraph slab. LOUD."""
    rho = 28.0 * (350.0 / 28.0) ** (1.0 - U ** 0.8)

    def f(s):
        x, y, z = s
        return np.array([10.0 * (y - x), x * (rho - z) - y, x * y - (8.0 / 3.0) * z])
    s = np.array([np.ones(F), np.ones(F), rho - 1.0])
    s, _ = _rk4(f, s, 0.002, 9000)
    s, rec = _rk4(f, s, 0.002, 36000, rec=lambda s: s[0].copy(), every=2)
    K = np.round(1 + 25 * U ** 1.25)
    fr = _cycles(rec, K, xf_frac=0.03)
    return _fin(_drive(fr, 1.4 + 14.0 * U ** 1.2))


def thomas_labyrinth():
    """THOMAS LABYRINTH — Thomas' cyclically symmetric flow x'=sin y - bx (and cyclic), sampled
    as sin(x+y). Frame axis: damping b 0.33 -> 0.035: a limit cycle, the period-doubled loop,
    chaos, and finally 'labyrinth chaos' — a deterministic random walk through a lattice of
    cells. The window grows from 1 to 18 loops."""
    bb = 0.035 + 0.295 * (1.0 - U) ** 1.2

    def f(s):
        x, y, z = s
        return np.array([np.sin(y) - bb * x, np.sin(z) - bb * y, np.sin(x) - bb * z])
    s = np.array([np.full(F, 0.1), np.full(F, 0.3), np.full(F, -0.2)])
    s, _ = _rk4(f, s, 0.1, 3000)
    s, rec = _rk4(f, s, 0.05, 16000, rec=lambda s: np.sin(s[0] + s[1]) + 0.5 * np.sin(2.0 * s[2]))
    K = np.round(1 + 17 * U ** 1.3)
    fr = _cycles(rec, K)
    fr = _tilt(fr, 0.6 + 1.0 * U, 1000)
    return _fin(fr)


def double_pendulum():
    """DOUBLE PENDULUM — two equal arms released from the slow normal mode at rising energy
    (theta1 0.25 -> 2.9 rad). Rendered as the WRAPPED angle of the outer arm: a gentle sway at
    frame 0 (one period of the normal mode), then the arm starts to flip over the top and every
    flip is a saw-riser. The window grows from 1 to 7 slow periods."""
    a = 0.25 + 2.65 * U ** 1.1
    g = 9.81

    def f(s):
        t1, t2, w1, w2 = s
        d = t1 - t2
        den = 3.0 - np.cos(2 * d)
        a1 = (-3 * g * np.sin(t1) - g * np.sin(t1 - 2 * t2) - 2 * np.sin(d) * (w2 * w2 + w1 * w1 * np.cos(d))) / den
        a2 = (2 * np.sin(d) * (2 * w1 * w1 + 2 * g * np.cos(t1) + w2 * w2 * np.cos(d))) / den
        return np.array([w1, w2, a1, a2])
    t2 = np.minimum(np.sqrt(2.0) * a, 3.0)
    s = np.array([a, t2, np.zeros(F), np.zeros(F)])
    Tslow = 2 * np.pi / math.sqrt(g * (2 - math.sqrt(2)))
    dt = 0.0025
    n = int(Tslow * 7.6 / dt)
    s, rec = _rk4(f, s, dt, n, rec=lambda s: np.concatenate([np.cos(s[1]), np.sin(s[1])]))
    C, Sn = rec[:F], rec[F:]
    out = np.zeros((F, SIZE))
    for i in range(F):
        k = 1.0 + 6.0 * U[i] ** 1.2
        L = int(k * Tslow / dt)
        xf = int(0.05 * L)
        cc = _up(_close(C[i], L, xf)[None])[0]
        sn = _up(_close(Sn[i], L, xf)[None])[0]
        ang = np.arctan2(sn, cc)
        out[i] = _rs(ang[None])[0]
    return _fin(out)


def _billiard_x(Ls, speeds, y0s, angs, T=1.0, M=OSN, extra=0.06):
    """Point particle in a stadium (two radius-1 caps joined by straights of length L).
    Returns x(t) sampled at M points over [0, T*(1+extra)) for each frame."""
    out = np.zeros((len(Ls), int(M * (1 + extra))))
    tt = np.arange(out.shape[1]) / M * T
    for i, (L, sp, y0, an) in enumerate(zip(Ls, speeds, y0s, angs)):
        h = L / 2.0
        p = np.array([0.0, y0]); v = sp * np.array([math.cos(an), math.sin(an)])
        times, xs = [0.0], [p[0]]
        tnow = 0.0
        for _ in range(4000):
            best, kind, cc = 1e9, None, None
            if abs(v[1]) > 1e-12:
                for wy in (1.0, -1.0):
                    tc = (wy - p[1]) / v[1]
                    if tc > 1e-9:
                        xc = p[0] + v[0] * tc
                        if -h - 1e-12 <= xc <= h + 1e-12 and tc < best:
                            best, kind = tc, ('w', wy)
            for cx in (h, -h):
                q = p - np.array([cx, 0.0])
                A = v @ v; Bq = 2 * (q @ v); C = q @ q - 1.0
                disc = Bq * Bq - 4 * A * C
                if disc >= 0:
                    r = math.sqrt(disc)
                    for tc in ((-Bq + r) / (2 * A), (-Bq - r) / (2 * A)):
                        if tc > 1e-9:
                            xc = p[0] + v[0] * tc
                            if (cx > 0 and xc >= h - 1e-12) or (cx < 0 and xc <= -h + 1e-12) or h == 0:
                                if tc < best:
                                    best, kind, cc = tc, ('c', cx), cx
            if kind is None:
                break
            p = p + v * best
            tnow += best
            if kind[0] == 'w':
                v = np.array([v[0], -v[1]])
            else:
                nrm = p - np.array([kind[1], 0.0]); nrm /= np.linalg.norm(nrm)
                v = v - 2 * (v @ nrm) * nrm
            times.append(tnow); xs.append(p[0])
            if tnow > tt[-1]:
                break
        out[i] = np.interp(tt, times, xs) / (1.0 + h)
    return out


def stadium_billiard():
    """STADIUM BILLIARD — a point ball in a Bunimovich stadium; the cycle is its x position.
    Frame 0: a circle and a ball bouncing along the diameter = a pure triangle wave. Frame axis:
    the straights grow (0 -> 1.6) and the launch tilts, so the regular orbit turns ergodic, and
    the ball speeds up from 1 to 13 round trips per cycle: a triangle shattering into zigzags."""
    Ls = 1.6 * U ** 1.1
    rt = 1.0 + 12.0 * U ** 1.35
    speeds = rt * 2.0 * (2.0 + Ls)
    y0s = 0.0005 + 0.25 * U
    angs = 0.0008 + 0.21 * U ** 1.2
    seg = _billiard_x(Ls, speeds, y0s, angs)
    out = np.zeros((F, SIZE))
    for i in range(F):
        out[i] = _rs(_close(seg[i], OSN, int(0.05 * OSN))[None])[0]
    return _fin(_tilt(out, 0.3 * U, 1000))


def chaotic_bouncer():
    """CHAOTIC BOUNCER — a ball on a sinusoidally vibrating table (restitution 0.6); the cycle is
    two table periods of the ball's height. Frame axis: table acceleration Gamma 1.3 -> 5.2:
    one clean arch per period (a pumping half-sine), period doubling (alternate arches: a
    sub-octave appears), chaos (arches of every size). Impacts are kinks = bright top."""
    Gm = 1.3 + 3.9 * U ** 0.9
    A = Gm / (2 * np.pi) ** 2
    e = 0.6
    h = np.zeros(F); v = np.zeros(F)
    ph = 0.0

    def run(nsteps, dt, rec):
        nonlocal h, v, ph
        out = np.empty((nsteps, F)) if rec else None
        for i in range(nsteps):
            ph += dt
            v = v - dt
            h = h + dt * v
            zt = A * np.sin(2 * np.pi * ph)
            vt = A * 2 * np.pi * np.cos(2 * np.pi * ph)
            hit = h < zt
            if hit.any():
                h = np.where(hit, zt, h)
                v = np.where(hit, vt + e * np.maximum(vt - v, 0.0), v)
            if rec:
                out[i] = h
        return out
    run(80 * 256, 1.0 / 256, False)
    ph = round(ph)
    rec = run(2 * 2048 + 300, 1.0 / 2048, True).T
    out = np.zeros((F, SIZE))
    for i in range(F):
        out[i] = _rs(_close(rec[i], 4096, 300)[None])[0]
    out = _tilt(out, 0.35 + 0.7 * U, 1000)
    return _fin(out)


def synapse_storm():
    """SYNAPSE STORM — a 48-neuron random recurrent tanh network (Sompolinsky), plus a planted
    rotation so that just past the edge of chaos it sings one clean oscillation. Frame axis:
    synaptic gain g 1.15 -> 7: the oscillation, then quasi-periodic beating, then full chaos with
    saturated neurons slamming rail to rail. 1 -> 16 cycles per frame."""
    N = 48
    rng = np.random.default_rng(0x5E7A95)
    J = rng.normal(0, 1.0 / math.sqrt(N), (N, N)) * 0.55
    a_ = rng.normal(0, 1, N); a_ /= np.linalg.norm(a_)
    b_ = rng.normal(0, 1, N); b_ -= (b_ @ a_) * a_; b_ /= np.linalg.norm(b_)
    J = J + 1.35 * (np.outer(a_, a_) + np.outer(b_, b_)) + 0.9 * (np.outer(b_, a_) - np.outer(a_, b_))
    g = 1.15 * (7.0 / 1.15) ** (U ** 1.2)
    x = rng.normal(0, 0.5, (F, N))
    dt = 0.05

    def f(x):
        return -x + g[:, None] * (np.tanh(x) @ J.T)
    for _ in range(3000):
        k1 = f(x); k2 = f(x + 0.5 * dt * k1)
        x = x + dt * k2
    rec = np.empty((9000, F))
    for i in range(9000):
        k1 = f(x); k2 = f(x + 0.5 * dt * k1)
        x = x + dt * k2
        rec[i] = np.tanh(1.5 * (x[:, 0] - 0.6 * x[:, 1]))
    rec = rec.T
    K = np.round(1 + 15 * U ** 1.3)
    fr = _cycles(rec, K)
    fr = _tilt(fr, 0.5 + 0.9 * U, 1000)
    return _fin(fr)


def three_body():
    """THREE BODY — the figure-eight choreography of three equal masses, perturbed. Frame 0 is
    one period of the eight (body 1's x-velocity: a smooth, strangely asymmetric wave). Frame
    axis: the perturbation of the middle body's velocity grows 0 -> 0.42 and the window grows to
    6 periods, so the dance breaks into close encounters — each one a velocity needle."""
    r1 = np.array([0.97000436, -0.24308753]); v3 = np.array([-0.93240737, -0.86473146])
    T8 = 6.32591398
    eps = 0.42 * U ** 1.4
    P = np.zeros((3, 2, F)); V = np.zeros((3, 2, F))
    P[0] = r1[:, None]; P[1] = -r1[:, None]
    V[2] = (v3[:, None] * (1.0 + eps))
    V[0] = -v3[:, None] / 2; V[1] = -v3[:, None] / 2
    soft = 0.004

    def acc(P):
        A = np.zeros_like(P)
        for i in range(3):
            for j in range(i + 1, 3):
                d = P[j] - P[i]
                r3 = (d[0] ** 2 + d[1] ** 2 + soft) ** 1.5
                A[i] += d / r3; A[j] -= d / r3
        return A
    dt = 0.0025
    n = int(T8 * 6.5 / dt)
    rec = np.empty((n, F))
    Acc = acc(P)
    for k in range(n):
        V += 0.5 * dt * Acc
        P += dt * V
        Acc = acc(P)
        V += 0.5 * dt * Acc
        rec[k] = V[0, 0]
    rec = rec.T
    out = np.zeros((F, SIZE))
    for i in range(F):
        L = int(T8 * (1.0 + 5.0 * U[i] ** 1.2) / dt)
        xf = int(0.04 * L)
        seg = np.tanh(0.8 * rec[i])
        out[i] = _rs(_close(seg, L, xf)[None])[0]
    out = _tilt(out, 0.3 * U, 1000)
    return _fin(out)


# ══ MAPS ══════════════════════════════════════════════════════════════════════════════════════
def _ikeda_seq(p, n, warm=600, tail=256):
    """(len(p), n + tail) orbits of the Ikeda map, one per p, read as Re z + 0.7 Im z."""
    z = np.full(len(p), 0.1 + 0.1j)
    for _ in range(warm):
        z = 1 + p * z * np.exp(1j * (0.4 - 6.0 / (1 + np.abs(z) ** 2)))
    seq = np.empty((n + tail, len(p)))
    for k in range(n + tail):
        z = 1 + p * z * np.exp(1j * (0.4 - 6.0 / (1 + np.abs(z) ** 2)))
        seq[k] = np.real(z) + 0.7 * np.imag(z)
    return seq.T


def ikeda_tremor():
    """LASER TREMOR — the Ikeda laser-cavity map z -> 1 + p z exp(i(0.4 - 6/(1+|z|^2))). The
    orbit's phase, unwrapped and de-trended, phase-modulates a sine. Frame axis: p 0.633 -> 0.9015
    (a period-4 orbit: a pure sine -> period 8, 16 -> the strange attractor, trembling by frame 7)
    with the index growing, so the sine trembles and then shatters into an FM storm.
    fb638: p stops at 0.9015 — past ~0.907 the orbit is captured by a coexisting fixed point and
    the storm snapped back to a bare sine for the last 16 frames; a frame whose p lands in a
    periodic window is nudged to the nearest chaotic p so the storm never drops out."""
    s = (22.0 + 88.0 * U) / (F - 1.0)           # the old journey's frames 22 -> 110, stretched
    p = 0.55 + 0.40 * s ** 0.9
    n = 2048
    seq = _ikeda_seq(p, n)
    nd = np.array([len(np.unique(np.round(q[:n], 5))) for q in seq])
    for i in np.where((nd < 0.95 * n) & (p > 0.647))[0]:
        for k in range(1, 80):
            d = 0.00025 * ((k + 1) // 2) * (1 if k % 2 else -1)
            q = np.array([p[i] + d]) if p[i] + d <= p.max() else np.array([p[i] - abs(d)])
            sq = _ikeda_seq(q, n)[0]
            if len(np.unique(np.round(sq[:n], 5))) >= 0.95 * n:
                seq[i] = sq
                break
    seq = seq - seq.mean(axis=1, keepdims=True)
    mod = np.array([_close(seq[i], n, 256) for i in range(F)])
    mod = _hsm(mod, 3)
    modu = _up(mod)
    idx = 0.15 + 9.0 * s ** 1.5
    tt = np.arange(OSN) / OSN
    y = np.sin(2 * np.pi * tt[None, :] + idx[:, None] * modu)
    return _fin(_rs(y))


def _cloud(fn, P=768, warm=40, n=200, seed=1):
    """Iterate a 2-D map from P random seeds per frame (all frames at once); returns (F, P*n) x, y."""
    rng = np.random.default_rng(seed)
    x = rng.uniform(-0.5, 0.5, (F, P)); y = rng.uniform(-0.5, 0.5, (F, P))
    for _ in range(warm):
        x, y = fn(x, y)
    X = np.empty((F, P, n)); Y = np.empty((F, P, n))
    for k in range(n):
        x, y = fn(x, y)
        X[:, :, k] = x; Y[:, :, k] = y
    return X.reshape(F, -1), Y.reshape(F, -1)


def _hist_rows(V, lo, hi, nb=2048):
    V = np.clip((V - lo[:, None]) / (hi - lo)[:, None], 0, 0.999999)
    out = np.zeros((V.shape[0], nb))
    for i in range(V.shape[0]):
        out[i] = np.bincount((V[i] * nb).astype(int), minlength=nb)
    return out


def de_jong_shadow():
    """DE JONG SHADOW — Peter de Jong's attractor x' = sin(a y) - cos(b x), y' = sin(c x) - cos(d y).
    The cycle is its SHADOW: the density of x drawn upward and the density of y drawn downward.
    Frame axis: (a, b, c, d) glide from a thin quasi-periodic ring (two sharp horns) to full
    chaos, where the shadow becomes a jagged fractal skyline."""
    a = 1.4 + (-2.24 - 1.4) * U ** 0.9
    b = -2.3 + (0.43 + 2.3) * U
    c = 2.4 + (-0.65 - 2.4) * U ** 1.1
    d = -2.1 + (-2.43 + 2.1) * U
    A, Bb, C, D = a[:, None], b[:, None], c[:, None], d[:, None]
    X, Y = _cloud(lambda x, y: (np.sin(A * y) - np.cos(Bb * x), np.sin(C * x) - np.cos(D * y)), P=1024, n=160)
    lo = np.full(F, -2.05); hi = np.full(F, 2.05)
    hx = _hist_rows(X, lo, hi); hy = _hist_rows(Y, lo, hi)
    w = _hsm(hx, 2) ** 0.35 - _hsm(hy, 2) ** 0.35
    lim = np.percentile(np.abs(w), 97.0, axis=1)[:, None]
    w = np.clip(w, -lim, lim)
    return _fin(_rs(w))


def hopalong_petals():
    """HOPALONG PETALS — Barry Martin's Hopalong map x' = y - sign(x)sqrt|bx - c|, y' = a - x.
    The cycle is the ANGULAR density of the orbit around its centre (a full turn = one cycle),
    so rotating petal structures become lobes and spikes. Frame axis: (a, b, c) walk from a
    few fat petals into a dense many-armed chaos flower."""
    a = 0.2 + 11.8 * U ** 1.2
    b = 0.5 + 2.5 * U
    c = 5.0 * U ** 1.5
    A, Bb, C = a[:, None], b[:, None], c[:, None]
    X, Y = _cloud(lambda x, y: (y - np.sign(x) * np.sqrt(np.abs(Bb * x - C)), A - x), P=512, warm=20, n=260, seed=7)
    X = X - X.mean(axis=1, keepdims=True); Y = Y - Y.mean(axis=1, keepdims=True)
    th = (np.arctan2(Y, X) / (2 * np.pi)) % 1.0
    out = np.zeros((F, SIZE))
    for i in range(F):
        out[i] = np.bincount((th[i] * SIZE).astype(int) % SIZE, minlength=SIZE)
    out = np.sqrt(_hsm(out, 2))
    return _fin(_blur(_rs(out), 10.0 * 100.0 ** (U ** 0.9)))


def clifford_contour():
    """CLIFFORD CONTOUR — Clifford Pickover's attractor x' = sin(a y) + c cos(a x),
    y' = sin(b x) + d cos(b y). The cycle is its OUTLINE: the outermost radius of the point cloud
    at every angle around its centre. Frame axis: the parameters travel from a smooth closed
    loop (a lumpy sine) to the full attractor, whose outline is all horns and fractal bays."""
    a = -1.25 + (-1.7 + 1.25) * U
    b = 1.6 + (1.3 - 1.6) * U
    c = -0.35 + (-0.1 + 0.35) * U ** 0.8 + 0.0
    d = -0.35 + (-1.21 + 0.35) * U ** 0.7
    A, Bb, C, D = a[:, None], b[:, None], c[:, None], d[:, None]
    X, Y = _cloud(lambda x, y: (np.sin(A * y) + C * np.cos(A * x), np.sin(Bb * x) + D * np.cos(Bb * y)),
                  P=640, n=150, seed=3)
    X = X - X.mean(axis=1, keepdims=True); Y = Y - Y.mean(axis=1, keepdims=True)
    nb = 1024
    th = ((np.arctan2(Y, X) / (2 * np.pi)) % 1.0 * nb).astype(int) % nb
    R = np.hypot(X, Y)
    out = np.zeros((F, nb))
    for i in range(F):
        m = np.zeros(nb)
        np.maximum.at(m, th[i], R[i])
        z = m <= 0
        if z.any():
            k = np.arange(nb)
            m[z] = np.interp(k[z], k[~z], m[~z], period=nb)
        out[i] = m
    out = _hsm(out, 2)
    return _fin(_rs(out))


def kam_torus_break():
    """KAM TORUS BREAK — Chirikov's standard map on the golden-mean torus. The cycle is the torus
    itself: the orbit's momentum p as a function of its angle theta (60 000 iterates binned into
    2048 angles), so it loops by construction. Frame axis: kick K 0.25 -> 2.6: a gently bent sine,
    a torus wrinkling toward its critical point (K = 0.9716), then the last KAM curve breaks and
    the orbit smears into a chaotic sea."""
    K = 0.25 + 2.35 * U ** 1.3
    th = np.zeros(F); p = np.full(F, (math.sqrt(5) - 1) / 2)
    n = 60000
    TH = np.empty((n, F), dtype=np.float32); PP = np.empty((n, F), dtype=np.float32)
    kk = K / (2 * np.pi)
    for i in range(n):
        p = p + kk * np.sin(2 * np.pi * th)
        th = (th + p) % 1.0
        TH[i] = th; PP[i] = p
    out = np.zeros((F, SIZE))
    for i in range(F):
        b = (TH[:, i] * SIZE).astype(int) % SIZE
        pw = PP[:, i].astype(float)
        pw = (pw - pw.mean())
        pw = np.sin(np.pi * np.clip(pw, -1.5, 1.5) / 1.5)
        cnt = np.bincount(b, minlength=SIZE)
        sm = np.bincount(b, weights=pw, minlength=SIZE)
        m = np.divide(sm, cnt, out=np.zeros(SIZE), where=cnt > 0)
        if (cnt == 0).any():
            k = np.arange(SIZE); z = cnt == 0
            m[z] = np.interp(k[z], k[~z], m[~z], period=SIZE)
        out[i] = m
    return _fin(_rs(out))


def julia_approach():
    """JULIA APPROACH — the escape potential G(z) = lim log|z_n| / 2^n of the Douady rabbit
    (z -> z^2 - 0.123 + 0.745i), sampled around an off-centre circle. Frame axis: the circle shrinks
    from far outside (G is a smooth lopsided sine there) down onto the Julia set itself, so the
    wave grows cusps, then the set's fractal coastline, then flat 'captured' plateaus."""
    cc = -0.123 + 0.745j
    M = 8192
    R = 2.1 * (0.46 / 2.1) ** (U ** 0.9)
    ctr = 0.22 + 0.12j + 0.12 * U * np.exp(1j * 1.7)
    th = 2 * np.pi * np.arange(M) / M
    z = ctr[:, None] + R[:, None] * np.exp(1j * th)[None, :]
    G = np.zeros(z.shape)
    alive = np.ones(z.shape, bool)
    for k in range(48):
        z = np.where(alive, z * z + cc, z)
        az = np.abs(z)
        esc = alive & (az > 1e3)
        G[esc] = np.log(az[esc]) / 2.0 ** (k + 1)
        alive &= ~esc
    G = np.sqrt(np.maximum(G, 0))
    return _fin(_tilt(_rs(G), 0.45 * U, 1000))


def riffle_shuffle():
    """RIFFLE SHUFFLE — a saw treated as a deck of 16 384 cards. Each card is sent to a warped
    destination t + D*noise(t) (a seeded smooth random field) and the deck is re-sorted by
    destination. Frame axis: D and the field's bandwidth grow, so the saw first bends, then folds
    over itself, then is cut and interleaved into a hail of ramp fragments."""
    rng = np.random.default_rng(0x81FF1E)
    tt = np.arange(OSN) / OSN
    H = 96
    ph = rng.uniform(0, 2 * np.pi, H)
    amp = rng.normal(0, 1, H) / (np.arange(1, H + 1) ** 0.7)
    src = np.sin(2 * np.pi * tt) + 0.3 * np.sin(4 * np.pi * tt + 0.5)
    out = np.zeros((F, OSN))
    for i in range(F):
        bw = 1.5 + 90.0 * U[i] ** 1.6
        wgt = amp * np.exp(-0.5 * (np.arange(1, H + 1) / bw) ** 2)
        noise = (wgt[:, None] * np.sin(2 * np.pi * np.arange(1, H + 1)[:, None] * tt[None, :] + ph[:, None])).sum(0)
        noise /= np.abs(noise).max()
        D = 0.02 + 0.55 * U[i] ** 1.3
        dest = tt + D * noise
        out[i] = src[np.argsort(dest, kind='stable')]
    return _fin(_rs(out))


def lyapunov_loop():
    """LYAPUNOV LOOP — the Markus-Lyapunov fractal: for the forcing sequence AABAB the logistic
    rate alternates between a and b, and lambda(a, b) is the orbit's Lyapunov exponent. The cycle
    is lambda along a closed circle in (a, b) space. Frame axis: the circle drifts from the calm
    ordered zone (dark rounded valleys) into the chaotic zone, where it cuts the fractal's
    knife-edge superstable spines, and the spectrum tilts from dark to bright with it."""
    M = 2048
    th = 2 * np.pi * np.arange(M) / M
    ca = 3.05 + 0.78 * U ** 0.9
    cb = 3.3 + 0.52 * U
    Rr = 0.18 + 0.1 * U
    A = ca[:, None] + Rr[:, None] * np.cos(th)[None, :]
    B = cb[:, None] + Rr[:, None] * np.sin(th + 0.4)[None, :]
    A = np.clip(A, 0.5, 3.999); B = np.clip(B, 0.5, 3.999)
    seq = [A, A, B, A, B]
    x = np.full(A.shape, 0.5)
    lam = np.zeros(A.shape)
    for k in range(260):
        r = seq[k % 5]
        x = r * x * (1 - x)
        if k >= 60:
            lam += np.log(np.abs(r * (1 - 2 * x)) + 1e-12)
    lam /= 200.0
    w = np.tanh(1.3 * np.clip(lam, -4, 2))
    return _fin(_tilt(_rs(w), -0.6 + 0.9 * U, 1000))


def logistic_depth():
    """LOGISTIC DEPTH — x_N of the logistic map (x0 = 0.5) as a function of the rate r, with r
    swept round a lopsided closed path inside every cycle. N = 1 draws the path itself (a skewed
    sine); every extra iteration doubles the polynomial degree. Frame axis: N 1 -> 34 while the
    path's range climbs from the ordered r < 3 into the chaotic r -> 4: a curve that folds into
    a comb of 2^N wiggles."""
    tt = np.arange(OSN) / OSN
    b = 0.5 - 0.5 * np.cos(2 * np.pi * (tt + 0.13 * np.sin(2 * np.pi * tt)))
    lo = 2.55 + 0.9 * U ** 1.3
    hi = 3.05 + 0.949 * U ** 0.7
    Nf = 1.0 + 33.0 * U ** 1.5
    out = np.zeros((F, OSN))
    x = np.full((F, OSN), 0.5)
    r = lo[:, None] + (hi - lo)[:, None] * b[None, :]
    prev = x.copy()
    for k in range(1, 36):
        prev = x
        x = r * x * (1 - x)
        sel = np.floor(Nf).astype(int) == k
        if sel.any():
            w = (Nf[sel] - k)[:, None]
            nxt = r[sel] * x[sel] * (1 - x[sel])
            out[sel] = (1 - w) * x[sel] + w * nxt
    return _fin(_rs(out))


def newton_basins():
    """NEWTON BASINS — Newton's method for z^3 = 1, run from every point of a circle; the cycle is
    which root each point falls into (three levels), shaded by how fast it got there. Frame 0: a
    wide circle crossing the three basins cleanly = a three-step stair. Frame axis: the circle
    shrinks and slides onto the fractal basin boundary, where the stair splinters into
    infinitely nested switching."""
    M = 8192
    th = 2 * np.pi * np.arange(M) / M
    R = 2.6 * (0.62 / 2.6) ** (U ** 0.9)
    ctr = 0.35 * U ** 0.8 * np.exp(1j * 0.52)
    z = ctr[:, None] + R[:, None] * np.exp(1j * (th[None, :] + 0.3))
    Nf = 1.0 + 29.0 * U ** 1.3

    def step(z):
        z2 = z * z
        z = z - (z2 * z - 1.0) / (3.0 * z2 + 1e-12)
        az = np.abs(z)
        return np.where(az > 3.0, 3.0 * z / np.maximum(az, 1e-12), z)

    def val(z):
        return np.real(z) + 0.8 * np.imag(z)
    out = np.zeros((F, M))
    for k in range(1, 32):
        z = step(z)
        sel = np.floor(Nf).astype(int) == k
        if sel.any():
            w = _ss(Nf[sel] - k)[:, None]
            out[sel] = (1 - w) * val(z[sel]) + w * val(step(z[sel]))
    return _fin(_rs(out))


# ══ SPATIAL SYSTEMS — the state of a ring IS the cycle, so these loop by construction ═══════════
def _life(G, n):
    for _ in range(n):
        a = np.roll(G, 1, 0); b = np.roll(G, -1, 0)
        N = (a + b + np.roll(G, 1, 1) + np.roll(G, -1, 1) + np.roll(a, 1, 1) + np.roll(a, -1, 1)
             + np.roll(b, 1, 1) + np.roll(b, -1, 1))
        G = ((N == 3) | ((G == 1) & (N == 2))).astype(np.int8)
    return G


def _hold(rows, M=OSN):
    """Cells -> a sample-and-hold staircase at 8x (exact edges, band-limited afterwards)."""
    rows = np.atleast_2d(np.asarray(rows, float))
    idx = (np.arange(M) * rows.shape[1]) // M
    return rows[:, idx]


def life_slice():
    """LIFE SLICE — Conway's Game of Life on a 48 x 256 torus; the cycle is a band of five rows
    read left to right (live = up). Frame 0: a nearly solid half-block, so the slice is a square
    wave with a few pits. Frame axis: 3 generations per frame — the block's edges erode, gliders
    and debris spread round the torus and the square wave dissolves into a living pulse train."""
    rng = np.random.default_rng(0x11FE51)
    H, W = 48, 256
    G = np.zeros((H, W), np.int8)
    G[:, :128] = rng.random((H, 128)) < 0.965
    out = np.zeros((F, W))
    for f in range(F):
        out[f] = G[21:26].mean(axis=0)
        G = _life(G, 3)
    return _fin(_rs(_hold(out)))


def life_census():
    """LIFE CENSUS — Game of Life on a 64 x 256 torus; the cycle is the COLUMN CENSUS (how many
    cells are alive in each column). Frame 0: a solid triangular mountain of live cells, so the
    census is a triangle wave. Frame axis: generations 0 -> 420 (accelerating): the mountain's
    flanks collapse into oscillators and glider debris and the census becomes a skyline."""
    rng = np.random.default_rng(0xCE5505)
    H, W = 64, 256
    col = np.arange(W)
    tri = 1 - np.abs(col / W * 2 - 1)
    h = (3 + 58 * tri).astype(int)
    G = ((np.arange(H)[:, None] < h[None, :]) & (rng.random((H, W)) < 0.9)).astype(np.int8)
    gens = np.round(420 * U ** 1.5).astype(int)
    out = np.zeros((F, W))
    g = 0
    for f in range(F):
        G = _life(G, gens[f] - g); g = gens[f]
        out[f] = np.sqrt(G.sum(axis=0) + 0.5)
    return _fin(_rs(_hold(out)))


def rule_54_walk():
    """RULE 54 WALK — Wolfram's rule 54 on a 512-cell ring, rendered as a WALK: the running sum of
    (live = up, dead = down) steps, de-trended so it closes. Frame 0: one block of live cells =
    a triangle wave. Frame axis: 2 generations per frame; the block's edges spit gliders that
    collide and annihilate, and the triangle becomes a fractal mountain range."""
    N = 512
    tab = np.array([(54 >> k) & 1 for k in range(8)], np.int8)
    c = np.zeros(N, np.int8); c[150:350] = 1
    out = np.zeros((F, OSN))
    ramp = np.arange(OSN) / OSN
    for f in range(F):
        st = _hold((2.0 * c - 1.0)[None])[0]
        cs = np.cumsum(st) / OSN * 8.0
        out[f] = cs - ramp * cs[-1]
        for _ in range(2):
            c = tab[4 * np.roll(c, 1) + 2 * c + np.roll(c, -1)]
    return _fin(_tilt(_rs(out), 0.3 + 0.8 * U, 1000))


def ising_heatwave():
    """ISING HEATWAVE — a 32 x 512 Ising magnet (Metropolis dynamics), read as the mean spin of
    four rows. Frame 0: two domains (up / down) = a square wave. Frame axis: the temperature
    ramps from 1.3 through the Curie point (2.27) to 4.5 — domain walls start to wander and
    sprout islands, then the order melts into flickering thermal noise."""
    rng = np.random.default_rng(0x151A69)
    H, W = 32, 512
    s = np.ones((H, W)); s[:, W // 2:] = -1
    T = 1.3 + 3.2 * U ** 1.25
    ii, jj = np.meshgrid(np.arange(H), np.arange(W), indexing='ij')
    masks = [((ii + jj) % 2) == 0, ((ii + jj) % 2) == 1]
    out = np.zeros((F, W))
    for f in range(F):
        out[f] = s[12:16].mean(axis=0)
        for _ in range(3):
            for m in masks:
                nb = np.roll(s, 1, 0) + np.roll(s, -1, 0) + np.roll(s, 1, 1) + np.roll(s, -1, 1)
                dE = 2.0 * s * nb
                acc = m & ((dE <= 0) | (rng.random((H, W)) < np.exp(-dE / T[f])))
                s = np.where(acc, -s, s)
    return _fin(_rs(_hold(out)))


def sandpile_fractal():
    """SANDPILE FRACTAL — the abelian sandpile: grains dropped on the centre of a 129 x 129 table
    topple 4 at a time onto their neighbours. The cycle is a slice through the middle of the pile
    (heights 0-3). Frame axis: grains 400 -> 14 000 — a dark rounded pulse widens and sharpens
    into the pile's self-similar fractal terraces."""
    Wd = 101
    Hh = np.zeros((Wd, Wd), np.int64)
    tot = np.round(400 + 13600 * U ** 1.4).astype(int)
    out = np.zeros((F, Wd))
    have = 0
    c = Wd // 2
    for f in range(F):
        Hh[c, c] += tot[f] - have; have = tot[f]
        while True:
            q = Hh // 4
            if not q.any():
                break
            Hh = Hh - 4 * q
            Hh[1:, :] += q[:-1, :]; Hh[:-1, :] += q[1:, :]
            Hh[:, 1:] += q[:, :-1]; Hh[:, :-1] += q[:, 1:]
            Hh[0, :] = 0; Hh[-1, :] = 0; Hh[:, 0] = 0; Hh[:, -1] = 0
        out[f] = Hh[c - 1:c + 2].mean(axis=0) + 0.35 * Hh[c - 9]
    return _fin(_tilt(_rs(_hold(out)), -0.5 + 0.8 * U, 1000))


def _etdrk4_coeffs(Lop, h, M=32):
    r = np.exp(1j * np.pi * (np.arange(1, M + 1) - 0.5) / M)
    LR = h * Lop[:, None] + r[None, :]
    E = np.exp(h * Lop); E2 = np.exp(h * Lop / 2)
    Q = h * np.real(np.mean((np.exp(LR / 2) - 1) / LR, axis=1))
    f1 = h * np.real(np.mean((-4 - LR + np.exp(LR) * (4 - 3 * LR + LR ** 2)) / LR ** 3, axis=1))
    f2 = h * np.real(np.mean((2 + LR + np.exp(LR) * (-2 + LR)) / LR ** 3, axis=1))
    f3 = h * np.real(np.mean((-4 - 3 * LR - LR ** 2 + np.exp(LR) * (4 - LR)) / LR ** 3, axis=1))
    return E, E2, Q, f1, f2, f3


def _ks_run(N, q, times, u0, h=0.2):
    """Kuramoto-Sivashinsky u_t = -u u_x - u_xx - u_xxxx on a ring of length 2 pi q (ETDRK4)."""
    k = np.fft.rfftfreq(N, d=1.0 / N) / q
    Lop = k ** 2 - k ** 4
    E, E2, Q, f1, f2, f3 = _etdrk4_coeffs(Lop, h)
    g = -0.5j * k
    v = np.fft.rfft(u0)
    nl = lambda v: g * np.fft.rfft(np.fft.irfft(v, n=N) ** 2)
    out = []
    tnow = 0.0
    for T in times:
        while tnow < T - 1e-9:
            Nv = nl(v)
            a = E2 * v + Q * Nv; Na = nl(a)
            b = E2 * v + Q * Na; Nb = nl(b)
            c = E2 * a + Q * (2 * Nb - Nv); Nc = nl(c)
            v = E * v + Nv * f1 + 2 * (Na + Nb) * f2 + Nc * f3
            tnow += h
        out.append(np.fft.irfft(v, n=N))
    return np.array(out)


def flame_front():
    """FLAME FRONT — the Kuramoto-Sivashinsky equation (the physics of a wrinkling flame front) on
    a ring. Frame 0: a flat front with one gentle bend = a sine. Frame axis: time. The bend's
    harmonics grow, cells form at the most unstable wavelength, then split, merge and drift in
    spatiotemporal chaos; rendered as front + slope so the cell cusps bite."""
    N, q = 1024, 34.0
    x = np.arange(N) / N * 2 * np.pi
    u0 = 1.2 * np.cos(x) + 0.02 * np.cos(3 * x + 0.4)
    times = 260.0 * U ** 1.7
    uu = _ks_run(N, q, times, u0)
    k = np.fft.rfftfreq(N, d=1.0 / N)
    dx = np.fft.irfft(1j * k * np.fft.rfft(uu, axis=1), n=N, axis=1) / q
    fr = uu / (np.abs(uu).max(axis=1, keepdims=True) + 1e-9) + 0.9 * U[:, None] * dx / (np.abs(dx).max(axis=1, keepdims=True) + 1e-9)
    y, _ = _drive_crest(_tilt(_rs(fr), 0.5 + 1.1 * U, 1000), 4.0, 1.6)   # fb638: the lone-shock frames
    return _fin(y)                                                     # (32-64) no longer sink to -17 dB


def formant_flame():
    """FORMANT FLAME — the same flame-front chaos, but living in the SPECTRUM: a Kuramoto-Sivashinsky
    field on a ring of 1024 sites laid along the log-frequency axis becomes the log-amplitude of
    the harmonics. Frame 0: one smooth spectral hill. Frame axis: time — the hill wrinkles into
    formant cells that split, merge and wander like a throat possessed."""
    N, q = 1024, 26.0
    x = np.arange(N) / N * 2 * np.pi
    u0 = 1.5 * np.cos(x) + 0.03 * np.cos(2 * x + 1.0)
    times = 30.0 + 300.0 * U ** 1.5
    uu = _ks_run(N, q, times, u0)
    pos = np.log2(n_ax) / 10.0 * N
    mags = np.zeros((F, NH))
    for f in range(F):
        uf = uu[f] / (uu[f].std() + 1e-9)
        val = np.interp(pos, np.arange(N + 1), np.append(uf, uf[0]))
        mags[f] = np.exp((1.1 + 1.2 * U[f]) * val) / n_ax ** (0.55 - 0.25 * U[f])
    return _spec(mags)


def spot_splitter():
    """SPOT SPLITTER — Gray-Scott reaction-diffusion on a ring (F 0.04, k 0.06): the cycle is the
    activator concentration. Frame 0: a single smooth bump. Frame axis: time — the bump splits
    in two, the halves split again, and the ring fills with a jostling crowd of self-replicating
    pulses."""
    N, L, Du, Dv, dt = 1024, 2.5, 2e-5, 1e-5, 1.0
    x = np.arange(N) / N * L
    v = 0.28 * np.exp(-0.5 * ((x - L / 2) / 0.035) ** 2)
    u = 1.0 - 2.0 * v
    k = 2 * np.pi * np.fft.rfftfreq(N, d=L / N)
    du = 1 / (1 + dt * Du * k ** 2); dv = 1 / (1 + dt * Dv * k ** 2)
    steps = np.round(14000 * U ** 1.25).astype(int)
    out = np.zeros((F, N))
    st = 0
    for f in range(F):
        while st < steps[f]:
            uvv = u * v * v
            u = np.fft.irfft(np.fft.rfft(u + dt * (-uvv + 0.04 * (1 - u))) * du, n=N)
            v = np.fft.irfft(np.fft.rfft(v + dt * (uvv - 0.10 * v)) * dv, n=N)
            st += 1
        out[f] = (v / (v.max() + 1e-12)) ** 2
    return _fin(_tilt(_rs(out), 0.7 + 1.0 * U, 1000))


def ginzburg_defects():
    """GINZBURG DEFECTS — the complex Ginzburg-Landau equation A_t = A + (1+1.5i)A_xx -
    (1-1.2i)|A|^2 A (Benjamin-Feir unstable) on a ring; the cycle is Re A. Frame 0: a plane wave
    = a pure sine. Frame axis: time — the wave's phase starts to wobble, the wobble grows into
    modulated wavetrains, and amplitude defects (holes) punch through: defect turbulence."""
    N, L = 1024, 160.0
    b, c = 1.5, -1.2
    rng = np.random.default_rng(0x61E2)
    x = np.arange(N) / N * L
    qq = 2 * np.pi / L
    A0 = np.sqrt(1 - qq * qq) * np.exp(1j * qq * x)
    A = A0 + 3e-3 * (rng.normal(size=N) + 1j * rng.normal(size=N))
    k = 2 * np.pi * np.fft.fftfreq(N, d=L / N)
    dt = 0.05
    Lk = 1 - (1 + 1j * b) * k ** 2
    E = np.exp(dt * Lk); E2 = np.exp(dt * Lk / 2)
    times = 190.0 * U ** 0.9
    out = np.zeros((F, N))
    tn = 0.0
    for f in range(F):
        while tn < times[f] - 1e-9:
            Ah = np.fft.fft(A)
            Nl = -(1 + 1j * c) * np.abs(A) ** 2 * A
            Am = np.fft.ifft(E2 * (Ah + 0.5 * dt * np.fft.fft(Nl)))
            Nm = -(1 + 1j * c) * np.abs(Am) ** 2 * Am
            A = np.fft.ifft(E * Ah + dt * E2 * np.fft.fft(Nm))
            tn += dt
        Af = A0 if f == 0 else A          # fb638: frame 0 is the clean plane wave, not the seed hash
        out[f] = np.real(Af) + 0.4 * U[f] * (np.abs(Af) - 1)
    return _fin(_tilt(_rs(out), 0.6 + 1.2 * U, 1000))


_D2 = {}


def _logheat(lphi, s2):
    """Exact periodic heat step in the LOG domain: log(G_s2 * exp(lphi)) by log-sum-exp.
    (An FFT heat step on exp(lphi) drowns in round-off once lphi spans more than ~30 nats,
    which is exactly the sharp-shock regime we want.)"""
    N = lphi.shape[0]
    if N not in _D2:
        x = np.arange(N) / N
        d = np.abs(x[:, None] - x[None, :]); d = np.minimum(d, 1.0 - d)
        _D2[N] = d * d
    M = lphi[None, :] - _D2[N] / (2.0 * s2)
    mx = M.max(axis=1, keepdims=True)
    return mx[:, 0] + np.log(np.exp(M - mx).sum(axis=1))


def _dper(v):
    N = v.shape[0]
    return (np.roll(v, -1) - np.roll(v, 1)) * (N / 2.0)


def burgulence():
    """BURGULENCE — Burgers' equation u_t + u u_x = nu u_xx (the simplest shock physics), solved
    exactly through the Cole-Hopf transform with a log-domain heat kernel. Frame 0: a sine.
    Frame axis: time — the sine steepens into a shock (it becomes a saw, as Burgers promised),
    then random large-scale kicks keep spawning new shocks that race, collide and merge:
    'Burgers turbulence', a hail of saw-teeth."""
    N, nu = 1024, 0.0006
    x = np.arange(N) / N
    lphi = np.cos(2 * np.pi * x) / (4 * np.pi * nu)
    krng = np.random.default_rng(0xB066)
    KPH = krng.uniform(0, 2 * np.pi, (40, 5))
    KA = krng.uniform(0.6, 1.4, (40, 5))
    ftimes = np.concatenate([0.26 * U[:40] / U[39], 0.26 + 2.2 * ((U[40:] - U[40]) / (1 - U[40])) ** 1.2])
    ev = [(tf, 0, f) for f, tf in enumerate(ftimes)] + [(0.28 + 0.07 * j, 1, j) for j in range(32)]
    ev.sort()
    out = np.zeros((F, N))
    tc = 0.0
    for tv, kind, j in ev:
        if tv > tc:
            lphi = _logheat(lphi, 2 * nu * (tv - tc)); tc = tv
        if kind == 1:
            K = sum((0.07 / m) * KA[j, m] * np.cos(2 * np.pi * m * x + KPH[j, m]) for m in range(1, 5))
            lphi = lphi - K / (2 * nu)
        else:
            out[j] = -2 * nu * _dper(lphi)
        lphi = lphi - lphi.max()
    return _fin(_rs(out))


def kpz_surface():
    """KPZ SURFACE — Kardar-Parisi-Zhang growth h_t = nu h_xx + h_x^2/2 + noise (a burning paper
    edge, a bacterial colony front), via Cole-Hopf with a log-domain heat kernel. Frame 0: a
    smooth three-hill landscape. Frame axis: growth time under a white random rain — the
    valleys sharpen into cusps (parabolic scallops meeting at knife-edges), then the whole
    profile roughens into KPZ's jagged universality."""
    N, nu = 1024, 0.0015
    x = np.arange(N) / N
    h0 = 0.12 * np.cos(2 * np.pi * x) + 0.07 * np.cos(4 * np.pi * x + 1.0) + 0.05 * np.cos(6 * np.pi * x + 2.2)
    lphi = h0 / (2 * nu)
    rng = np.random.default_rng(0x4B2)
    nst = 240
    T = 0.9
    dt = T / nst
    fstep = np.round(nst * U ** 1.2).astype(int)
    out = np.zeros((F, N))
    f = 0
    for k in range(nst + 1):
        while f < F and fstep[f] == k:
            out[f] = 2 * nu * lphi; f += 1
        if k == nst:
            break
        lphi = _logheat(lphi, 2 * nu * dt)
        amp = 0.0035 * min(1.0, (k * dt) / 0.4)
        lphi = lphi + amp * rng.normal(size=N) / (2 * nu)
        lphi = lphi - lphi.max()
    return _fin(_rs(out))


def fault_line():
    """FAULT LINE — the Olami-Feder-Christensen earthquake model: 512 blocks on a ring, slowly
    loaded; a block that reaches the threshold slips to zero and throws 42% of its stress onto
    each neighbour, which can cascade. The cycle is the stress along the fault. Frame 0: a smooth
    stress hump. Frame axis: 0 -> ~650 quakes — the hump is carved into cliffs, terraces and a
    jagged self-organised-critical skyline."""
    N, al = 512, 0.42
    i = np.arange(N)
    sg = 0.55 + 0.4 * np.cos(2 * np.pi * i / N) + 0.03 * np.cos(10 * np.pi * i / N + 1)
    nq = np.round(650 * U ** 1.5).astype(int)
    out = np.zeros((F, N))
    done = 0
    for f in range(F):
        while done < nq[f]:
            sg = sg + (1.0 - sg.max()) + 1e-9
            for _ in range(2000):
                hot = sg >= 1.0
                if not hot.any():
                    break
                give = np.where(hot, sg, 0.0)
                sg = np.where(hot, 0.0, sg) + al * (np.roll(give, 1) + np.roll(give, -1))
            done += 1
        out[f] = sg
    return _fin(_drive(_rs(_hold(out)), 2.2))


def barkhausen_crackle():
    """BARKHAUSEN CRACKLE — the random-field Ising model's hysteresis loop: 1536 disordered
    magnetic domains driven by a field that swings up and back once per cycle; the cycle is the
    magnetisation. Frame axis: disorder R 2.4 -> 0.6, crossing the critical point: a smooth
    lagging sine becomes a crackling staircase of power-law avalanches, then a single violent
    snap — a square wave with debris on its shoulders."""
    rng = np.random.default_rng(0xBA4C)
    N = 200
    g = np.sort(rng.normal(size=N))
    R = 2.4 * (0.6 / 2.4) ** (U ** 0.85)
    M = 8192
    Hs = -2.7 * np.cos(2 * np.pi * np.arange(M) / M)
    n = np.zeros(F)
    rec = np.zeros((M, F))
    for rep in range(2):
        for k in range(M):
            up = k < M // 2
            for _ in range(400):
                m = 2 * n / N - 1
                cnt = N - np.searchsorted(g, -(Hs[k] + m) / R)
                new = np.maximum(n, cnt) if up else np.minimum(n, cnt)
                if np.array_equal(new, n):
                    break
                n = new
            rec[k] = 2 * n / N - 1
    return _fin(_rs(rec.T))


def richardson_cascade():
    """RICHARDSON CASCADE — a multiplicative turbulence cascade (the binomial p-model): each eddy
    splits into two that share its energy unevenly (p : 1-p, seeded coin flips). The cycle is the
    energy dissipated along a line through the flow, each finest-level eddy pair drawn with
    alternating sign (a Haar-signed view, so the wave is bipolar). Frame axis: cascade depth
    2.5 -> 13, intermittency p 0.62 -> 0.86 and a dark-to-bright tilt — a lopsided stepped
    square splinters into a multifractal forest of spikes."""
    rng = np.random.default_rng(0x41C4)
    Lm = 13
    coins = [rng.random(2 ** l) < 0.5 for l in range(Lm + 1)]
    out = np.zeros((F, OSN))
    D = 2.5 + 10.5 * U ** 0.8
    P = 0.62 + 0.24 * U
    for f in range(F):
        p = P[f]
        levels = []
        m = np.ones(1)
        levels.append(m)
        for l in range(Lm):
            W = np.where(coins[l], p, 1 - p)
            ch = np.empty(2 * len(m))
            ch[0::2] = m * 2 * W; ch[1::2] = m * 2 * (1 - W)
            m = ch
            levels.append(m)
        a = int(np.floor(D[f])); b = min(a + 1, Lm); fr_ = D[f] - a; w = _ss(fr_)
        mu = (1 - w) * _hold(levels[a][None])[0] + w * _hold(levels[b][None])[0]
        sg = lambda lv: np.where((np.arange(OSN) * 2 ** max(lv, 1) // OSN) % 2 == 0, 1.0, -1.0)
        ws = _ss((fr_ - 0.8) / 0.2) if b > a else 0.0     # fb638: the Haar sign pattern hands over to
        out[f] = ((1 - ws) * sg(a) + ws * sg(b)) * mu ** 0.3  # the next depth's instead of switching
    y, _ = _drive_crest(_tilt(_rs(out), -0.4 + 0.8 * U, 1000), 4.0, 1.6)   # fb638: the deep, spiky frames
    return _fin(y)                                                         # were -16..-19 dB, crest 9.6


def langton_ant():
    """LANGTON ANT — Langton's ant on an empty grid (turn right on white, left on black, flip the
    cell, step). After ~10 000 steps of chaos it builds its famous 104-step 'highway'. The cycle
    is the ant's diagonal position over a window of its walk. Frame 0: exactly one highway period
    (a strange but perfectly periodic 104-step motif). Frame axis: the window widens to 1040
    steps and slides back in time, deep into the ant's chaotic childhood."""
    Wd = 300
    grid = np.zeros((Wd, Wd), np.int8)
    x = y = Wd // 2
    d = 0
    dx = (0, 1, 0, -1); dy = (-1, 0, 1, 0)
    n = 12600
    P = np.zeros((n, 2))
    for k in range(n):
        if grid[y, x] == 0:
            d = (d + 1) % 4
        else:
            d = (d - 1) % 4
        grid[y, x] ^= 1
        x += dx[d]; y += dy[d]
        P[k] = (x, y)
    s1 = P[:, 0] + P[:, 1]; s2 = P[:, 0] - P[:, 1]
    tail = slice(11000, 12500)
    sig = s1 if np.ptp(s1[tail] - np.linspace(s1[11000], s1[12499], 1500)) < np.ptp(s2[tail] - np.linspace(s2[11000], s2[12499], 1500)) else s2
    sig = sig + 0.35 * (P[:, 0] if sig is s2 else P[:, 1])
    out = np.zeros((F, OSN))
    for f in range(F):
        Wn = 104 * int(round(1 + 9 * U[f] ** 1.1))
        end = int(12500 - 11200 * U[f] ** 1.25)
        seg = sig[end - Wn:end].astype(float)
        seg = seg - np.linspace(0, 1, Wn, endpoint=False) * (sig[end] - sig[end - Wn])
        out[f] = _hold(seg[None])[0]
    return _fin(_rs(out))


def cat_map_recurrence():
    """CAT MAP RECURRENCE — Arnold's cat map on a 45 x 45 image whose raster IS a saw. Each
    iteration shears and wraps the image; the pixels scramble into noise, and then — Poincare
    recurrence — the scramble undoes itself and the saw comes back. Frame axis: one full
    recurrence period (with crossfades between iterations): saw -> sheared stripes -> static ->
    ghosts -> saw. A deliberately seamless loop: the last frame flows into the first."""
    N = 45
    img = (1.0 - 2.0 * (np.arange(N * N) / (N * N))).reshape(N, N)
    X, Y = np.meshgrid(np.arange(N), np.arange(N), indexing='ij')
    nx, ny = (X + Y) % N, (X + 2 * Y) % N
    its = [img]
    cur = img
    for _ in range(400):
        nxt = np.empty_like(cur); nxt[nx, ny] = cur
        cur = nxt
        if np.allclose(cur, img):
            break
        its.append(cur)
    P = len(its)
    its.append(img)
    out = np.zeros((F, N * N))
    pos = U * P
    for f in range(F):
        a = int(np.floor(pos[f])); a = min(a, P); b = min(a + 1, P); w = _ss(pos[f] - a)
        out[f] = ((1 - w) * its[a] + w * its[b]).ravel()
    return _fin(_rs(_hold(out)))


# ══ ROT, GRIT AND THE LOUD ONES ═══════════════════════════════════════════════════════════════
def bit_rot():
    """BIT ROT — a triangle stored as 16-bit PCM, rotting. Every (sample, bit) has a seeded threshold;
    the rot front climbs the bit planes from the LSB, so errors accumulate instead of flickering.
    Frame axis: hiss (low bits) -> crackle (mid bits) -> the sign and top bits go: the triangle
    folds on its stuck bits and shatters into full-scale glitch splinters."""
    rng = np.random.default_rng(0xB17207)
    tt = np.arange(OSN) / OSN
    base = _rs(2 * np.abs(2 * ((tt + 0.25) % 1.0) - 1) - 1)[0]
    base = base / np.abs(base).max() * 0.92
    q = np.round(base * 32767).astype(np.int16).view(np.uint16).astype(np.uint32)
    th = rng.random((16, SIZE))
    out = np.zeros((F, SIZE))
    for f in range(F):
        u = U[f]
        mask = np.zeros(SIZE, np.uint32)
        for b in range(16):
            if b < 15:
                p = 0.5 * _ss((u * 17.0 - b * 0.95) / 2.5)
            else:
                p = 0.22 * _ss((u - 0.6) / 0.4)
            mask |= ((th[b] < p).astype(np.uint32) << b)
        stuck = np.uint32(0)
        if u > 0.45:
            stuck |= np.uint32(1 << 13)
        if u > 0.7:
            stuck |= np.uint32(1 << 14)
        v = ((q ^ mask) | stuck).astype(np.uint16).view(np.int16).astype(float) / 32768.0
        out[f] = v
    return _fin(_rs(out))


def float_rot():
    """FLOAT ROT — a sine stored as 32-bit floats, rotting from the inside: mantissa bits flip
    first (a fine fizz riding the sine), then exponent bits (samples jump by powers of two), and
    the wreckage is caught by a soft limiter. Frame axis: the rot front, sine -> fizz -> slabs
    of clipped exponent glitch, over a sub body that survives to the end.
    fb638: the rot front starts where it becomes audible (the old u = 0.56): the first exponent
    slips arrive by frame 3 instead of frame 73; frame 0 is still the clean sine."""
    rng = np.random.default_rng(0xF107)
    base = np.sin(2 * np.pi * t).astype(np.float32).view(np.uint32)
    th = rng.random((31, SIZE))
    out = np.zeros((F, SIZE))
    G = 0.56 + 0.44 * U
    for f in range(F):
        u = G[f]
        mask = np.zeros(SIZE, np.uint32)
        for b in range(31):
            if b < 23:
                p = 0.4 * _ss((u * 30.0 - b) / 5.0)
            else:
                p = 0.05 * _ss((u - 0.55 - 0.04 * (b - 23)) / 0.2)
            mask |= ((th[b] < p).astype(np.uint32) << np.uint32(b))
        with np.errstate(invalid='ignore'):
            v = (base ^ mask).view(np.float32).astype(float)
        v = np.nan_to_num(v, nan=0.0, posinf=4.0, neginf=-4.0)
        out[f] = np.tanh(0.9 * np.clip(v, -50, 50))
    return _fin(_rs(out))


def packet_loss():
    """PACKET LOSS — a bright sine-saw-square blend streamed in packets; lost packets are
    concealed by repeating the last good one. Frame axis: loss 0 -> 88% while the packets shrink
    from 1/16 to 1/128 of a cycle — dropouts, then stutter, then the stream freezes into a
    robotic buzz at the packet rate."""
    rng = np.random.default_rng(0x9AC4E7)
    tt = np.arange(OSN) / OSN
    src = np.sin(2 * np.pi * tt) + 0.22 * _saw_os() + 0.12 * np.sign(np.sin(2 * np.pi * 3 * tt + 0.3))
    th = rng.random(4096)
    out = np.zeros((F, OSN))
    for f in range(F):
        npk = int(round(16 * 8 ** U[f]))
        loss = 0.88 * U[f] ** 1.1
        edges = (np.arange(npk + 1) * OSN) // npk
        y = src.copy()
        last = None
        rep_n = 0
        for j in range(npk):
            a, b = edges[j], edges[j + 1]
            if th[(j * 7919 + npk * 31) % 4096] < loss and last is not None:
                rep_n += 1
                seg = last if rep_n % 2 else last[::-1]
                y[a:b] = seg[:b - a] if len(seg) >= b - a else np.resize(seg, b - a)
            else:
                rep_n = 0
                last = src[a:b].copy()
        out[f] = y
    return _fin(_rs(out))


def sigma_delta_fizz():
    """SIGMA DELTA FIZZ — a second-order 1-bit sigma-delta modulator encoding a sine. The output
    is only +1/-1, so the whole cycle is a chaotic pulse stream whose average is the sine and
    whose error is shoved up into the treble. Frame axis: the bit rate falls 2048 -> 24 bits per
    cycle (the noise floods down) while the input overloads into chaotic limit cycles. LOUD."""
    R = np.round(2048 * (24 / 2048) ** (U ** 0.85)).astype(int)
    A = 0.45 + 0.75 * U ** 1.6
    n = 2048 * 6
    v1 = np.zeros(F); v2 = np.zeros(F); y = np.ones(F)
    bits = np.zeros((n, F), np.int8)
    for k in range(n):
        x = A * np.sin(2 * np.pi * k / R)
        v1 = np.clip(v1 + x - y, -6, 6)
        v2 = np.clip(v2 + v1 - y, -9, 9)
        y = np.where(v2 >= 0, 1.0, -1.0)
        bits[k] = y
    out = np.zeros((F, OSN))
    for f in range(F):
        e = (n // R[f]) * R[f]
        seq = bits[e - R[f]:e, f].astype(float)
        out[f] = _hold(seq[None])[0]
    return _fin(_rs(out))


def edge_storm():
    """EDGE STORM — a square wave split into K slots, each slot a pulse whose width is the next
    value of a logistic orbit. Frame axis: K 1 -> 128 and r 3.2 -> 4: one square, then a
    period-2 PWM pair, then chaotic pulse widths in ever finer slots — a hard-edged storm with a
    formant riding at the slot rate. LOUD."""
    out = np.zeros((F, OSN))
    tt = np.arange(OSN) / OSN
    for f in range(F):
        K = int(round(2 ** (7 * U[f] ** 0.75)))
        r = 3.2 + 0.8 * U[f] ** 0.8
        xx = 0.31
        for _ in range(200):
            xx = r * xx * (1 - xx)
        w = np.empty(K)
        for j in range(K):
            xx = r * xx * (1 - xx); w[j] = 0.1 + 0.8 * xx
        slot = np.minimum((tt * K).astype(int), K - 1)
        frac = tt * K - slot
        sg = np.where(slot % 2 == 0, 1.0, -1.0) if K > 1 else 1.0
        out[f] = np.where(frac < w[slot], sg, 0.0 if K > 1 else -1.0)
    return _fin(_rs(out))


def dial_up_screech():
    """DIAL UP SCREECH — continuous-phase frequency-shift keying of a seeded bit stream, the
    handshake squeal. Frame 0: two slow symbols (a low tone then a higher one inside the cycle).
    Frame axis: the baud rate climbs 2 -> 300 symbols per cycle, the tone pair spreads apart and
    upward and the symbols gain amplitude levels — a squeal that becomes a data blizzard."""
    rng = np.random.default_rng(0xD1A1)
    bits = rng.integers(0, 4, 4096)
    tt = np.arange(OSN) / OSN
    out = np.zeros((F, OSN))
    for f in range(F):
        u = U[f]
        B = int(round(2 * 150 ** (u ** 0.9)))
        sym = bits[(tt * B).astype(int) % 4096]
        lo = 3.0 + 26.0 * u ** 1.2; hi = 6.0 + 140.0 * u ** 1.3
        fr = lo + (hi - lo) * (sym / 3.0)
        sm = max(1, int(OSN / B * 0.3))
        ker = np.ones(sm) / sm
        fr = np.convolve(np.concatenate([fr[-sm:], fr, fr[:sm]]), ker, 'same')[sm:-sm]
        tot = fr.sum() / OSN
        fr = fr * (max(1, round(tot)) / tot)
        ph = 2 * np.pi * np.cumsum(fr) / OSN
        amp = 1.0 - 0.6 * u * ((sym % 2) == 1)
        out[f] = amp * np.sin(ph)
    return _fin(_rs(out))


def lost_signal():
    """LOST SIGNAL — one composite-video scanline: sync tip, colour burst, a staircase of colour
    bars with chroma riding on them. Frame axis: the reception dies — snow rises, a ghost echo
    arrives, the chroma loses lock and smears, the picture content scrambles, and the AGC finally
    slams the whole line into clipped static."""
    rng = np.random.default_rng(0x7E1E)
    tt = np.arange(OSN) / OSN
    base = np.zeros(OSN)
    sync = tt < 0.074
    base[sync] = -0.45
    burst = (tt > 0.085) & (tt < 0.125)
    act = (tt >= 0.17) & (tt < 0.985)
    bar = np.clip(((tt - 0.17) / 0.815 * 7).astype(int), 0, 6)
    lum = np.array([0.78, 0.68, 0.58, 0.48, 0.38, 0.28, 0.18])
    chp = np.array([0.0, 2.1, 1.0, 3.9, 5.2, 0.6, 4.4])
    cha = np.array([0.0, 0.28, 0.33, 0.24, 0.24, 0.33, 0.28])
    snow = rng.normal(size=OSN)
    snow = np.convolve(np.concatenate([snow[-6:], snow, snow[:6]]), np.ones(6) / 6, 'same')[6:-6]
    blocks = rng.random(64)
    out = np.zeros((F, OSN))
    for f in range(F):
        u = U[f]
        det = 227.0 + 9.0 * u ** 1.5 * np.sin(2 * np.pi * 3 * tt)
        sub = np.sin(2 * np.pi * np.cumsum(det) / OSN)
        y = base.copy()
        y[burst] += 0.2 * sub[burst]
        scr = _ss((u - 0.35) / 0.4)
        lvl = (1 - scr) * lum[bar] + scr * blocks[(tt * 64).astype(int)]
        y[act] += lvl[act] + cha[bar][act] * np.sin(2 * np.pi * np.cumsum(det)[act] / OSN + chp[bar][act])
        ghost = np.roll(y, int(OSN * 0.045)) * 0.5 * _ss(u / 0.5)
        y = y + ghost + 1.3 * u ** 1.4 * snow
        g = 1.0 + 7.0 * _ss((u - 0.7) / 0.3)
        out[f] = np.tanh(g * y) / np.tanh(g)
    return _fin(_rs(out))


# ══ SUBBY CHAOS — a fundamental that survives while the top goes monstrous ════════════════════
def _lorenz_x(n=24000, dt=0.004, rho=28.0, seed=0.0):
    x, y, z = 1.0 + seed, 1.0, 20.0
    out = np.empty(n)
    for i in range(n):
        dx = 10.0 * (y - x); dy = x * (rho - z) - y; dz = x * y - 8.0 / 3.0 * z
        x += dt * dx; y += dt * dy; z += dt * dz
        out[i] = x
    return out


def sub_quake():
    """SUB QUAKE (subby) — a sine sub whose phase is shaken by a Lorenz trajectory. Frame axis:
    the shaking window grows from 1.5 to 60 time-units of the flow (the tremor gets faster and
    denser) and its depth grows to 1.6 rad, so the sub stays the loudest thing in the room while
    its skin turns to gravel."""
    lx = _lorenz_x(30000, 0.004)[2000:]
    lx = lx / np.abs(lx).max()
    tt = np.arange(OSN) / OSN
    out = np.zeros((F, OSN))
    for f in range(F):
        L = int((1.5 + 58.5 * U[f] ** 1.4) / 0.004)
        L = min(L, len(lx) - 400)
        m = _close(lx[:L + int(0.05 * L)], L, int(0.05 * L))
        m = np.interp(tt, np.arange(L) / L, m)
        beta = 0.08 + 1.55 * U[f] ** 0.9
        out[f] = np.sin(2 * np.pi * tt + beta * m) * (1 + 0.3 * U[f] * m)
    return _fin(_rs(out))


def sub_sizzle():
    """SUB SIZZLE (subby) — a sub (h1 + a little h2) with a seeded, slowly boiling noise band
    above a spectral gap. Frame axis: the band's floor falls from harmonic 420 to harmonic 5,
    closing the gap, while its level rises from -46 dB to -8 dB against the sub: a clean 808
    swallowed by sizzle from above."""
    rng = np.random.default_rng(0x5122)
    base = rng.rayleigh(1.0, NH)
    walk = np.cumsum(rng.normal(0, 0.12, (F, NH)), axis=0)
    mags = np.zeros((F, NH))
    for f in range(F):
        u = U[f]
        lo = 420.0 * (5.0 / 420.0) ** (u ** 0.8)
        edge = 1 / (1 + np.exp(-(np.log2(n_ax) - np.log2(lo)) * 6))
        lvl = 10 ** ((-46 + 38 * u ** 0.9) / 20)
        band = lvl * base * np.exp(0.5 * walk[f]) / (n_ax / lo) ** 0.35 * edge
        m = band
        m[0] += 1.0; m[1] += 0.22; m[2] += 0.05
        mags[f] = m
    return _spec(_fsm(mags, 3))


def sub_rattle():
    """SUB RATTLE (subby) — a sine sub with snare wires: narrow alternating clicks at times drawn
    from a tent-map orbit. Frame axis: the clicks multiply 1 -> 48 per cycle and grow from a tick
    to 80% of the sub's level: an 808 with a loose, chaotic rattle on top."""
    tt = np.arange(OSN) / OSN
    out = np.zeros((F, OSN))
    xx = 0.3141
    orb = np.empty(64)
    for j in range(64):
        xx = 1.9999 * min(xx, 1 - xx) if j else xx
        orb[j] = xx
    for f in range(F):
        u = U[f]
        K = int(round(1 + 47 * u ** 1.3))
        y = np.sin(2 * np.pi * tt)
        w = 0.0009 + 0.002 * (1 - u)
        amp = 0.12 + 0.68 * u ** 1.1
        for j in range(K):
            c = orb[j]
            d = (tt - c + 0.5) % 1.0 - 0.5
            y = y + amp * ((-1) ** j) * np.exp(-0.5 * (d / w) ** 2) * (1 - 2 * (d / w) ** 2) * (0.6 + 0.4 * orb[(j + 7) % 64])
        out[f] = y
    return _fin(_rs(out))


def sub_telegraph():
    """SUB TELEGRAPH (subby) — a sine sub keyed against the Lorenz flow's random telegraph (its
    lobe-switching, hard-limited). Frame axis: the telegraph speeds up (2 -> 40 switches per
    cycle) and grows to 70% of the sub: a round sub that ends up chewing on a stuck-key buzz."""
    lx = _lorenz_x(40000, 0.004)[2000:]
    tele = np.tanh(6 * lx / lx.std())
    tt = np.arange(OSN) / OSN
    out = np.zeros((F, OSN))
    for f in range(F):
        L = int((3.0 + 110.0 * U[f] ** 1.3) / 0.004)
        L = min(L, len(tele) - 800)
        m = _close(tele[:L + int(0.04 * L)], L, int(0.04 * L))
        m = np.interp(tt, np.arange(L) / L, m)
        out[f] = np.sin(2 * np.pi * tt) + (0.2 + 0.6 * U[f] ** 1.1) * m
    return _fin(_tilt(_rs(out), 0.6 * U, 1000))


def sub_vinyl():
    """SUB VINYL (subby) — a sine sub played off a wrecked record: every crackle is a tiny ringing
    burst (a click exciting the stylus resonance, harmonic 300-900), with power-law loudness, at
    seeded positions. Frame axis: the crackles multiply 1 -> 90 per cycle and grow from a dusty
    tick to a torn-groove roar riding on a sub that never lets go."""
    rng = np.random.default_rng(0xC4AC)
    pos = rng.random(128)
    hz = np.exp(rng.uniform(np.log(300), np.log(900), 128))
    tau = rng.uniform(0.0015, 0.005, 128)
    amp = np.clip(rng.pareto(1.6, 128) + 0.25, 0, 6) * rng.choice([-1, 1], 128)
    tt = np.arange(OSN) / OSN
    out = np.zeros((F, OSN))
    for f in range(F):
        u = U[f]
        K = int(round(1 + 89 * u ** 1.3))
        cr = np.zeros(OSN)
        for j in range(K):
            d = (tt - pos[j]) % 1.0
            m = d < 10 * tau[j]
            cr[m] += amp[j] * np.exp(-d[m] / tau[j]) * np.sin(2 * np.pi * hz[j] * d[m])
        cr = cr / (np.abs(cr).max() + 1e-12)
        out[f] = np.sin(2 * np.pi * tt) + (0.1 + 0.62 * u ** 1.1) * cr
    return _fin(_rs(out))


def subharmonic_cascade():
    """SUBHARMONIC CASCADE — a saw three octaves up, whose 8 cycles take their amplitudes from a
    logistic orbit. Frame axis: r walks the period-doubling cascade (3.0 -> 3.449 -> 3.544 ->
    3.5644 -> chaos -> 4): at period 1 it is a clean high saw, each doubling drops a new
    subharmonic an octave lower until the fundamental itself is born out of chaos."""
    kp = np.array([0.0, 0.18, 0.4, 0.55, 0.68, 1.0])
    rp = np.array([2.95, 3.3, 3.5, 3.555, 3.5695, 3.99])
    r = np.interp(U, kp, rp)
    tt = np.arange(OSN) / OSN
    cyc = np.minimum((tt * 8).astype(int), 7)
    saw = 1 - 2 * ((tt * 8) % 1.0)
    out = np.zeros((F, OSN))
    for f in range(F):
        xx = 0.41
        for _ in range(3000):
            xx = r[f] * xx * (1 - xx)
        a = np.empty(8)
        for j in range(8):
            xx = r[f] * xx * (1 - xx); a[j] = xx
        out[f] = (0.15 + a[cyc]) * saw
    return _fin(_tilt(_rs(out), -0.4 + 0.9 * U, 1000))


# ══ MORE MAPS, FLOWS AND NUMBERS ══════════════════════════════════════════════════════════════
def feigenbaum_organ():
    """FEIGENBAUM ORGAN — the logistic orbit x_1, x_2, ... x_1024 (no warm-up) sets the amplitude of
    harmonic 1, 2, ... 1024. Frame axis: r through the cascade: one level for every harmonic (an
    organ's saw), period 2 (odd and even harmonics part company), period 4 and 8 (the drawbars
    lock into a repeating pattern), then chaos throws the stops about at random."""
    kp = np.array([0.0, 0.15, 0.35, 0.5, 0.6, 1.0])
    rp = np.array([2.8, 3.2, 3.5, 3.556, 3.57, 4.0])
    r = np.interp(U, kp, rp)
    mags = np.zeros((F, NH))
    for f in range(F):
        xx = 0.23
        xs = np.empty(NH)
        for j in range(NH):
            xx = r[f] * xx * (1 - xx); xs[j] = xx
        mags[f] = (0.004 + xs) ** 5.0 / n_ax ** (1.15 - 0.85 * U[f])
    return _spec(mags)


def firefly_desync():
    """FIREFLY DESYNC — 24 pulse-coupled fireflies (Mirollo-Strogatz: each flash nudges the others'
    clocks forward). Frame 0: strong coupling, near-identical clocks — the swarm flashes as one,
    a single bump per cycle. Frame axis: coupling fades and the clocks spread; the flash splits
    into clusters, then 24 independent flickers scattered through the cycle."""
    rng = np.random.default_rng(0xF1EF)
    Nf = 24
    gi = rng.normal(0, 1, Nf)
    eps = 0.16 * (1 - U) ** 1.4
    spread = 0.002 + 0.09 * U ** 1.1
    om = 1.0 + spread[:, None] * gi[None, :]
    ph = rng.random((F, Nf))
    dt = 1.0 / 400
    nstep = 400 * 36
    fires = [[] for _ in range(F)]
    tnow = 0.0
    for k in range(nstep):
        ph = ph + om * dt
        tnow += dt
        fired = ph >= 1.0
        if fired.any():
            cnt = fired.sum(axis=1)
            ph = np.where(fired, ph - 1.0, ph)
            kick = eps[:, None] * cnt[:, None] * (0.3 + ph)
            ph = np.where(fired, ph, ph + kick)
            again = ph >= 1.0
            ph = np.where(again, 0.0, ph)
            if tnow > 35.0:
                for f in np.where(cnt > 0)[0]:
                    for j in np.where(fired[f] | again[f])[0]:
                        fires[f].append(tnow % 1.0)
    tt = np.arange(OSN) / OSN
    out = np.zeros((F, OSN))
    for f in range(F):
        w = 0.06 * (1 - U[f]) + 0.006
        y = np.zeros(OSN)
        for c in fires[f]:
            d = (tt - c) % 1.0
            y += (d / w * np.exp(1 - d / w) - 0.55 * (d / (2.6 * w)) * np.exp(1 - d / (2.6 * w))) * (d < 30 * w)
        out[f] = y
    out = out - out.mean(axis=1, keepdims=True)
    return _fin(_tilt(_rs(out), -0.4 + 0.9 * U, 1000))


def arrhythmia():
    """ARRHYTHMIA — a heartbeat: P wave, QRS spike, T wave (sums of Gaussians, one beat per cycle
    at frame 0 — a trace everyone recognises). Frame axis: the rate climbs to 5 beats per cycle
    with beat intervals from a chaotic circle map, T-waves alternate, beats drop, and in the last
    quarter the rhythm collapses into fibrillation: coarse chaotic waves."""
    Pw = [(-0.22, 0.03, 0.14), (-0.035, 0.009, -0.12), (0.0, 0.012, 0.75), (0.035, 0.01, -0.3), (0.26, 0.05, 0.32)]
    tt = np.arange(OSN) / OSN
    lx = _lorenz_x(20000, 0.004, seed=0.3)[1000:]
    out = np.zeros((F, OSN))
    for f in range(F):
        u = U[f]
        nb = 1.0 + 4.0 * u ** 1.3
        K = max(1, int(round(nb)))
        th = 0.137 + 0.01 * f
        iv = np.empty(K)
        for j in range(K):
            th = th + 0.29 + (0.2 + 1.8 * u) / (2 * np.pi) * math.sin(2 * np.pi * th)
            iv[j] = 1.0 + 0.9 * u * ((th % 1.0) - 0.5)
        iv = iv / iv.sum()
        times = np.concatenate([[0.0], np.cumsum(iv)[:-1]])
        amps = [1.0 - (0.35 * u if j % 2 else 0.0) for j in range(K)]
        y = np.zeros(OSN)
        RR = 1.0 / max(nb, 1.0)
        for tk, ak in zip(times, amps):
            for (c, w, a) in Pw:
                sc = RR * (1 + 0.8 * u) if c > 0.1 else RR
                d = ((tt - tk - c * sc + 0.5) % 1.0) - 0.5
                y += ak * a * np.exp(-0.5 * (d / (w * RR)) ** 2) * (1.0 if c <= 0.1 else (1 - 0.5 * u * (tk > 0)))
        fib = _ss((u - 0.72) / 0.28)
        if fib > 0:
            L = 3000
            m = np.interp(tt, np.arange(L) / L, _close(lx[f * 50: f * 50 + L + 200], L, 200))
            y = (1 - fib) * y + fib * 0.8 * m / (np.abs(m).max() + 1e-9)
        out[f] = y
    return _fin(_drive(_rs(out), 1.5))


def soliton_train():
    """SOLITON TRAIN — the Korteweg-de Vries equation u_t + u u_x + 0.022^2 u_xxx = 0 (Zabusky and
    Kruskal's 1965 experiment, the birth of the soliton). Frame 0: a cosine. Frame axis: time —
    the cosine steepens toward a shock, the shock sheds ripples, and the ripples condense into a
    train of eight solitons that overtake and pass through each other."""
    N = 512
    x = np.arange(N) / N * 2.0
    u = np.cos(np.pi * x)
    k = np.pi * np.fft.rfftfreq(N, d=1.0 / N)
    dl = 0.022 ** 2
    dt = 3e-4
    times = 1.55 * U ** 1.15
    out = np.zeros((F, N))
    v = np.fft.rfft(u)
    tn = 0.0
    lin = 1j * dl * k ** 3
    def nl(vh, tn):
        uu = np.fft.irfft(vh * np.exp(lin * tn), n=N)
        return -0.5j * k * np.fft.rfft(uu * uu) * np.exp(-lin * tn)
    w = v.copy()
    for f in range(F):
        while tn < times[f] - 1e-12:
            k1 = nl(w, tn); k2 = nl(w + 0.5 * dt * k1, tn + 0.5 * dt)
            k3 = nl(w + 0.5 * dt * k2, tn + 0.5 * dt); k4 = nl(w + dt * k3, tn + dt)
            w = w + dt / 6 * (k1 + 2 * k2 + 2 * k3 + k4)
            tn += dt
        out[f] = np.fft.irfft(w * np.exp(lin * tn), n=N)
    return _fin(_tilt(_rs(out), 0.7 + 0.9 * U, 1000))


def swinging_spring():
    """SWINGING SPRING — an elastic pendulum tuned to the 2:1 resonance (the bounce is twice the
    swing frequency), started bouncing with a hair of swing. Frame axis: the launch energy grows:
    a pure bounce, then the famous energy exchange (bounce turns into swing and back), then the
    stepwise precession and finally chaotic tumbling. Cycle = 1 -> 8 bounce periods of the
    radial stretch plus the swing."""
    g, L0 = 1.0, 1.0
    kk = 4.0 * g / L0
    A = 0.03 + 0.55 * U ** 1.1

    def f(s):
        r, th, vr, vth = s
        return np.array([vr, vth, r * vth * vth + g * np.cos(th) - kk * (r - L0 - g / kk),
                         (-g * np.sin(th) - 2 * vr * vth) / r])
    s = np.array([L0 + g / kk + A, 0.02 + 0.1 * A, np.zeros(F), np.zeros(F)])
    Tb = 2 * np.pi / math.sqrt(kk)
    dt = Tb / 400
    n = int(8.6 * Tb / dt)
    s, rec = _rk4(f, s, dt, n, rec=lambda s: np.concatenate([s[0], s[0] * np.sin(s[1])]))
    Rr, Xx = rec[:F], rec[F:]
    out = np.zeros((F, SIZE))
    for i in range(F):
        L = int((1 + 7 * U[i] ** 1.2) * 400)
        xf = int(0.05 * L)
        sig = (Rr[i] - Rr[i].mean()) / (Rr[i].std() + 1e-9) + 1.2 * Xx[i] / (Xx[i].std() + 1e-9) * U[i]
        out[i] = _rs(_close(sig, L, xf)[None])[0]
    return _fin(_tilt(_drive(out, 1.0 + 5.0 * U ** 1.5), 0.3 + 0.8 * U, 1000))


def josephson_junction():
    """JOSEPHSON JUNCTION — the RCSJ model of a superconducting junction, phi'' + 0.25 phi' +
    sin(phi) = 0.2 + i_ac sin(0.66 s): a driven pendulum whose phase can slip. The cycle is one
    drive period of the supercurrent sin(phi) plus the voltage phi'. Frame axis: i_ac 0.1 -> 2.6:
    a small wobble, phase-locked slips (Shapiro steps: clean harmonic combs), then chaotic
    slipping."""
    Om = 0.66
    P = 2 * np.pi / Om
    iac = 0.1 + 2.5 * U ** 1.1

    def f(s):
        ph, v, c = s
        return np.array([v, -0.25 * v - np.sin(ph) + 0.2 + iac * np.sin(Om * c), np.ones_like(c)])
    s = np.array([np.zeros(F), np.zeros(F), np.zeros(F)])
    s, _ = _rk4(f, s, P / 256, 256 * 40)
    s[2] = 0.0
    s, rec = _rk4(f, s, P / 2048, 2048 * 2 + 300, rec=lambda s: np.concatenate([np.sin(s[0]), s[1]]))
    Sn, V = rec[:F], rec[F:]
    out = np.zeros((F, SIZE))
    for i in range(F):
        L = 2048 * (1 if U[i] < 0.5 else 2)
        sig = Sn[i] + 0.35 * V[i] / (np.abs(V[i]).max() + 1e-9)
        out[i] = _rs(_close(sig, L, 300)[None])[0]
    return _fin(_tilt(_drive(out, 1.0 + 3.0 * U), 0.6 + 1.0 * U, 1000))


def hailstone():
    """HAILSTONE — Collatz trajectories (n -> n/2 if even, 3n+1 if odd) drawn as log2 altitude
    over one cycle. Frame axis: starting numbers chosen in order of flight length, from 7 (a
    short hop: 16 steps) to 837 799 (524 steps of turbulence), so a lopsided ramp becomes a
    storm-tossed mountain range."""
    def traj(n):
        s = [n]
        while n != 1 and len(s) < 2000:
            n = n // 2 if n % 2 == 0 else 3 * n + 1
            s.append(n)
        return np.log2(np.array(s, float))
    cand = list(range(3, 4000)) + [6171, 10971, 13255, 17647, 23529, 26623, 34239, 35655, 52527, 77031,
                                      106239, 142587, 156159, 216367, 230631, 410011, 511935, 626331, 837799]
    lens = {}
    for n in cand:
        lens[n] = len(traj(n))
    order = sorted(set(lens.values()))
    targets = np.round(np.interp(U, [0, 1], [min(order), max(order)])).astype(int)
    out = np.zeros((F, OSN))
    tt = np.arange(OSN) / OSN
    for f in range(F):
        best = min(cand, key=lambda n: (abs(lens[n] - targets[f]), n))
        v = traj(best)
        L = len(v)
        vv = np.interp(tt * L, np.arange(L + 1), np.append(v, v[0]))
        out[f] = vv
    return _fin(_rs(out))


def mandelbrot_rim():
    """MANDELBROT RIM — smooth escape time of c -> z^2 + c, sampled round a circle in the
    c-plane centred on the neck between the main cardioid and the period-2 bulb. Frame 0: a wide
    circle far outside the set (a smooth lopsided sine). Frame axis: the circle shrinks onto the
    rim, and the wave picks up the set's filaments, spirals and baby Mandelbrots as spikes."""
    M = 8192
    th = 2 * np.pi * np.arange(M) / M
    R = 1.9 * (0.1 / 1.9) ** (U ** 0.8)
    ctr = -0.75 + 0.06j + 0.05j * U
    c = ctr[:, None] + R[:, None] * np.exp(1j * th)[None, :]
    z = np.zeros_like(c)
    nu = np.full(c.shape, 60.0)
    alive = np.ones(c.shape, bool)
    for k in range(60):
        z = np.where(alive, z * z + c, z)
        az = np.abs(z)
        esc = alive & (az > 64.0)
        nu[esc] = k + 1 - np.log2(np.log(az[esc]))
        alive &= ~esc
    v = np.log1p(nu)
    return _fin(_rs(v))


def circle_map_warp():
    """CIRCLE MAP WARP — a sine read through the circle map's lift iterated five times,
    y = sin(2 pi F^5(t)) with F(x) = x + 0.2 - (K / 2 pi) sin(2 pi x). The lift carries a cycle onto
    itself, so every frame loops exactly. Frame axis: K 0 -> 2.4. Below K = 1 the map is a smooth
    warp (a sine leaning and bunching); past it the map folds back on itself and the sine is
    traversed forward, backward and forward again: mode-locked pockets and chaotic fold-storms."""
    tt = np.arange(OSN) / OSN
    K = 2.4 * U ** 1.15
    Om = 0.2 + 0.1 * U
    th = np.tile(tt, (F, 1))
    for _ in range(5):
        th = th + Om[:, None] - (K[:, None] / (2 * np.pi)) * np.sin(2 * np.pi * th)
    return _fin(_rs(np.sin(2 * np.pi * th) + 0.25 * np.sin(6 * np.pi * th)))


def delay_loop_howl():
    """DELAY LOOP HOWL — a delayed-feedback oscillator x' = -x - mu sin(x(t - 8) + 0.3), the
    optoelectronic 'Ikeda delay' circuit: a sluggish amplifier listening to its own output eight
    time-units late. Frame axis: loop gain mu 1.2 -> 4.4. It first howls as a square wave (the
    delay's period-2 flip), the plateaus then split into staircases (the delay map doubling) and
    finally boil into delay-chaos. 1 -> 6 periods per frame. LOUD."""
    mu = 1.2 + 3.2 * U ** 1.1
    dt, tau = 0.02, 8.0
    D = int(tau / dt)
    B = D + 2
    hist = np.full((F, B), 0.3)
    x = np.full(F, 0.3)
    n_set, n_rec = 16000, 22000
    rec = np.empty((n_rec // 2, F))
    for i in range(n_set + n_rec):
        xd = hist[:, (i - D) % B]
        x = x + dt * (-x - mu * np.sin(xd + 0.3))
        hist[:, i % B] = x
        if i >= n_set and (i - n_set) % 2 == 0:
            rec[(i - n_set) // 2] = x
    fr = _cycles(rec.T, np.round(1 + 5 * U ** 1.3))
    return _fin(_tilt(fr, 0.5 + 1.0 * U, 1000))


def dynamo_reversals():
    """DYNAMO REVERSALS (subby) — Rikitake's two-disc dynamo x' = -2x + yz, y' = -2y + (z - a)x,
    z' = 1 - xy, a toy of the Earth's magnetic field. The current swings in growing oscillations
    around one polarity, then flips. Frame axis: a 0.6 -> 6 and the window grows 1 -> 9
    reversals: a heavy lopsided swing whose field keeps reversing in ever wilder bursts."""
    a = 0.6 + 5.4 * U ** 0.9

    def f(s):
        x, y, z = s
        return np.array([-2 * x + y * z, -2 * y + (z - a) * x, 1 - x * y])
    s = np.array([np.full(F, 1.0), np.full(F, 0.5), np.full(F, 0.2)])
    s, _ = _rk4(f, s, 0.01, 6000)
    s, rec = _rk4(f, s, 0.01, 30000, rec=lambda s: s[0].copy(), every=2)
    fr = _cycles(rec, np.round(1 + 8 * U ** 1.2))
    return _fin(_tilt(_drive(fr, 1.6 + 1.5 * U), 0.3 + 0.8 * U, 1000))


def chaos_game_dust():
    """CHAOS GAME DUST — the chaos game on a pentagon: jump a fraction r of the way toward a random
    vertex, forever (78 125 points per frame, built exactly as the IFS to depth 7). The cycle is
    the cloud's SHADOW along a slowly turning direction. Frame axis: r 0.64 -> 0.3 — an overlapped
    smooth blob (one round hump) is torn into a pentaflake and then into Cantor dust with
    razor gaps."""
    ang = 2 * np.pi * np.arange(5) / 5 + 0.3
    V = np.stack([np.cos(ang), np.sin(ang)], axis=1)
    r = 0.64 - 0.34 * U ** 0.9
    phi = 0.25 + 1.1 * U
    out = np.zeros((F, SIZE))
    for f in range(F):
        P = np.zeros((1, 2))
        for _ in range(7):
            P = (r[f] * P[None, :, :] + (1 - r[f]) * V[:, None, :]).reshape(-1, 2)
        pr = P[:, 0] * np.cos(phi[f]) + P[:, 1] * np.sin(phi[f])
        h = np.bincount(np.clip(((pr + 1.05) / 2.1 * SIZE).astype(int), 0, SIZE - 1), minlength=SIZE)
        out[f] = h.astype(float) ** 0.6
    return _fin(_rs(out))


def edge_of_chaos():
    """EDGE OF CHAOS — Langton's lambda experiment: a 4-state cellular automaton whose rule table is
    filled in, one seeded entry at a time, with active states. The cycle is each cell's activity
    over 128 generations (a space-time census) around a 256-cell ring seeded with one block.
    Frame axis: lambda 0.1 -> 0.75 — the block dies (a clean pulse), then grows frozen crystals,
    then complex gliders at the edge of chaos, then a chaotic light-cone that floods the ring."""
    rng = np.random.default_rng(0xED6E)
    order = rng.permutation(np.arange(1, 64))
    vals = rng.integers(1, 4, 64)
    N = 256
    init = np.zeros(N, int)
    init[112:144] = rng.integers(1, 4, 32)
    lam = 0.1 + 0.65 * U ** 0.9
    out = np.zeros((F, N))
    for f in range(F):
        tab = np.zeros(64, int)
        na = int(round(lam[f] * 63))
        tab[order[:na]] = vals[order[:na]]
        c = init.copy()
        act = np.zeros(N)
        for g in range(128):
            act += (c > 0) + 0.15 * c
            c = tab[16 * np.roll(c, 1) + 4 * c + np.roll(c, -1)]
        out[f] = np.sqrt(act + 2.0)
    return _fin(_drive(_rs(_hold(out)), 1.8))


def energy_cascade():
    """ENERGY CASCADE — the Sabra shell model of turbulence: 15 complex shell velocities, each an
    octave of eddy size, forced at the largest scale and coupled only to their neighbours. Shell
    energies are laid along the log-frequency axis as the spectrum. Frame 0: all the energy in
    the forced shells (a dark hum). Frame axis: time — energy avalanches down the octaves in
    intermittent bursts until the whole Kolmogorov cascade is lit, flickering."""
    rng = np.random.default_rng(0x5AB2A)
    Ns = 15
    k = 2.0 ** np.arange(Ns) * 0.125
    nu = 1e-5
    u = np.zeros(Ns, complex); u[0] = 0.4; u[1] = 0.2j
    u[2:] = 1e-6 * (rng.normal(size=Ns - 2) + 1j * rng.normal(size=Ns - 2))
    fo = np.zeros(Ns, complex); fo[0] = 0.05 * (1 + 1j)
    dt = 2e-3
    visc = np.exp(-nu * k * k * dt)

    def rhs(u):
        up = np.concatenate([u, [0, 0]]); um = np.concatenate([[0, 0], u])
        t1 = k * np.conj(up[1:Ns + 1]) * up[2:Ns + 2]
        t2 = -0.5 * np.concatenate([[0], k[:-1]]) * np.conj(um[1:Ns + 1]) * up[1:Ns + 1]
        t3 = 0.5 * np.concatenate([[0, 0], k[:-2]]) * um[1:Ns + 1] * um[0:Ns]
        return 1j * (t1 * np.concatenate([[1.0], np.ones(Ns - 1)]) + t2 + t3) * 1.0 + fo
    times = np.round(30000 * U ** 1.3).astype(int)
    rough = np.exp(0.35 * rng.normal(size=NH))
    lg = np.log2(n_ax)
    pos = np.arange(Ns) * (10.0 / (Ns - 1))
    mags = np.zeros((F, NH))
    st = 0
    for f in range(F):
        while st < times[f]:
            k1 = rhs(u); k2 = rhs(u + 0.5 * dt * k1)
            u = (u + dt * k2) * visc
            u = np.nan_to_num(u, nan=0.0, posinf=0.0, neginf=0.0)
            au = np.abs(u)
            u = np.where(au > 5.0, 5.0 * u / np.maximum(au, 1e-12), u)
            st += 1
        e = np.log(np.abs(u) + 1e-9) + np.log(k) / 3.0
        prof = np.interp(lg, pos, e)
        mags[f] = np.exp(prof - prof.max()) * rough / n_ax ** 0.25
    return _spec(_fsm(mags, 2))


def buffer_overrun():
    """BUFFER OVERRUN — a playback bug: the oscillator's read pointer strides through memory at the
    wrong step. Memory holds the sine it should play, then a saw, a staircase, seeded garbage
    bytes and a square. Frame axis: the stride drifts from 1 (a clean sine) to 1.4 (the cycle
    runs into the saw and the stairs), 3, 17 and finally 509.3: every read lands somewhere
    else and the cycle is shrapnel from the whole heap."""
    rng = np.random.default_rng(0xB0F)
    tt = np.arange(SIZE) / SIZE
    mem = np.concatenate([np.sin(2 * np.pi * tt), 1 - 2 * tt, np.floor(tt * 7) / 3.5 - 1,
                          rng.integers(32, 123, SIZE) / 61.0 - 1.5, np.sign(np.sin(2 * np.pi * 3 * tt)),
                          np.sin(2 * np.pi * 5 * tt) * (1 - tt)])
    M = len(mem)
    stride = np.exp(np.log(509.3) * U ** 1.6)
    out = np.zeros((F, SIZE))
    for f in range(F):
        idx = np.floor(np.arange(SIZE) * stride[f]).astype(np.int64) % M
        out[f] = mem[idx]
    return _fin(_rs(_hold(out)))


def lorenz_vowels():
    """LORENZ VOWELS — four formants steered by the Lorenz flow: F1 by x, F2 by y, F3 by z, F4 by
    x*y. Frame axis: time along one trajectory while rho climbs 14 -> 70: below the chaos
    threshold the flow sinks to a fixed point and the throat holds a steady vowel; past it the
    formants are flung around the butterfly's two wings — a voice possessed by weather."""
    n = 16000
    dt = 0.0025
    x, y, z = 1.0, 1.0, 12.0
    X = np.empty(n); Y = np.empty(n); Z = np.empty(n)
    for i in range(n):
        rho = 14.0 + 56.0 * (i / n) ** 1.4
        for _ in range(2):
            dx = 10.0 * (y - x); dy = x * (rho - z) - y; dz = x * y - 8.0 / 3.0 * z
            x += dt * dx; y += dt * dy; z += dt * dz
        X[i] = x; Y[i] = y; Z[i] = z
    idx = np.linspace(0, n - 1, F).astype(int)
    xs, ys, zs = X[idx] / 30.0, Y[idx] / 40.0, Z[idx] / 90.0
    mags = np.zeros((F, NH))
    for f in range(F):
        u = U[f]
        F1 = 5.0 * 2.2 ** np.clip(xs[f] * 1.6, -2.5, 2.5)
        F2 = 22.0 * 2.0 ** np.clip(ys[f] * 1.8, -2.5, 2.5)
        F3 = 70.0 * 2.2 ** np.clip((zs[f] - 0.3) * 3.0, -2.5, 2.5)
        F4 = 260.0 * 2.0 ** np.clip(xs[f] * ys[f] * 2.5, -2.0, 2.0)
        wdt = 3.0 + 4.0 * u
        m = (_bump(F1, wdt) + 0.8 * _bump(F2, wdt + 1) + 0.6 * _bump(F3, wdt + 2) + 0.45 * _bump(F4, wdt + 3))
        mags[f] = m / n_ax ** 0.25 + 0.02 / n_ax ** (1.2 - 0.6 * u)
    return _spec(_fsm(mags, 2))


def _bump(center, width_st, amp=1.0):
    lw = max(width_st, 0.5) / 12.0
    return amp * np.exp(-0.5 * ((np.log2(n_ax) - math.log2(max(center, 1.0))) / lw) ** 2)


def avalanche_spectrum():
    """AVALANCHE SPECTRUM — the Oslo rice-pile (a 1-D self-organised-critical sandpile with random
    slope thresholds) laid along the log-frequency axis: pile height is spectral level, grains
    are dropped at the bass end. Frame axis: grains 20 -> 7000. The pile creeps up the spectrum in
    avalanches of every size, its slope a staircase of terraces that collapse and regrow; the
    critical-slope field is stamped on top as a ragged comb."""
    rng = np.random.default_rng(0x051A)
    L = 72
    h = np.zeros(L + 1, int)
    zc = rng.integers(1, 3, L)
    tot = np.round(20 + 6980 * U ** 1.3).astype(int)
    lg = np.log2(n_ax) / 10.0 * (L - 1)
    rough = np.exp(0.25 * rng.normal(size=NH))
    mags = np.zeros((F, NH))
    have = 0
    for f in range(F):
        while have < tot[f]:
            h[0] += 1
            have += 1
            moved = True
            while moved:
                moved = False
                zz = h[:-1] - h[1:]
                for i in np.where(zz[:L] > zc)[0]:
                    if h[i] - h[i + 1] > zc[i]:
                        h[i] -= 1; h[i + 1] += 1
                        zc[i] = rng.integers(1, 3)
                        moved = True
                h[L] = 0
        hs = h[:L].astype(float)
        slope = np.maximum(h[:L] - h[1:L + 1], 0).astype(float)
        prof = np.interp(lg, np.arange(L), hs) / (1.8 * L) * 9.0
        sl = np.interp(lg, np.arange(L), slope)
        mags[f] = np.exp(prof) * (0.35 + 0.65 * sl / 2.0) * rough / n_ax ** 0.6
    return _spec(_fsm(mags, 2))


def logistic_grains():
    """LOGISTIC GRAINS — windowed sine grains whose positions and pitches are successive values of
    a logistic orbit. Frame axis: r 3.3 -> 3.99 and 1 -> 40 grains per cycle, grains shrinking:
    a single blip, a period-2 pair (two grains, two pitches), a period-4 quartet, then a chaotic
    swarm of grains at every pitch."""
    tt = np.arange(OSN) / OSN
    out = np.zeros((F, OSN))
    for f in range(F):
        u = U[f]
        r = 3.3 + 0.69 * u ** 0.8
        K = int(round(1 + 39 * u ** 1.4))
        xx = 0.37
        for _ in range(300):
            xx = r * xx * (1 - xx)
        y = np.zeros(OSN)
        sig = 0.05 * (1 - u) + 0.006
        for j in range(K):
            xx = r * xx * (1 - xx); c = xx
            xx = r * xx * (1 - xx); hh = 3.0 + 90.0 * xx ** 2
            d = (tt - c + 0.5) % 1.0 - 0.5
            y += ((-1) ** j) * np.exp(-0.5 * (d / sig) ** 2) * np.sin(2 * np.pi * hh * d)
        out[f] = y
    return _fin(_drive(_tilt(_rs(out), 0.3 + 0.7 * U, 1000), 1.6))


def traffic_jam():
    """TRAFFIC JAM — the Nagel-Schreckenberg traffic automaton: 150 cars on a 600-cell ring road,
    speed limit 5. The cycle is the HEADWAY (gap to the car in front) along the road. Frame 0:
    evenly spaced traffic with one hesitant platoon bunched up (a single dip). Frame axis: time, while the drivers' random braking rises
    0 -> 0.4 — the dip runs backwards round the ring as a kinematic wave, then seeds stop-and-go
    waves that split and merge into phantom jams."""
    rng = np.random.default_rng(0x7AFF1C)
    L, nc, vmax = 600, 150, 5
    pos = np.arange(nc) * (L // nc)
    v = np.full(nc, 3)
    v[70:82] = 0
    pb = 0.4 * U ** 1.2
    tt = np.arange(OSN) / OSN
    out = np.zeros((F, OSN))
    for f in range(F):
        order = np.argsort(pos)
        pp = pos[order].astype(float); vv = v[order].astype(float)
        gp = ((np.roll(pp, -1) - pp) % L).astype(float)
        out[f] = np.interp(tt * L, np.append(pp, pp[0] + L), np.append(gp, gp[0]), period=L)
        for _ in range(3):
            gap = (np.roll(pos, -1) - pos - 1) % L
            v = np.minimum(np.minimum(v + 1, vmax), gap)
            v = np.where(rng.random(nc) < pb[f], np.maximum(v - 1, 0), v)
            pos = (pos + v) % L
    return _fin(_drive(_tilt(_rs(out), -0.3 + 0.9 * U, 1000), 1.4))


def periodic_windows():
    """PERIODIC WINDOWS — the logistic map's Lyapunov exponent lambda(r) used as a SPECTRUM: the
    harmonic axis (log-spaced) is a window onto the rate r, and each harmonic's level is
    exp(3 lambda), over a held fundamental. Chaos is loud; every periodic window of the bifurcation diagram cuts a
    notch and every superstable point a razor. Frame axis: the r window zooms from the whole
    route [2.9, 4.0] into the period-3 window and its own cascade: a self-similar notch comb."""
    lo = 2.9 + (3.8260 - 2.9) * _ss(U ** 0.8)
    hi = 4.0 + (3.8575 - 4.0) * _ss(U ** 0.8)
    frac = np.log2(n_ax) / 10.0
    r = lo[:, None] + (hi - lo)[:, None] * frac[None, :]
    xx = np.full(r.shape, 0.4)
    lam = np.zeros(r.shape)
    for k in range(420):
        xx = r * xx * (1 - xx)
        if k >= 120:
            lam += np.log(np.abs(r * (1 - 2 * xx)) + 1e-12)
    lam /= 300.0
    mags = np.exp(3.0 * np.clip(lam, -3.0, 0.8)) / n_ax ** 0.45
    mags[:, 0] += 1.5; mags[:, 1] += 0.375
    return _spec(_fsm(mags, 2))


def _logistic_orbit(r, n, warm=1500, x0=0.3):
    x = np.full(len(r), x0)
    for _ in range(warm):
        x = r * x * (1 - x)
    out = np.empty((n, len(r)))
    for k in range(n):
        x = r * x * (1 - x); out[k] = x
    return out


def bifurcation_bands():
    """BIFURCATION BANDS — the logistic map's attractor laid out as a SPECTRUM: where the orbit lives
    in [0, 1] is mapped onto log-spaced harmonics 3 .. 900, so the chaotic bands of the bifurcation
    diagram become bands of partials and its dark veils (the images of the critical point) become
    bright formant ridges, over a held fundamental. Wild from frame 0 (r 3.572, just past the
    Feigenbaum point: four narrow bands = four buzzing formant clusters). Frame axis: r -> 3.998 —
    the bands merge pairwise, the veils sweep and cross, and the carpet widens to the full interval,
    bright at both edges. The period-3 window (r 3.820-3.857) is stepped over and any frame that
    lands in a smaller window is nudged to the nearest chaotic r, so no frame collapses to a chord;
    the levels are then smoothed along the frame axis."""
    n = 6000
    r = 3.572 + 0.426 * U ** 0.9
    lo, hi = 3.8200, 3.8570
    r = np.where((r > lo) & (r < hi), np.where(r - lo < hi - r, lo, hi), r)
    pts = _logistic_orbit(r, n)
    for f in range(F):
        if len(np.unique(np.round(pts[:, f], 6))) < 0.9 * n:
            for k in range(1, 200):
                rr = r[f] + 0.0004 * ((k + 1) // 2) * (1 if k % 2 else -1)
                if rr > 3.999 or lo < rr < hi:
                    continue
                q = _logistic_orbit(np.array([rr]), n)[:, 0]
                if len(np.unique(np.round(q, 6))) >= 0.9 * n:
                    pts[:, f] = q
                    break
    hpos = np.round(np.exp(np.linspace(np.log(3), np.log(900), 600))).astype(int) - 1
    mags = np.zeros((F, NH))
    for f in range(F):
        H, _ = np.histogram(pts[:, f], bins=600, range=(0, 1))
        np.add.at(mags[f], hpos, _fsm(H.astype(float), 2))
        mags[f] /= np.maximum(mags[f].max(), 1e-12)
        mags[f, 0] = 0.5
    return _spec(_fsm(_rownorm(_hsm(mags, 2)), 3))


TABLES = [
    # flows
    ("DUFFING SWING",        duffing_swing),
    ("CHUA DOUBLE SCROLL",   chua_double_scroll),
    ("VAN DER POL SNAP",     van_der_pol_snap),
    ("BLOOD CELL DELAY",         mackey_glass),
    ("NEURON BURST",         neuron_burst),
    ("LORENZ COMPARATOR",    lorenz_comparator),
    ("THOMAS LABYRINTH",     thomas_labyrinth),
    ("DOUBLE PENDULUM",      double_pendulum),
    ("STADIUM BILLIARD",     stadium_billiard),
    ("CHAOTIC BOUNCER",      chaotic_bouncer),
    ("SYNAPSE STORM",        synapse_storm),
    ("THREE BODY",           three_body),
    ("SWINGING SPRING",      swinging_spring),
    ("JOSEPHSON JUNCTION",   josephson_junction),
    ("DELAY LOOP HOWL",      delay_loop_howl),
    ("DYNAMO REVERSALS",     dynamo_reversals),
    # maps and fractals
    ("LASER TREMOR",         ikeda_tremor),
    ("DE JONG SHADOW",       de_jong_shadow),
    ("HOPALONG PETALS",      hopalong_petals),
    ("CLIFFORD CONTOUR",     clifford_contour),
    ("KAM TORUS BREAK",      kam_torus_break),
    ("JULIA APPROACH",       julia_approach),
    ("MANDELBROT RIM",       mandelbrot_rim),
    ("RIFFLE SHUFFLE",       riffle_shuffle),
    ("LYAPUNOV LOOP",        lyapunov_loop),
    ("LOGISTIC DEPTH",       logistic_depth),
    ("NEWTON BASINS",        newton_basins),
    ("FEIGENBAUM ORGAN",     feigenbaum_organ),
    ("PERIODIC WINDOWS",     periodic_windows),
    ("BIFURCATION BANDS",    bifurcation_bands),
    ("SUBHARMONIC CASCADE",  subharmonic_cascade),
    ("LOGISTIC GRAINS",      logistic_grains),
    ("HAILSTONE",            hailstone),
    ("CAT MAP RECURRENCE",   cat_map_recurrence),
    ("CIRCLE MAP WARP",      circle_map_warp),
    ("CHAOS GAME DUST",      chaos_game_dust),
    # spatial systems
    ("LIFE SLICE",           life_slice),
    ("LIFE CENSUS",          life_census),
    ("RULE 54 WALK",         rule_54_walk),
    ("ISING HEATWAVE",       ising_heatwave),
    ("SANDPILE FRACTAL",     sandpile_fractal),
    ("FLAME FRONT",          flame_front),
    ("FORMANT FLAME",        formant_flame),
    ("SPOT SPLITTER",        spot_splitter),
    ("GINZBURG DEFECTS",     ginzburg_defects),
    ("SAWTOOTH HAIL",           burgulence),
    ("KPZ SURFACE",          kpz_surface),
    ("FAULT LINE",           fault_line),
    ("BARKHAUSEN CRACKLE",   barkhausen_crackle),
    ("RICHARDSON CASCADE",   richardson_cascade),
    ("LANGTON ANT",          langton_ant),
    ("FIREFLY DESYNC",       firefly_desync),
    ("SOLITON RIOT",        soliton_train),
    ("ARRHYTHMIA",           arrhythmia),
    ("EDGE OF CHAOS",        edge_of_chaos),
    ("ENERGY CASCADE",       energy_cascade),
    ("AVALANCHE SPECTRUM",   avalanche_spectrum),
    ("LORENZ VOWELS",        lorenz_vowels),
    ("TRAFFIC JAM",          traffic_jam),
    # rot and grit
    ("BIT ROT",              bit_rot),
    ("FLOAT ROT",            float_rot),
    ("PACKET LOSS",          packet_loss),
    ("SIGMA DELTA FIZZ",     sigma_delta_fizz),
    ("EDGE STORM",           edge_storm),
    ("DIAL UP SCREECH",      dial_up_screech),
    ("LOST SIGNAL",          lost_signal),
    ("BUFFER OVERRUN",       buffer_overrun),
    # subby
    ("SUB QUAKE",            sub_quake),
    ("SUB SIZZLE",           sub_sizzle),
    ("SUB GRINDER",           sub_rattle),
    ("SUB TELEGRAPH",        sub_telegraph),
    ("SUB VINYL",            sub_vinyl),
]

CATEGORY = {ident: "Chaos" for ident, _ in TABLES}
