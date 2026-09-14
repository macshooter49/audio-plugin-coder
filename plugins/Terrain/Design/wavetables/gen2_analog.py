"""
gen2_analog.py — Terrain factory library, ANALOG (the 500 build, fb638).

Hardware idioms from first principles, pushed until the circuit breaks. Every table is ONE circuit whose
failure mode IS the frame axis: frame 0 is the healthy, playable machine; the last frame is the same machine
somewhere a real synth would fall over (a sync ratio no VCO tracks, a ladder howling past self-oscillation,
a transformer core out of permeability, an op-amp flipping phase, a class-D carrier dropping into the audio
band, two VCOs pulling each other off pitch).

HOW THE HARD EDGES STAY CLEAN (no aliasing, exact to harmonic 1000):
  * Piecewise waveforms with jumps (sync, PWM, comparators, sample-and-hold) go through jump_bl(): every
    jump J at time t_j is cancelled with a unit ramp, the now-continuous remainder is FFT'd from an 8x grid,
    and the jumps are added back analytically (J * i/(pi k) * e^{-2 pi i k t_j}). Exact single-cycle sync.
  * Nonlinear circuits (ladder, SVF, slew, hysteresis) are simulated at 4-8x, vectorised over the 128 frames,
    for several cycles; the LAST cycle is kept (periodic steady state), its residual wrap mismatch removed
    by a linear detrend, then band-limited by FFT to harmonic 1000.
  * quiet_roll() rotates EVERY frame by the same amount so the wrap lands on the quietest point: a rotation
    leaves the magnitude spectrum, every metric and the fingerprint unchanged.
Deterministic: numpy only, seeded RNG only.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, wtlib

F, NH, SZ = wtlib.FRAMES, wtlib.NH, wtlib.SIZE
NMAX = 1000
s = np.linspace(0.0, 1.0, F)                 # frame axis 0..1
KH = np.arange(1, NMAX + 1, dtype=float)     # harmonic numbers 1..1000


# ── helpers ───────────────────────────────────────────────────────────────────────────────────
def col(v): return np.asarray(v, float).reshape(-1, 1)
def lin(a, b, c=1.0): return a + (b - a) * s ** c
def geo(a, b, c=1.0): return a * (b / a) ** (s ** c)
def ss(x0, x1):
    u = np.clip((s - x0) / (x1 - x0), 0.0, 1.0)
    return u * u * (3 - 2 * u)
def tgrid(os_=8): return np.arange(SZ * os_) / (SZ * os_)
def ramp(u): return np.mod(u, 1.0) - 0.5     # unit periodic ramp: slope +1, jump -1 at integers


def bl(y, nmax=NMAX):
    """(F, M) periodic cycles at any oversample -> (F, 2048) holding exactly harmonics 1..nmax."""
    y = np.atleast_2d(y); M = y.shape[1]
    S = np.fft.rfft(y, axis=1)
    out = np.zeros((y.shape[0], SZ // 2 + 1), complex)
    out[:, 1:nmax + 1] = S[:, 1:nmax + 1] * (SZ / M)
    return np.fft.irfft(out, n=SZ, axis=1)


def spec(y, nmax=NMAX):
    """(F, M) cycles -> (F, nmax) complex harmonic coefficients (rfft scaled to the 2048 layout)."""
    y = np.atleast_2d(y); M = y.shape[1]
    return np.fft.rfft(y, axis=1)[:, 1:nmax + 1] * (SZ / M)


def unspec(S):
    out = np.zeros((S.shape[0], SZ // 2 + 1), complex)
    out[:, 1:S.shape[1] + 1] = S
    return np.fft.irfft(out, n=SZ, axis=1)


def jump_bl(y, jt, jv, nmax=NMAX, as_spec=False):
    """EXACT band-limiting of cycles with jumps. y (F, M) sampled right-continuously; jt[f]/jv[f] the jump
    times in [0,1) and heights. Each jump is cancelled by J*ramp(t - t_j), the continuous remainder is FFT'd,
    and the jumps come back analytically — nothing above nmax, nothing folded."""
    y = np.atleast_2d(y); M = y.shape[1]; tt = np.arange(M) / M
    k = KH[:nmax]
    out = np.zeros((y.shape[0], nmax), complex)
    for f in range(y.shape[0]):
        tj = np.asarray(jt[f], float); J = np.asarray(jv[f], float)
        ys = y[f]
        if tj.size:
            ys = ys + (J[:, None] * ramp(tt[None, :] - tj[:, None])).sum(0)
        S = np.fft.rfft(ys)[1:nmax + 1] * (SZ / M)
        if tj.size:
            S = S - (SZ / 2) * (1j / (np.pi * k)) * (J[:, None] * np.exp(-2j * np.pi * np.outer(tj, k))).sum(0)
        out[f] = S
    return out if as_spec else unspec(out)


def edge_table(wave, events, os_=8, as_spec=False):
    """wave(f, t) -> the frame-f waveform (smooth between events); events(f) -> candidate jump times.
    Jump heights are measured from the waveform itself, so any piecewise-smooth circuit is exact."""
    tt = tgrid(os_)
    y = np.empty((F, tt.size)); jt, jv = [], []
    for f in range(F):
        y[f] = wave(f, tt)
        e = np.unique(np.mod(np.asarray(events(f), float), 1.0))
        if e.size:
            J = wave(f, np.mod(e + 1e-10, 1.0)) - wave(f, np.mod(e - 1e-10, 1.0))
            keep = np.abs(J) > 1e-7
            e, J = e[keep], J[keep]
        jt.append(e); jv.append(np.zeros(0) if not e.size else J)
    return jump_bl(y, jt, jv, as_spec=as_spec)


def quiet_roll(y):
    """Rotate every frame by the SAME amount so the wrap sits where the table is quietest."""
    d = np.abs(np.diff(y, axis=1)); k = max(1, int(round(0.01 * d.shape[1])))
    typ = np.partition(d, d.shape[1] - k, axis=1)[:, -k:].mean(axis=1)
    st = np.abs(y - np.roll(y, 1, axis=1)) / np.maximum(typ, 1e-12)[:, None]
    return np.roll(y, -int(np.argmin(st.max(axis=0))), axis=1)


def fin(y): return wtlib.finalize(quiet_roll(np.asarray(y, float)))


def detrend_wrap(last, nxt0):
    """last: (F, M) final simulated cycle; nxt0: (F,) the sample that would follow it. A not-quite-converged
    steady state leaves a tiny mismatch at the wrap; a linear ramp removes it (inaudible; makes it loop)."""
    M = last.shape[1]
    return last + (last[:, 0] - nxt0)[:, None] * (np.arange(M) / M)[None, :]


def bl_saw(M, nmax=NMAX):
    """One band-limited RISING saw cycle (2t-1) on an M-point grid: exact Fourier series, harmonics 1..nmax."""
    S = np.zeros(M // 2 + 1, complex)
    k = np.arange(1, nmax + 1)
    S[1:nmax + 1] = 1j * M / (np.pi * k)
    return np.fft.irfft(S, n=M)


def crest_limit(y, ct, iters=32):
    """Per-frame output diode limiter. y (F, M) oversampled cycles -> (F, 2048) band-limited. A frame whose crest
    (peak/RMS) exceeds ct[f] is driven through tanh just hard enough (bisection on the band-limited result) to
    bring it down to ct[f]; frames already under their cap pass untouched. wtlib.finalize peak-normalises every
    frame, so a spiky frame plays quiet — this evens the level out without touching the rest of the table."""
    y = y - y.mean(axis=1, keepdims=True)
    y = y / np.maximum(np.abs(y).max(axis=1, keepdims=True), 1e-12)
    ct = np.broadcast_to(np.asarray(ct, float), (y.shape[0],))
    def cr(v): return np.abs(v).max(axis=1) / np.maximum(np.sqrt((v ** 2).mean(axis=1)), 1e-12)
    def drive(g):
        gc = np.maximum(g, 1e-6)[:, None]
        return bl(np.tanh(gc * y) / np.tanh(gc))
    base = bl(y); need = cr(base) > ct
    lo = np.zeros(y.shape[0]); hi = np.full(y.shape[0], 40.0)
    for _ in range(iters):
        g = 0.5 * (lo + hi); over = cr(drive(g)) > ct
        lo = np.where(over, g, lo); hi = np.where(over, hi, g)
    return np.where(need[:, None], drive(hi), base)


def _ladder(xT, g, k, drive, ncyc, tap=4):
    """Transistor ladder (tanh per stage, tanh on the input/feedback sum). xT (M, F) one input cycle.
    Returns stage `tap` (1..4) over the last of ncyc cycles (F, M) and the sample that would follow it."""
    M, Fn = xT.shape
    y1 = np.zeros(Fn); y2 = np.zeros(Fn); y3 = np.zeros(Fn); y4 = np.zeros(Fn)
    t1 = np.zeros(Fn); t2 = np.zeros(Fn); t3 = np.zeros(Fn); t4 = np.zeros(Fn)
    out = np.empty((M, Fn)); tanh = np.tanh
    for c in range(ncyc + 1):
        for i in range(M if c < ncyc else 1):
            u = tanh(drive * xT[i] - k * y4)
            y1 += g * (u - t1); t1 = tanh(y1)
            y2 += g * (t1 - t2); t2 = tanh(y2)
            y3 += g * (t2 - t3); t3 = tanh(y3)
            y4 += g * (t3 - t4); t4 = tanh(y4)
            if c == ncyc - 1:
                out[i] = (y1, y2, y3, y4)[tap - 1]
    return out.T, (y1, y2, y3, y4)[tap - 1].copy()


def _ladder_taps(xT, g, k, drive, ncyc):
    """_ladder, but returning ALL four stage outputs of the last cycle (4, F, M) and the samples that would follow
    them (4, F) — for tables that mix taps."""
    M, Fn = xT.shape
    y1 = np.zeros(Fn); y2 = np.zeros(Fn); y3 = np.zeros(Fn); y4 = np.zeros(Fn)
    t1 = np.zeros(Fn); t2 = np.zeros(Fn); t3 = np.zeros(Fn); t4 = np.zeros(Fn)
    out = np.empty((4, M, Fn)); tanh = np.tanh
    for c in range(ncyc + 1):
        for i in range(M if c < ncyc else 1):
            u = tanh(drive * xT[i] - k * y4)
            y1 += g * (u - t1); t1 = tanh(y1)
            y2 += g * (t1 - t2); t2 = tanh(y2)
            y3 += g * (t2 - t3); t3 = tanh(y3)
            y4 += g * (t3 - t4); t4 = tanh(y4)
            if c == ncyc - 1:
                out[0, i] = y1; out[1, i] = y2; out[2, i] = y3; out[3, i] = y4
    return out.transpose(0, 2, 1), np.array([y1, y2, y3, y4])


def _svf(xT, fcT, R, ncyc, sat=None):
    """Zero-delay-feedback state-variable filter with a per-sample cutoff. xT, fcT (M, F); fc in cycles per
    sample; R = 1/(2Q) (negative = self-oscillating, then give sat). Returns lp, bp, hp of the last of ncyc
    cycles, each (F, M), and the (lp, bp, hp) sample that would follow."""
    M, Fn = xT.shape
    s1 = np.zeros(Fn); s2 = np.zeros(Fn)
    G = np.tan(np.pi * fcT)
    lp = np.empty((M, Fn)); bp = np.empty((M, Fn)); hp = np.empty((M, Fn))
    for c in range(ncyc + 1):
        for i in range(M if c < ncyc else 1):
            g = G[i]
            h = (xT[i] - (2 * R + g) * s1 - s2) / (1 + 2 * R * g + g * g)
            v1 = g * h; b = v1 + s1; s1 = b + v1
            if sat is not None:
                s1 = np.tanh(s1 * sat) / sat
            v2 = g * b; l = v2 + s2; s2 = l + v2
            if c == ncyc - 1:
                lp[i] = l; bp[i] = b; hp[i] = h
    return lp.T, bp.T, hp.T, (l, b, h)


def sign_crossings(dfun, os_=16, iters=40):
    """Roots of a piecewise-smooth comparator difference d(t) on [0,1): sign changes on an os_ x 2048 grid,
    each refined by bisection against the exact function — edges exact to ~1e-15 of a cycle."""
    te = np.arange(SZ * os_ + 1) / (SZ * os_)
    d = dfun(te)
    i = np.nonzero(np.signbit(d[:-1]) != np.signbit(d[1:]))[0]
    a, b = te[i].copy(), te[i + 1].copy(); da = d[i].copy()
    for _ in range(iters):
        m = 0.5 * (a + b); dm = dfun(m)
        left = np.signbit(dm) != np.signbit(da)
        b = np.where(left, m, b); a = np.where(left, a, m); da = np.where(left, da, dm)
    r = 0.5 * (a + b)
    return r[r < 1.0]


# ══════════════════════════════════════════════════════════════════════════════════════════════
# SYNC — exact single-cycle hard sync, every jump analytic
# ══════════════════════════════════════════════════════════════════════════════════════════════
def triangle_sync_shriek():
    """Hard sync, TRIANGLE slave. The master resets a triangle core once per cycle; slave ratio 1 -> 38 (exp).
    The overdriven core's charge/discharge currents drift apart, so the slave triangle leans into a ramp as
    the ratio climbs. Frame 0 = a plain triangle; the end = a 38x sync shriek no analogue core would track."""
    r = geo(1.0, 38.0, 1.0); sk = lin(0.5, 0.93, 1.8)
    def wave(f, t):
        ph = np.mod(r[f] * t, 1.0); a = sk[f]
        return np.where(ph < a, ph / a, (1.0 - ph) / (1.0 - a)) * 2.0 - 1.0
    return fin(edge_table(wave, lambda f: [0.0]))


def square_sync_splinter():
    """Hard sync, SQUARE slave with its pulse width closing as the ratio climbs (sync + PWM on one knob).
    Ratio 1 -> 26, duty 50% -> 12%. Starts a square; ends as a comb of splinters crammed into each master cycle."""
    r = geo(1.0, 26.0, 1.0); d = lin(0.5, 0.12, 1.3)
    def wave(f, t):
        return np.where(np.mod(r[f] * t, 1.0) < d[f], 1.0, -1.0)
    def ev(f):
        m = np.arange(0, int(np.ceil(r[f])) + 1)
        e = np.concatenate([[0.0], m / r[f], (m + d[f]) / r[f]])
        return e[e < 1.0]
    return fin(edge_table(wave, ev))


def poly_mod_zap():
    """Poly-mod sync: a SINE slave hard-synced to the master AND exponentially frequency-modulated by the
    master's falling ramp (osc B -> osc A pitch), so inside every cycle it chirps from a scream down to a crawl;
    a little of the master ramp is mixed in. Centre ratio 1 -> 4.5, FM depth 0 -> 4.2 octaves each way.
    Frame 0 = sine + ramp; the end = a laser zap in every cycle."""
    r0 = geo(1.0, 4.5, 1.0); dd = lin(0.0, 4.2, 1.2)
    L2 = np.log(2.0)
    def phi(f, t):
        d = dd[f]
        if d < 1e-6:
            return r0[f] * t
        return r0[f] * 2.0 ** d * (1.0 - 2.0 ** (-2.0 * d * t)) / (2.0 * d * L2)
    def wave(f, t):
        return np.sin(2 * np.pi * phi(f, t)) + 0.35 * (1.0 - 2.0 * t)
    def ev(f):
        d = dd[f]; tot = phi(f, np.array([1.0]))[0]
        m = np.arange(1, int(np.floor(tot)) + 1, dtype=float)
        if d < 1e-6:
            e = m / r0[f]
        else:
            arg = 1.0 - m * 2.0 * d * L2 / (r0[f] * 2.0 ** d)
            e = -np.log2(np.maximum(arg, 1e-300)) / (2.0 * d)
        e = np.concatenate([[0.0], e])
        return e[e < 1.0]
    return fin(edge_table(wave, ev, os_=16))


def sync_cascade():
    """Three VCOs in a sync CHAIN: B is hard-synced to the master, C (a triangle core) is hard-synced to B.
    B's ratio climbs 1 -> 4.3 while C's ratio races 1 -> 7.5 ahead of it, so the chirps nest inside chirps.
    Frame 0 = one triangle; the end = a fractal ladder of resets."""
    rB = geo(1.0, 4.3, 1.1); rC = geo(1.0, 7.5, 0.6)
    def wave(f, t):
        pB = np.mod(rB[f] * t, 1.0)
        pC = np.mod(rC[f] * pB, 1.0)
        return 1.0 - 2.0 * np.abs(2.0 * pC - 1.0)
    def ev(f):
        nb = int(np.ceil(rB[f])) + 1; nc = int(np.ceil(rC[f])) + 1
        n = np.arange(nb, dtype=float)[:, None]; m = np.arange(nc, dtype=float)[None, :]
        e = ((n + m / rC[f]) / rB[f]).ravel()
        return e[e < 1.0]
    return fin(edge_table(wave, ev, os_=16))


def mirror_sync():
    """REVERSE ('soft') sync on a triangle core: at every master reset the slave does not restart — it turns
    round and runs backwards (how triangle-core soft sync really works). That takes TWO master cycles to
    repeat, so the frame is two master cycles and the direction flip-flop is a sub-octave square, mixed in as
    the sub output (subby). Slave ratio 1 -> 30. Frame 0 = a triangle over a sub square; the end = mirrored
    chirp pairs riding the sub."""
    r = geo(1.0, 30.0, 1.0)
    def wave(f, t):
        p = np.where(t < 0.5, 2.0 * r[f] * t, r[f] * (2.0 - 2.0 * t))
        return (1.0 - 2.0 * np.abs(2.0 * np.mod(p, 1.0) - 1.0)) + 0.6 * np.where(t < 0.5, 1.0, -1.0)
    return fin(edge_table(wave, lambda f: [0.0, 0.5], os_=16))


def sub_sync_growl():
    """Sub-oscillator sync growl: a sync-swept saw (ratio 1.6 -> 30) and the master's own sine are summed and
    pushed through a hot mixer (tanh drive 0.7 -> 18); a clean sub sine rejoins after the drive so the body
    never moves. Frame 0 = a fat hollow saw over sine; the end = a clipped scream welded onto a sub (subby)."""
    r = geo(1.6, 30.0, 1.1); g = geo(0.7, 18.0, 1.0)
    def wave(f, t):
        sl = 2.0 * np.mod(r[f] * t, 1.0) - 1.0
        x = 0.9 * sl + 0.6 * np.sin(2 * np.pi * t)
        return np.tanh(g[f] * x) / np.tanh(g[f] * 1.5) + 0.5 * np.sin(2 * np.pi * t)
    def ev(f):
        m = np.arange(0, int(np.ceil(r[f])) + 1, dtype=float)
        e = np.concatenate([[0.0], m / r[f]])
        return e[e < 1.0]
    return fin(edge_table(wave, ev, os_=16))


def sync_fm_triangle():
    """Hard sync + exponential FM — the analogue trick for FM that stays in tune: a TRIANGLE carrier reset by
    the master every cycle while a triangle modulator at the master rate bends its pitch in octaves. Carrier
    ratio 1 -> 3.2, FM depth 0 -> 5 octaves. Frame 0 = a triangle; the end = the carrier crawling at the
    bottom of each cycle and sprinting at the top (up-and-down chirps)."""
    r = geo(1.0, 3.2, 1.0); dd = lin(0.0, 5.0, 1.1)
    L2 = np.log(2.0)
    def phase(f, t):
        d = dd[f]
        if d < 1e-6:
            return r[f] * t
        a = r[f] * 2.0 ** (-d) * (2.0 ** (4 * d * np.minimum(t, 0.5)) - 1.0) / (4 * d * L2)
        b = r[f] * 2.0 ** d * (1.0 - 2.0 ** (-4 * d * np.maximum(t - 0.5, 0.0))) / (4 * d * L2)
        return a + b
    def wave(f, t):
        return 1.0 - 2.0 * np.abs(2.0 * np.mod(phase(f, t), 1.0) - 1.0)
    return fin(edge_table(wave, lambda f: [0.0], os_=16))


def twin_slave_sync():
    """Two SINE slaves hard-synced to one master at diverging ratios (1 -> 9 and 1 -> 23) and mixed — a
    two-formant sync voice whose formants pull apart as the table climbs; the only edge is the master's
    reset. Frame 0 = a sine (sub); the end = two screaming formants a harmonic series apart."""
    r2 = geo(1.0, 9.0, 1.0); r3 = geo(1.0, 23.0, 0.8)
    def wave(f, t):
        return np.sin(2 * np.pi * np.mod(r2[f] * t, 1.0)) + 0.8 * np.sin(2 * np.pi * np.mod(r3[f] * t, 1.0))
    return fin(edge_table(wave, lambda f: [0.0], os_=16))


# ══════════════════════════════════════════════════════════════════════════════════════════════
# FILTERS — ladders, state-variables and phasers pushed past self-oscillation
# ══════════════════════════════════════════════════════════════════════════════════════════════
def ladder_howlround():
    """A transistor ladder driven into self-oscillation, heard from its FIRST pole (the bright 6 dB tap) while
    the full four-pole loop howls, then the output VCA overdriven. Saw in; cutoff 4 -> 12 harmonics,
    resonance 2 -> 7.5 (a ladder sings on its own past ~4), input drive 0.6 -> 4, output gain 1 -> 30. Frame 0 =
    a bright saw with a resonant bump; the end = the howl squared off and riding the saw."""
    os_ = 4; M = SZ * os_
    x = bl_saw(M)
    fc = geo(4.0, 12.0, 1.0); k = lin(2.0, 7.5, 1.0); dr = geo(0.6, 4.0, 1.0)
    g = 1.0 - np.exp(-2 * np.pi * fc / M)
    y, nxt = _ladder(np.repeat(x[:, None], F, axis=1), g, k, dr, 8, tap=1)
    y = detrend_wrap(y, nxt)
    y = y - y.mean(axis=1, keepdims=True)
    y = y / np.maximum(np.abs(y).max(axis=1, keepdims=True), 1e-9)
    G = col(geo(1.0, 30.0, 0.8))
    return fin(bl(np.tanh(G * y) / np.tanh(G)))


def filter_fm_squelch():
    """Audio-rate filter FM: the oscillator's own saw sweeps a resonant state-variable filter's cutoff inside
    every cycle. FM depth 0 -> 6.5 octaves, Q 2 -> 40, base cutoff 8 -> 30 harmonics; the tap slides from
    low-pass to band/high-pass and the filter output is driven into a clipper at the end (1 -> 12). Frame 0 = a
    soft low-passed saw; the end = the resonance whipping through the whole spectrum each cycle."""
    os_ = 4; M = SZ * os_
    x = bl_saw(M)
    dep = lin(0.0, 6.5, 1.0); Q = geo(2.0, 40.0, 1.0); f0 = geo(8.0, 30.0, 1.0)
    fcT = np.minimum(f0[None, :] * 2.0 ** (dep[None, :] * x[:, None]), 0.4 * M) / M
    lp, bp, hp, nx = _svf(np.repeat(x[:, None], F, axis=1), fcT, 1.0 / (2 * Q), 5)
    m = col(ss(0.1, 0.9))
    y = (1 - m) * (lp + 0.8 * bp) + m * (bp + 0.6 * hp)
    y1 = (1 - m[:, 0]) * (nx[0] + 0.8 * nx[1]) + m[:, 0] * (nx[1] + 0.6 * nx[2])
    y = detrend_wrap(y, y1)
    y = y / np.maximum(np.abs(y).max(axis=1, keepdims=True), 1e-9)
    G = col(geo(1.0, 12.0, 1.5))
    return fin(bl(np.tanh(G * y) / np.tanh(G)))


def highpass_scream():
    """A 2-pole high-pass whose damping is pushed NEGATIVE — the self-oscillating, diode-limited 'scream'
    filter — fed a triangle: cutoff 6 -> 48 harmonics, damping 0.3 -> -0.5, drive 0.8 -> 3, the loop limited
    only by a saturating integrator. Frame 0 = a thin, edgy triangle; the end = the filter's own scream riding
    over its input.
    fb638 critique: through the resonant middle the output was a train of ringing spikes (crest 4.7, -13.4 dBFS
    once peak-normalised) and the scream then arrived 4 dB louder. A diode limiter now sits on the output, its
    drive solved per frame so no frame's crest exceeds 2.8 (2.3 once the scream is up): the dip and the jump are
    gone, while the spectrum moves by only ~0.5 dB (fingerprint)."""
    os_ = 4; M = SZ * os_; tt = tgrid(os_)
    x = 1.0 - 2.0 * np.abs(2.0 * tt - 1.0)
    fc = geo(6.0, 48.0, 1.0); R = lin(0.3, -0.5, 1.0); dr = geo(0.8, 3.0, 1.0)
    fcT = np.repeat((fc / M)[None, :], M, axis=0)
    lp, bp, hp, nx = _svf(x[:, None] * dr[None, :], fcT, R, 10, sat=1.0)
    return fin(crest_limit(detrend_wrap(hp, nx[2]), 2.8 - 0.5 * ss(0.50, 0.60)))


def notch_mode_sweep():
    """A two-pole state-variable filter's MODE knob swept all the way (low-pass -> notch -> high-pass, the
    classic analogue morph) while its cutoff climbs 4 -> 40 harmonics and its resonance rises to the edge of
    oscillation (Q 0.8 -> 30); in the last third the output is driven into clipping. Frame 0 = a warm
    low-passed saw; the end = a hollow, high-passed resonant scream with the lows carved away."""
    w = KH[None, :] / col(geo(4.0, 40.0, 1.0)); Q = col(geo(0.8, 30.0, 1.0)); m = col(lin(0.0, 1.0, 1.0))
    den = 1.0 - w * w + 1j * w / Q
    H = ((1 - m) * 1.0 + m * (-w * w)) / den
    y = unspec((1j * SZ / (np.pi * KH))[None, :] * H)
    y = y / np.maximum(np.abs(y).max(axis=1, keepdims=True), 1e-9)
    G = col(np.where(s < 0.66, 1.0, geo(1.0, 8.0, 1.0) ** ss(0.66, 1.0)))
    return fin(bl(np.tanh(G * y) / np.tanh(G)))


def phaser_feedback_howl():
    """A six-stage OTA phaser frozen at each point of its sweep (stage corner 1.5 -> 60 harmonics) with its
    feedback pushed 0.3 -> 0.95, the edge where a phaser starts to howl: three notches march up the saw while
    the peaks between them sharpen into whistles; the output stage is driven harder and harder (0.25 -> 2.5,
    RMS-referenced). Frame 0 = a gently phased saw; the end = a comb of howling, clipped peaks."""
    wc = col(geo(1.5, 60.0, 1.0)); g = col(lin(0.3, 0.95, 0.8))
    A = ((1.0 - 1j * KH[None, :] / wc) / (1.0 + 1j * KH[None, :] / wc)) ** 6
    H = 1.0 + A / (1.0 - g * A)
    y = unspec((1j * SZ / (np.pi * KH))[None, :] * H)
    y = y / np.sqrt((y ** 2).mean(axis=1, keepdims=True))
    return fin(bl(np.tanh(col(geo(0.25, 2.5, 1.0)) * y)))


def dirty_ladder_sweep():
    """A transistor ladder that never cleans up (fb638 critique: the pool's ladder settled into a square). A saw
    driven hot (3 -> 10) into four tanh poles whose loop climbs from the edge of self-oscillation (k 4.2) to a
    full howl (7.5) while the cutoff sweeps 2 -> 40 harmonics; the first pole bleeds into the output (the raw
    6 dB tap) and the output stage is a diode wavefolder whose gain climbs 1.2 -> 10 while its bias walks
    0 -> 0.8, from soft saturation into foldback. A clipper squares a howl off; a folder keeps splitting it, so
    every frame stays dirty. Frame 0 = a dark, driven ladder bass; the end = a howling, folded shred."""
    os_ = 4; M = SZ * os_
    x = bl_saw(M)
    fc = geo(2.0, 40.0, 1.0); k = lin(4.2, 7.5, 1.0); dr = geo(3.0, 10.0, 1.0)
    g = 1.0 - np.exp(-2 * np.pi * fc / M)
    Y, nx = _ladder_taps(np.repeat(x[:, None], F, axis=1), g, k, dr, 8)
    y = detrend_wrap(Y[3] + Y[0], nx[3] + nx[0])
    y = y - y.mean(axis=1, keepdims=True)
    y = y / (np.sqrt(2.0) * np.sqrt((y ** 2).mean(axis=1, keepdims=True)))
    G = col(geo(1.2, 10.0, 1.0)); b = col(lin(0.0, 0.8, 1.0))
    return fin(bl(np.sin(0.5 * np.pi * (G * y + b)) - np.sin(0.5 * np.pi * b)))


# ══════════════════════════════════════════════════════════════════════════════════════════════
# CROSS-MOD — two oscillators bending, reversing, ringing and pulling each other
# ══════════════════════════════════════════════════════════════════════════════════════════════
def cross_fm_knot():
    """Two VCOs frequency-modulating EACH OTHER (a cross-mod loop): A (triangle, 1x) bends B (sine, 3x) and B
    bends A; the periodic solution is found by fixed-point iteration, and past the point where the loop can
    settle the iteration itself ties the knot. Index 0 -> 14 / 0 -> 9. Frame 0 = a triangle with a
    twelfth; the end = a knot."""
    os_ = 8; M = SZ * os_; tt = tgrid(os_)
    a = lin(0.0, 14.0, 1.0); b = lin(0.0, 9.0, 0.8)
    pa = np.repeat(tt[None, :], F, axis=0); pb = 3.0 * pa
    def tri(p): return 1.0 - 2.0 * np.abs(2.0 * np.mod(p + 0.25, 1.0) - 1.0)
    def integ(v):
        v = v - v.mean(axis=1, keepdims=True)
        c = np.cumsum(v, axis=1) / M
        return c - c.mean(axis=1, keepdims=True)
    for _ in range(24):
        sa, sb = tri(pa), np.sin(2 * np.pi * pb)
        na = tt[None, :] + col(a) * integ(sb) * 3.0
        nb = 3.0 * tt[None, :] + col(b) * integ(sa) * 4.0
        pa = 0.5 * pa + 0.5 * na; pb = 0.5 * pb + 0.5 * nb
    return fin(bl(tri(pa) + 0.45 * np.sin(2 * np.pi * pb)))


def through_zero_triangles():
    """Through-zero linear FM between two triangle cores — the analogue TZFM trick: when the modulation drives
    the carrier's frequency below zero its core runs BACKWARDS instead of stalling, so the pitch never drifts.
    Modulator 5x (sidebands land on 1, 4, 6, 9, 11 ...), index 0 -> 20. Frame 0 = a triangle; the end = the
    carrier thrashing back and forth through zero, a wall of folded sidebands."""
    os_ = 8; M = SZ * os_; tt = tgrid(os_)
    I = lin(0.0, 20.0, 1.1)
    mod = 1.0 - 2.0 * np.abs(2.0 * np.mod(5.0 * tt + 0.25, 1.0) - 1.0)
    im = np.cumsum(mod - mod.mean()) / M; im = im - im.mean()
    ph = tt[None, :] + col(I) * im[None, :] * 5.0
    return fin(bl(1.0 - 2.0 * np.abs(2.0 * np.mod(ph + 0.25, 1.0) - 1.0)))


def through_zero_sevens():
    """Through-zero FM, the wide one: a triangle carrier and a SINE modulator at 7x — sidebands can only land on
    1, 6, 8, 13, 15 ... so the spectrum is a sparse, gapped comb — index 0 -> 28, the carrier reversing through
    zero again and again inside each cycle. Frame 0 = a triangle; the end = a gapped comb of folded sidebands
    shredding the triangle into ripples."""
    os_ = 8; M = SZ * os_; tt = tgrid(os_)
    I = lin(0.0, 28.0, 1.0)
    im = -np.cos(2 * np.pi * 7.0 * tt) / (2 * np.pi * 7.0)
    ph = tt[None, :] + col(I) * im[None, :] * 7.0
    return fin(bl(1.0 - 2.0 * np.abs(2.0 * np.mod(ph + 0.25, 1.0) - 1.0)))


def ring_mod_fifths():
    """Two VCOs a fifth apart (a sine at 2x, a triangle at 3x — the frame's fundamental is the note they
    imply) into a diode ring modulator. The ring fades in, its diodes switch harder (drive 0.8 -> 20, the
    triangle becoming a square gate) and the 3x oscillator drifts a third of its cycle against the 2x; in the
    second half the ring's output transformer saturates (1 -> 14). The difference tone lands on the
    fundamental, so a sub appears out of nowhere. Frame 0 = a soft power-fifth; the end = a ringing, subby snarl."""
    m = ss(0.0, 0.45); g = geo(0.8, 20.0, 1.0); ph = lin(0.0, 1.0 / 3.0, 1.0)
    tt = tgrid(8)[None, :]
    a = np.sin(2 * np.pi * 2.0 * tt)
    b = 1.0 - 2.0 * np.abs(2.0 * np.mod(3.0 * tt + col(ph) + 0.25, 1.0) - 1.0)
    cb = np.tanh(col(g) * b) / np.tanh(col(g))
    y = (1 - col(m)) * 0.5 * (a + b) + col(m) * a * cb
    y = y / np.abs(y).max(axis=1, keepdims=True)
    G = col(geo(1.0, 14.0, 1.0) ** ss(0.4, 1.0))
    return fin(bl(np.tanh(G * y) / np.tanh(G)))


def heterodyne_pull():
    """Two RF oscillators beating (the heterodyne principle) while their coupling pulls them toward lock —
    Adler's equation dphi/dt = D - K sin(phi), solved exactly — and the beat detector is driven into clipping.
    Pull K/D 0 -> 0.97, detector gain 1 -> 40. Frame 0 = a sine (sub); the end = a clipped plateau torn by one
    fast phase slip per cycle."""
    tt = tgrid(16)[None, :]
    K = col(lin(0.0, 0.97, 0.8)); Om = np.sqrt(1 - K * K)
    ph = 2 * np.arctan(K + Om * np.tan(np.pi * tt))
    G = col(geo(1.0, 40.0, 1.2))
    return fin(bl(np.tanh(G * np.sin(ph)) / np.tanh(G)))


# ══════════════════════════════════════════════════════════════════════════════════════════════
# SATURATION & FAILURE — iron, silicon and diodes out of their depth
# ══════════════════════════════════════════════════════════════════════════════════════════════
def iron_core_inrush():
    """A square wave into an output transformer whose core saturates. The flux is the square's integral (a
    triangle) riding a remanence offset (0 -> 0.45); while the core has permeability the square passes, the
    moment the flux saturates the coupling collapses and the output falls to nothing until the next edge.
    Flux swing 0.3 -> 5.5x saturation; the transformer's low-end droop high-passes it all. Frame 0 = a square;
    the end = lopsided pulses — each half-cycle dies the instant the iron runs out."""
    os_ = 8; M = SZ * os_; tt = tgrid(os_)
    k = np.arange(1, M // 2 + 1)
    S = np.zeros(M // 2 + 1, complex); S[1:] = np.where((k % 2 == 1) & (k <= NMAX), -2j * M / (np.pi * k), 0)
    sq = np.fft.irfft(S, n=M)
    tri = 1.0 - 2.0 * np.abs(2.0 * np.mod(tt + 0.25, 1.0) - 1.0)
    A = col(geo(0.3, 5.5, 1.0)); off = col(lin(0.0, 0.45, 1.2))
    mu = 1.0 / np.cosh(np.maximum(np.abs(A * tri[None, :] + off) - 0.7, 0.0) * 4.0) ** 2
    return fin(unspec(spec(sq[None, :] * mu) * (1j * KH / 0.7) / (1 + 1j * KH / 0.7)))


def iron_saw_collapse():
    """A saw into a saturating output transformer. The core's flux is the saw's integral (a parabola) riding a
    remanence offset (0 -> 0.45); wherever the flux saturates, the coupling collapses and the output falls
    away until the flux swings back, while the transformer's low-end droop bends every ramp into a curve.
    Flux swing 0.3 -> 5.5x saturation. Frame 0 = a drooping saw; the end = a ramp broken into collapsing
    shelves and notches."""
    os_ = 8; M = SZ * os_
    saw = bl_saw(M)
    flux = np.cumsum(saw) / M; flux = flux - flux.mean(); flux = flux / np.abs(flux).max()
    A = col(geo(0.3, 5.5, 1.0)); off = col(lin(0.0, 0.45, 1.2))
    mu = 1.0 / np.cosh(np.maximum(np.abs(A * flux[None, :] + off) - 0.7, 0.0) * 4.0) ** 2
    return fin(unspec(spec(saw[None, :] * mu) * (1j * KH / 0.7) / (1 + 1j * KH / 0.7)))


def op_amp_phase_flip():
    """An op-amp driven past its input common-mode range: first it clips at the rails, then — the famous
    failure — the output FLIPS to the opposite rail whenever the input overshoots, and its slew rate cannot
    keep up. Gain 0.7 -> 11, slew limit falling to 0.15% of full scale per sample. Frame 0 = a soft sine with
    its octave; the end = a waveform turned inside out, flipped notches with slewed walls."""
    os_ = 8; M = SZ * os_; tt = tgrid(os_)
    x = np.sin(2 * np.pi * tt) + 0.3 * np.sin(4 * np.pi * tt + 0.7)
    g = geo(0.7, 11.0, 1.0)
    v = col(g) * x[None, :]
    y = np.tanh(1.6 * v) * (1.0 - 2.0 / (1.0 + np.exp(-(np.abs(v) - 1.5) / 0.015)))
    sr = geo(1.0, 0.0015, 1.0)
    out = np.empty((F, M)); o = y[:, -1].copy(); yT = np.ascontiguousarray(y.T)
    for c in range(2):
        for i in range(M):
            o = o + np.clip(yT[i] - o, -sr, sr)
            if c == 1:
                out[:, i] = o
    return fin(bl(out))


def starved_sine_shaper():
    """A VCO's triangle-to-sine shaper (a differential pair) starving: its drive climbs 1.6 -> 12 so the sine
    squares up, an offset walks in (asymmetry), and the starved pair stops conducting near zero — a crossover
    dead band opens (0 -> 1.5) where the output drops to a tenth, so every zero crossing becomes a stair.
    Edges exact. Frame 0 = a sine (sub); the end = a terraced, stepped square."""
    g = geo(1.6, 12.0, 1.0); off = lin(0.0, 0.15, 1.2); thr = lin(0.0, 1.5, 1.3); dz = 0.9 * ss(0.3, 1.0)
    def v(f, t):
        return g[f] * (1.0 - 2.0 * np.abs(2.0 * np.mod(t + 0.25, 1.0) - 1.0) + off[f])
    def wave(f, t):
        x = v(f, t); y = np.tanh(x)
        return np.where(np.abs(x) < thr[f], (1.0 - dz[f]) * y, y)
    def ev(f):
        if thr[f] <= 0.0 or dz[f] <= 0.0:
            return np.zeros(0)
        return sign_crossings(lambda t: np.abs(v(f, t)) - thr[f])
    return fin(edge_table(wave, ev))


def diode_detector_clip():
    """An AM radio's diode detector with its smoothing capacitor far too big. The note rides a carrier at 90%
    modulation; the diode charges the cap on every carrier peak and the cap can only bleed away through its
    resistor, so the falling half of the note turns into exponential ramps — 'diagonal clipping'.
    fb638 critique: the frame axis runs from the choked detector to the naked one — carrier 3x -> 40x (rising
    out of the audio band), RC 30 -> 0.3 carrier periods — so it starts playable and ends in buzz (it used to
    run the other way). Frame 0 = a two-tooth falling saw of diagonal ramps; the end = the note drowned in a
    40x carrier's half-wave teeth."""
    os_ = 8; M = SZ * os_; tt = tgrid(os_)
    N = geo(3.0, 40.0, 1.0); rc = geo(30.0, 0.3, 1.0) / N; mi = 0.9
    n0 = np.floor(N); al = N - n0
    def det(nc):
        v = (1.0 + mi * np.sin(2 * np.pi * tt))[None, :] * np.maximum(np.sin(2 * np.pi * col(nc) * tt[None, :]), 0.0)
        vT = np.ascontiguousarray(v.T); dec = np.exp(-1.0 / (M * rc))
        y = np.zeros(F); out = np.empty((M, F))
        for c in range(3):
            for i in range(M):
                y = np.maximum(vT[i], y * dec)
                if c == 2:
                    out[i] = y
        return out.T
    y = col(1 - al) * det(n0) + col(al) * det(n0 + 1)
    return fin(bl(y))


def dying_battery_vco():
    """A saw-core VCO on a dying battery (fb638 critique: 'a VCO that loses its pitch as its current starves').
    The frame holds three healthy core cycles — the note an octave and a fifth up. As the supply current starves
    the core cannot finish its cycles in time: the third cycle charges for less and less time, resets before it
    reaches threshold and shrinks to nothing, then the second — 3 -> 2 -> 1 cycles per frame, the pitch falling
    a fourth, then an octave, in cycle-slips. Meanwhile the ramps bend (the current source loses compliance,
    bend 0.05 -> 3), the reset floor rises (0 -> 0.4), and in the last third the starved comparator chatters at
    threshold (up to 9 irregular partial resets, entering fractionally); the output buffer clips on the sagging
    rail (crest held to 3). Every reset is an exact jump. Frame 0 = a bright saw; the end = one bent, chattering
    cycle — the VCO has lost its pitch and its shape."""
    kap = 3.0 - ss(0.2, 0.4) - ss(0.5, 0.7)                       # core cycles per frame, 3 -> 1
    C = geo(0.05, 3.0, 1.0); FL = lin(0.0, 0.4, 1.3); N = 9.0 * ss(0.6, 1.0); D0 = lin(0.2, 0.45, 1.0)
    def B(q, cf): return (1.0 - np.exp(-cf * q)) / (1.0 - np.exp(-cf))        # starved charging curve
    def Binv(b, cf): return -np.log(1.0 - b * (1.0 - np.exp(-cf))) / cf
    PL = []
    for f in range(F):
        nfull = int(np.floor(kap[f] + 1e-9)); w = kap[f] - nfull
        w = 0.0 if w < 1e-6 else w
        cf, fl, n = C[f], FL[f], N[f]; nt = int(np.ceil(n - 1e-9)); qs = []
        for j in range(nt):                                        # chatter teeth on the last full cycle
            frac = (n - (nt - 1)) if j == nt - 1 else 1.0
            d = min(D0[f] * (1.0 + 0.35 * j) * (1.0 + 0.5 * np.sin(2.3 * j + 1.1)) * frac, 0.95 * (1 - fl))
            qs.append(Binv(1.0 - d / (1 - fl), cf))
        PL.append((nfull, w, 1.0 / (nfull + w + sum(1.0 - q for q in qs)), qs))
    def wave(f, t):
        cf, fl = C[f], FL[f]; nfull, w, T, qs = PL[f]
        V = np.full_like(t, fl); t0 = 0.0
        for i in range(nfull):
            m = (t >= t0) & (t < t0 + T); V = np.where(m, fl + (1 - fl) * B((t - t0) / T, cf), V); t0 += T
            if i == nfull - 1:
                for q in qs:
                    du = (1.0 - q) * T; m = (t >= t0) & (t < t0 + du)
                    V = np.where(m, fl + (1 - fl) * B(q + (t - t0) / T, cf), V); t0 += du
        if w > 0:                                                  # the slipping cycle: never reaches threshold
            V = np.where(t >= t0, fl + (1 - fl) * B(np.maximum(t - t0, 0.0) / T, cf), V)
        return 2.0 * V - 1.0
    def ev(f):
        nfull, w, T, qs = PL[f]; e = [0.0]; t0 = 0.0
        for i in range(nfull):
            t0 += T
            if i == nfull - 1:
                for q in qs:
                    e.append(t0); t0 += (1.0 - q) * T
            if t0 < 1.0 - 1e-12:
                e.append(t0)
        return e
    S = edge_table(wave, ev, as_spec=True)
    up = np.zeros((F, 2 * SZ + 1), complex); up[:, 1:S.shape[1] + 1] = S
    return fin(crest_limit(np.fft.irfft(up, n=4 * SZ, axis=1), 3.0))


# ══════════════════════════════════════════════════════════════════════════════════════════════
# SWITCHING, SAMPLING & DIVIDING — carriers and clocks falling into the audio band
# ══════════════════════════════════════════════════════════════════════════════════════════════
def class_d_saw():
    """A sub-bass sine played through a class-D switching amp whose carrier falls into the audio band.
    Natural-sampling PWM (a synced triangle carrier, every edge found exactly); the LC output filter (120
    harmonics) loses its load and rings harder (Q 0.8 -> 4). Carrier 30x -> 2.3x the note. Frame 0 = a sub
    sine wearing a halo of carrier sidebands; the end = the amp's own switching pattern — a bar code."""
    c = geo(30.0, 2.3, 0.8); fLC = 120.0; Q = lin(0.8, 4.0, 0.8)
    def car(f, t):
        p = np.mod(c[f] * t, 1.0)
        return np.where(p < 0.5, -1.0 + 4.0 * p, 3.0 - 4.0 * p)
    def wave(f, t):
        return np.where(0.92 * np.sin(2 * np.pi * t) > car(f, t), 1.0, -1.0)
    def ev(f):
        return np.concatenate([[0.0], sign_crossings(lambda t: 0.92 * np.sin(2 * np.pi * t) - car(f, t))])
    S = edge_table(wave, ev, as_spec=True)
    H = 1.0 / (1.0 - (KH[None, :] / fLC) ** 2 + 1j * KH[None, :] / (fLC * col(Q)))
    return fin(unspec(S * H))


def bucket_brigade_dive():
    """A bucket-brigade delay whose clock is dragged down through the audio band. The saw is sampled by the
    BBD clock (300 -> 3.3 samples per cycle) and held, through fixed 45-harmonic pre/post filters designed for
    a fast clock; the delay (3 stages / clock) is fed back hard (0.7 -> 0.97) and flanges against the dry path,
    and the clock itself bleeds onto the output, its whine falling into the note. Frame 0 = a resonant,
    flanged saw; the end = a three-step staircase with a clock buzz and a howling comb through it."""
    c = geo(300.0, 3.3, 0.8); fbk = lin(0.7, 0.97, 1.0); bld = lin(0.05, 0.5, 1.0)
    X = (1j * SZ / (np.pi * KH)) / (1 + 1j * KH / 45.0) ** 2
    post = 1.0 / (1 + 1j * KH / 45.0) ** 4
    XS = []
    for f in range(F):
        tj = np.arange(int(np.ceil(c[f]))) / c[f]
        XS.append((2.0 / SZ) * np.real(np.exp(2j * np.pi * np.outer(tj, KH)) @ X))
    def zoh(f, t):
        xs = XS[f]; return xs[np.minimum((t * c[f]).astype(int), xs.size - 1)]
    def ev(f):
        return np.arange(XS[f].size) / c[f]
    def bleed(f, t):
        ph = t * c[f] - np.minimum(np.floor(t * c[f]), XS[f].size - 1)
        return np.where(ph < 0.18, bld[f], 0.0)
    def evb(f):
        tj = np.arange(XS[f].size) / c[f]
        e = np.concatenate([tj, tj + 0.18 / c[f]])
        return e[e < 1.0]
    S = edge_table(zoh, ev, as_spec=True)
    B = edge_table(bleed, evb, as_spec=True)
    D = np.exp(-2j * np.pi * KH[None, :] * col(3.0 / c))
    wet = S * post[None, :] * D / (1 - col(fbk) * D * post[None, :])
    return fin(unspec(X[None, :] + 0.9 * wet + B))


def undertone_dividers():
    """A master VCO counted down by /2 /3 /4 /6 /12 dividers into a VCF — the subharmonic-sequencer voice. The
    frame is the /12 cycle, so the undertones land on harmonics 1, 2, 3, 4, 6 and the master (a triangle core)
    on 12. The counters enter from the bottom up with their real duty cycles (the /3 counter gives a 1/3
    pulse) while the 12 dB low-pass opens 3 -> 300 harmonics; from the middle on the counters glitch — their
    duty cycles collapse toward needles. Frame 0 = a dark sub square (subby); the end = a bright, glitching
    undertone chord."""
    divs = [(1, 0.5, 1.0, None), (2, 0.5, 0.5, (0.00, 0.20)), (3, 0.5, 0.8, (0.15, 0.35)),
            (4, 1.0 / 3.0, 0.8, (0.30, 0.50)), (6, 0.5, 0.7, (0.45, 0.65))]
    W = [np.full(F, lv) if span is None else lv * (0.35 + 0.65 * ss(*span)) if h == 2 else lv * ss(*span)
         for h, d, lv, span in divs]
    D = [d * (1.0 - 0.85 * ss(0.4, 1.0) * (0.35 + 0.15 * j)) for j, (h, d, lv, sp) in enumerate(divs)]
    wm = 0.9 * ss(0.5, 0.7)
    def wave(f, t):
        y = sum(W[j][f] * np.where(np.mod(h * t, 1.0) < D[j][f], 1.0, -1.0) for j, (h, d, lv, sp) in enumerate(divs))
        return y + wm[f] * (1.0 - 2.0 * np.abs(2.0 * np.mod(12.0 * t, 1.0) - 1.0))
    def ev(f):
        e = []
        for j, (h, d, lv, sp) in enumerate(divs):
            k = np.arange(h, dtype=float); e += [k / h, (k + D[j][f]) / h]
        return np.concatenate(e)
    S = edge_table(wave, ev, as_spec=True)
    return fin(unspec(S / (1.0 + 1j * KH[None, :] / col(geo(3.0, 300.0, 1.0))) ** 2))


# ══════════════════════════════════════════════════════════════════════════════════════════════
# TABLE LIST
# ══════════════════════════════════════════════════════════════════════════════════════════════
TABLES = [
    ("TRIANGLE SYNC SHRIEK",   triangle_sync_shriek),
    ("SQUARE SYNC SPLINTER",   square_sync_splinter),
    ("POLY MOD ZAP",           poly_mod_zap),
    ("SYNC CASCADE",           sync_cascade),
    ("MIRROR SYNC",            mirror_sync),
    ("SUB SYNC GROWL",         sub_sync_growl),
    ("SYNC FM TRIANGLE",       sync_fm_triangle),
    ("TWIN FORMANT SYNC",        twin_slave_sync),
    ("LADDER HOWLROUND",       ladder_howlround),
    ("FILTER FM SQUELCH",      filter_fm_squelch),
    ("HIGHPASS SCREAM",        highpass_scream),
    ("NOTCH MODE SWEEP",       notch_mode_sweep),
    ("PHASER FEEDBACK HOWL",   phaser_feedback_howl),
    ("CROSS FM KNOT",          cross_fm_knot),
    ("THROUGH ZERO TRIANGLES", through_zero_triangles),
    ("THROUGH ZERO FM",    through_zero_sevens),
    ("RING MOD FIFTHS",        ring_mod_fifths),
    ("HETERODYNE PULL",        heterodyne_pull),
    ("IRON CORE INRUSH",       iron_core_inrush),
    ("IRON SAW COLLAPSE",      iron_saw_collapse),
    ("OP AMP PHASE FLIP",      op_amp_phase_flip),
    ("STARVED SINE SHAPER",    starved_sine_shaper),
    ("DIODE DETECTOR CLIP",    diode_detector_clip),
    ("CLASS D SAW",            class_d_saw),
    ("BUCKET BRIGADE DIVE",    bucket_brigade_dive),
    ("UNDERTONE DIVIDERS",     undertone_dividers),
    ("DIRTY LADDER SWEEP",     dirty_ladder_sweep),        # fb638 NEW — the critique's gap: a ladder that stays dirty
    ("DYING BATTERY VCO",      dying_battery_vco),         # fb638 NEW — the critique's gap: a VCO starving off pitch
]
CATEGORY = {ident: "Analog" for ident, _ in TABLES}
