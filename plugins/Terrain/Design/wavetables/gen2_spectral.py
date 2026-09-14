"""
gen2_spectral.py — SPECTRAL, new tables for THE 500 (fb638, 2026-09-13).

Partial designs with a plan. Every table picks ONE thing to do to the partials and lets the frame axis
push it somewhere a filter cannot go: windowed grains of a high partial riding a sine body, spectral
chirps inside one cycle, random-partial carpets whose amplitudes cycle, chords and scales packed into
one cycle, holes and combs, number-theory series, tilt dances, and inharmonic sets snapped to the grid.

Two construction lanes, both periodic by construction (the sample after 2047 is sample 0):
  SPEC  magnitudes (128 x 1024) -> wtlib.cycles_from_mags with the shared fixed PHASE (or a table's own
        fixed phase law, e.g. a Schroeder chirp phase), harmonics above 1000 zeroed.
  TIME  built at 4x/8x (8192/16384 samples per cycle), FFT, harmonics 1..1000 kept, irfft to 2048.
Deterministic: every random number comes from a seeded default_rng. Clean-room: no audio is read.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, wtlib

F, N, SIZE = wtlib.FRAMES, wtlib.NH, wtlib.SIZE
n = wtlib.n_ax.astype(float)             # harmonic numbers 1..1024
ni = wtlib.n_ax.astype(np.int64)
ln = np.log2(n)                          # harmonic axis in octaves
u = np.linspace(0.0, 1.0, F)             # the frame axis
U = u[:, None]
NMAX = 1000
OS = 4
T = np.arange(SIZE * OS) / (SIZE * OS)   # one cycle, 4x oversampled
TWO_PI = 2.0 * np.pi


# ── shared primitives ────────────────────────────────────────────────────────────────────────
def sm(x):
    x = np.clip(np.asarray(x, float), 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def spec(m, phase=None):
    """(F, N) magnitudes -> finalized cycles. Harmonics above NMAX are zeroed; a silent row falls back to a saw."""
    m = np.maximum(np.nan_to_num(np.asarray(m, float)), 0.0)
    m[:, NMAX:] = 0.0
    pk = m.max(axis=1, keepdims=True)
    m = np.where(pk > 1e-30, m, (n ** -1.0)[None, :] * (n <= NMAX))
    return wtlib.finalize(wtlib.cycles_from_mags(m, phase))


def tband(y, nmax=NMAX):
    """(F, SIZE*k) oversampled cycles -> keep harmonics 1..nmax -> (F, SIZE), finalized."""
    S = np.fft.rfft(np.asarray(y, float), axis=1)
    o = np.zeros((S.shape[0], SIZE // 2 + 1), complex)
    o[:, 1:nmax + 1] = S[:, 1:nmax + 1]
    return wtlib.finalize(np.fft.irfft(o, n=SIZE, axis=1))


def wrapd(x):
    """Signed distance on the unit circle, in [-0.5, 0.5)."""
    return (x + 0.5) % 1.0 - 0.5


def grain(tt, p, L, k, ph=0.0):
    """A Hann grain of width L (cycles) centred at p, carrying a sine of k cycles per cycle. Wraps the seam."""
    d = wrapd(tt - p)
    w = np.where(np.abs(d) < 0.5 * L, 0.5 + 0.5 * np.cos(TWO_PI * d / L), 0.0)
    return w * np.sin(TWO_PI * k * d + ph)


def scatter(pos, amp):
    """Deposit amp at fractional 1-based harmonic positions (linear split between neighbouring bins)."""
    m = np.zeros(N + 2)
    pos = np.asarray(pos, float)
    amp = np.asarray(amp, float) * np.ones_like(pos)
    i0 = np.floor(pos).astype(np.int64)
    fr = pos - i0
    s1 = (i0 >= 1) & (i0 <= N)
    np.add.at(m, i0[s1] - 1, (amp * (1.0 - fr))[s1])
    s2 = (i0 >= 0) & (i0 <= N - 1)
    np.add.at(m, i0[s2], (amp * fr)[s2])
    return m[:N]


def blep(fr, dt):
    """PolyBLEP residual for a unit step at phase 0 (fr = phase fraction, dt = phase increment per sample)."""
    o = np.zeros_like(fr)
    a = fr < dt
    x = fr[a] / dt[a]
    o[a] = x + x - x * x - 1.0
    b = fr > 1.0 - dt
    x = (fr[b] - 1.0) / dt[b]
    o[b] = x * x + x + x + 1.0
    return o


def phase_from_freq(f, ph0=0.0):
    """Instantaneous frequency f (cycles per CYCLE, one value per sample) -> phase in cycles, rescaled so the
    total number of cycles is an integer: the waveform then closes on itself at the seam."""
    ns = f.size
    tot = f.sum() / ns
    k = max(1.0, np.round(tot))
    f = f * (k / tot)
    dt = f / ns
    ph = ph0 + np.concatenate(([0.0], np.cumsum(dt)[:-1]))
    return ph, dt


def saw_ph(ph, dt):
    fr = ph % 1.0
    return 2.0 * fr - 1.0 - blep(fr, dt)


def sq_ph(ph, dt):
    fr = ph % 1.0
    s = np.where(fr < 0.5, 1.0, -1.0)
    return s + blep(fr, dt) - blep((fr + 0.5) % 1.0, dt)


def primes_to(k):
    s = np.ones(k + 1, dtype=bool)
    s[:2] = False
    for p in range(2, int(k ** 0.5) + 1):
        if s[p]:
            s[p * p::p] = False
    return np.nonzero(s)[0]


PRIMES = primes_to(N)
SCHROEDER = np.pi * n * (n - 1.0) / NMAX        # chirp phase law: harmonic k arrives at time k/NMAX of the cycle


# ════════════════════════════════════════════════════════════════════════════════════════════
#  A · GRAINS — a body carrying windowed bursts of high partials (TIME)
# ════════════════════════════════════════════════════════════════════════════════════════════
def grain_constellation():
    """Sine body carrying up to eight Hann grains of high partials, placed on golden-ratio spots.
    Frames: grain count 1 -> 8, grain partial 6 -> 420 with each grain drifting to its own multiple,
    grains shrink from 1/6 to 1/48 cycle and end louder than the body: a sine to a sparking star map."""
    y = np.zeros((F, T.size))
    body = np.sin(TWO_PI * T)
    for i, uu in enumerate(u):
        ncont = 1.0 + 7.0 * uu ** 0.7
        k0 = 6.0 * 70.0 ** uu
        L = (1.0 / 6.0) * (1.0 / 8.0) ** uu
        g = np.zeros_like(T)
        for j in range(8):
            a = float(sm(ncont - j))
            if a <= 0:
                continue
            p = (0.19 + j * 0.618034) % 1.0
            k = min(900.0, k0 * (1.0 + 0.29 * j) ** uu)
            g += a * grain(T, p, L, k)
        y[i] = body * (1.0 - 0.55 * uu) + (0.35 + 1.1 * uu) * g
    return tband(y)


def grain_funnel():
    """Twelve grains scattered round the cycle, all on partial 24. Frames: the grains slide together
    until they stack on one spot while their partials fan out from 24 to 8..600 — a sine with glitter
    collapsing into a single dense knot of a dozen pitches."""
    rng = np.random.default_rng(0x6A11)
    p0 = np.sort(rng.uniform(0.0, 1.0, 12))
    ratio = np.exp(rng.uniform(np.log(1.0 / 3.0), np.log(25.0), 12))
    y = np.zeros((F, T.size))
    body = np.sin(TWO_PI * T)
    for i, uu in enumerate(u):
        e = float(sm(uu) ** 1.3)
        g = np.zeros_like(T)
        for j in range(12):
            p = p0[j] + e * wrapd(0.5 - p0[j])
            k = min(900.0, 24.0 * ratio[j] ** uu)
            L = 0.07 - 0.03 * uu
            g += grain(T, p, L, k, ph=0.7 * j)
        y[i] = body * (1.0 - 0.6 * uu) + (0.45 + 0.25 * uu) * g
    return tband(y)


def sub_spark():
    """SUB. A full-weight sine that keeps its fundamental while sparks of a climbing partial ignite at its
    landmarks — upward zero crossing, downward one, both crests, then the four points between. Frames: spark
    partial 12 -> 900, spark width 1/10 -> 1/200 cycle, spark level -16 -> +2 dB against the sine."""
    y = np.zeros((F, T.size))
    body = np.sin(TWO_PI * T)
    spots = [(0.0, 0.0, 1.0), (0.5, 0.3, -1.0), (0.25, 0.5, 1.0), (0.75, 0.6, -1.0),
             (0.125, 0.75, 1.0), (0.625, 0.8, -1.0), (0.375, 0.85, 1.0), (0.875, 0.9, -1.0)]
    for i, uu in enumerate(u):
        k = 12.0 * (900.0 / 12.0) ** uu
        L = 0.1 * (1.0 / 20.0) ** uu
        a = 0.16 + 1.1 * uu ** 1.3
        g = np.zeros_like(T)
        for p, born, sgn in spots:
            gg = float(sm((uu - born) / 0.1)) if born > 0 else 1.0
            if gg <= 0:
                continue
            g += sgn * gg * grain(T, p, L, k)
        y[i] = body + a * g
    return tband(np.roll(y, T.size // 16, axis=1))


def spark_rain():
    """Sixty-four micro-grains, each a seeded partial from 20 to 900 at a seeded spot, switching on one by one.
    Frames: grain density 1 -> 64 while the sine body sinks to a quarter — a clean sine that ends as a
    crackling rain of pitched sparks."""
    rng = np.random.default_rng(0x5BA7C)
    K = 64
    pos = rng.uniform(0.0, 1.0, K)
    kk = np.exp(rng.uniform(np.log(20.0), np.log(900.0), K))
    cyc = rng.uniform(2.0, 6.0, K)
    ph = rng.uniform(0.0, TWO_PI, K)
    birth = (np.arange(K) / K) ** 1.25
    y = np.zeros((F, T.size))
    body = np.sin(TWO_PI * T)
    for i, uu in enumerate(u):
        g = np.zeros_like(T)
        for j in range(K):
            a = float(sm((uu - birth[j]) / 0.06))
            if a <= 0:
                continue
            g += a * grain(T, pos[j], cyc[j] / kk[j], kk[j], ph[j])
        y[i] = body * (1.0 - 0.75 * uu) + 0.9 * g
    return tband(y)


def burst_ladder():
    """Twelve slots round the cycle; slot j rings partial m*(j+1), so one cycle climbs a harmonic ladder.
    Frames: live slots 2 -> 12 and the rung multiplier m 3 -> 40, the sine body fading — a staircase of
    pitches inside every cycle that ends as a shrieking run up to partial 480."""
    y = np.zeros((F, T.size))
    body = np.sin(TWO_PI * T)
    for i, uu in enumerate(u):
        live = 2.0 + 10.0 * uu ** 0.8
        m = 3.0 * (40.0 / 3.0) ** uu
        g = np.zeros_like(T)
        for j in range(12):
            a = float(sm(live - j))
            if a <= 0:
                continue
            g += a * grain(T, (j + 0.5) / 12.0, 1.5 / 12.0, min(900.0, m * (j + 1)))
        y[i] = body * (1.0 - 0.7 * uu) + (0.5 + 0.4 * uu) * g
    return tband(y)


def crest_ringer():
    """A sine whose crests are ring-modulated by bursts of a band-limited saw at integer partial k, and soon its
    zero crossings by bursts at partial 3k/2: y = sin * (1 + a |sin|^p saw(k t)) + b |cos|^p saw(1.5k t).
    Frames: k 24 -> 300, burst sharpness p 1 -> 150 (the bursts become clicks), crest depth 1 -> 5: a sine with a
    fizz on its peaks to a wave torn at all four landmarks like a struck wire."""
    y = np.zeros((F, T.size))
    s = np.sin(TWO_PI * T)
    c = np.cos(TWO_PI * T)

    def blsaw(k):
        M = max(1, NMAX // k)
        mm = np.arange(1, M + 1)
        return (np.sin(TWO_PI * np.outer(mm * k, T)) / mm[:, None]).sum(axis=0) / 1.7

    for i, uu in enumerate(u):
        k = int(np.round(24.0 * (300.0 / 24.0) ** uu))
        k2 = int(np.round(1.5 * k))
        p = 1.0 + 149.0 * uu ** 1.7
        a = 1.0 + 4.0 * uu ** 1.2
        b = 1.8 * float(sm((uu - 0.08) / 0.5))
        y[i] = s * (1.0 + a * np.abs(s) ** p * blsaw(k)) + b * np.abs(c) ** p * blsaw(k2)
    return tband(y)


# ════════════════════════════════════════════════════════════════════════════════════════════
#  B · CHIRPS — the instantaneous frequency sweeps inside one cycle
# ════════════════════════════════════════════════════════════════════════════════════════════
def sine_chirp_fade():
    """A sine body plus a sin^2-faded exponential chirp that starts at partial 2 each cycle.
    Frames: the chirp's top 2 -> 800 partials and its level rises over the body: a warm double-sine that
    ends as a laser zip in every cycle."""
    y = np.zeros((F, T.size))
    W = np.sin(np.pi * T) ** 2
    for i, uu in enumerate(u):
        f0, f1 = 2.0, 2.0 * 400.0 ** uu
        r = f1 / f0
        Phi = f0 * T if r < 1.0001 else f0 * (r ** T - 1.0) / np.log(r)
        a = 0.45 + 0.5 * uu
        y[i] = (1.0 - a) * np.sin(TWO_PI * T) + a * W * np.sin(TWO_PI * Phi)
    return tband(y)


def square_siren():
    """ODD-ONLY siren. A polyBLEP square whose pitch rises and falls twice per cycle, the second pass the exact
    negative of the first (half-wave symmetry), so every frame keeps only odd harmonics. Frames: siren top
    1 -> 600 on a fat-topped path: a plain square that ends as two mirrored screaming sweeps."""
    Ns = SIZE * 8
    half = Ns // 2
    th = np.arange(half) / half
    y = np.zeros((F, Ns))
    for i, uu in enumerate(u):
        f1 = 600.0 ** (uu ** 0.8)
        fh = 1.0 + (f1 - 1.0) * (0.5 - 0.5 * np.cos(TWO_PI * th)) ** 0.7
        tot = fh.sum() / Ns
        tgt = np.floor(tot) + 0.5
        fh = fh * (tgt / tot)
        dt = fh / Ns
        ph_h = 0.25 + np.concatenate(([0.0], np.cumsum(dt)[:-1]))
        y[i] = sq_ph(np.concatenate((ph_h, ph_h + tgt)), np.concatenate((dt, dt)))
    return tband(y)


def chirp_braid():
    """Up to five faded exponential SAW chirps (polyBLEP), alternately rising and falling between partial 12 and a
    climbing top, staggered round the cycle so they cross like a braid over a sine that fades out. Frames: strands
    1 -> 5, top 24 -> 768: a sine with one buzzing whoop that ends as a knot of crossing zips over a hollow gap."""
    Ns = SIZE * 8
    tt = np.arange(Ns) / Ns
    y = np.zeros((F, Ns))
    for i, uu in enumerate(u):
        cnt = 1.0 + 4.0 * uu ** 0.8
        f0, f1 = 12.0, 24.0 * 32.0 ** uu
        r = f1 / f0
        g = 1.2 * np.sin(TWO_PI * tt) * (1.0 - uu) ** 1.5
        for j in range(5):
            a = float(sm(cnt - j)) * (0.35 + 0.65 * uu)
            if a <= 0:
                continue
            tau = (tt + j / 5.0 + 0.1) % 1.0
            if j % 2:
                tau = 1.0 - tau
            Phi = f0 * (r ** tau - 1.0) / np.log(r) + 0.37 * j
            dt = f0 * r ** tau / Ns
            g += a * np.sin(np.pi * tau) ** 2 * saw_ph(Phi, dt)
        y[i] = g
    return tband(y)


def arpeggio_inside():
    """An arpeggio played INSIDE one cycle: eight notes of a minor-ninth arpeggio, phase-continuous, with a
    glide between notes. Frames: arpeggio root 2 -> 48 partials, range 1 -> 4 octaves, glide 40% -> 2%
    (portamento to hard steps) — a warbling sine that ends as a staircase of pitched blocks."""
    Ns = SIZE * 4
    tt = np.arange(Ns) / Ns
    semis = np.array([0, 3, 7, 10, 14, 15, 19, 22], float)
    y = np.zeros((F, Ns))
    for i, uu in enumerate(u):
        root = 2.0 * 24.0 ** uu
        spread = 0.25 + 0.75 * uu
        notes = root * 2.0 ** (semis * spread * 2.2 / 12.0)
        pos = tt * 8.0
        j = np.floor(pos).astype(int) % 8
        fr = pos - np.floor(pos)
        gl = 0.4 * (0.05 ** uu)
        mix = sm(fr / max(gl, 1e-3))
        prev = notes[(j - 1) % 8]
        lf = np.log(prev) * (1.0 - mix) + np.log(notes[j]) * mix
        ph, dt = phase_from_freq(np.exp(lf))
        y[i] = np.sin(TWO_PI * ph)
    return tband(y)


def chirp_horizon():
    """A flat band of partials from K/8 to K in Schroeder (chirp) phase over a fundamental, so each cycle carries one
    linear chirp that lives only in the top three octaves of its range. Frames: K 2 -> 1000 (exponential) — a sine
    that grows a zipping chirp which climbs away from it, a hollow gap opening below."""
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        K = 2.0 * 500.0 ** uu
        lo = max(1.0, K / 8.0)
        hi_e = 1.0 / (1.0 + np.exp(np.clip((n - K) / (0.04 * K + 1.0), -60.0, 60.0)))
        lo_e = 1.0 / (1.0 + np.exp(np.clip(-(n - lo) / (0.08 * lo + 0.5), -60.0, 60.0)))
        m[i] = hi_e * lo_e * n ** 0.1
        m[i, 0] = 1.3 * m[i].max()
    return spec(m, SCHROEDER)


def pulse_scale_run():
    """A polyBLEP 1/8-duty pulse climbing a major-pentatonic run inside every cycle (seven notes, then the wrap
    drops back). Frames: bottom note 1 -> 40 partials, run range 0 -> 2 octaves, glide shrinking — a thin
    pulse that ends as a rising run of nasal pulse blips, a scale per cycle."""
    Ns = SIZE * 8
    tt = np.arange(Ns) / Ns
    semis = np.array([0, 2, 4, 7, 9, 12, 14], float)
    y = np.zeros((F, Ns))
    for i, uu in enumerate(u):
        bot = 40.0 ** uu
        notes = bot * 2.0 ** (semis * uu ** 0.6 * 1.7 / 12.0)
        pos = tt * 7.0
        j = np.floor(pos).astype(int) % 7
        fr = pos - np.floor(pos)
        mix = sm(fr / (0.3 * 0.1 ** uu + 1e-3))
        lf = np.log(notes[(j - 1) % 7]) * (1.0 - mix) + np.log(notes[j]) * mix
        ph, dt = phase_from_freq(np.maximum(np.exp(lf), 1.0), 0.3)
        y[i] = saw_ph(ph, dt) - saw_ph(ph + 0.125, dt)
    return tband(y)


def wavelet_chirp():
    """A single log-Gaussian band of partials in quadratic (dispersive) phase, centred mid-cycle, so the cycle
    shows one chirping wave packet over a quiet fundamental. Frames: band centre 2 -> 400, band width
    0.25 -> 2.5 octaves, dispersion 0.5 -> 4 wraps — a pure blip that stretches into a long wide zip."""
    out = np.zeros((F, SIZE))
    for i, uu in enumerate(u):
        c = 1.0 + np.log2(200.0) * uu
        sg = 0.25 * 10.0 ** uu
        mg = np.exp(-((ln - c) ** 2) / (2 * sg * sg))
        mg[0] = max(mg[0], 0.25)
        mg = mg * (n <= NMAX)
        ph = np.pi * (0.5 + 3.5 * uu ** 1.2) * n * n / NMAX + np.pi * n
        out[i] = wtlib.cycles_from_mags(mg[None, :], ph)[0]
    return wtlib.finalize(out)


# ════════════════════════════════════════════════════════════════════════════════════════════
#  C · CARPETS AND SWARMS — many seeded partials with a plan (SPEC)


# ════════════════════════════════════════════════════════════════════════════════════════════


def fold_oct(x, top=9.96):
    """Reflect log2 positions into [0, top] (partials bounce off the spectrum's floor and ceiling)."""
    x = np.mod(x, 2.0 * top)
    return np.where(x > top, 2.0 * top - x, x)


def carpet_tide():
    """WILD FROM FRAME 0. All 1000 partials, each with a seeded weight and its own amplitude LFO (1..5 cycles across
    the table). Frames: the tilt opens from dark (-6 dB/oct) to nearly white while the LFO exponent sharpens
    1.5 -> 7: a glittering, rippled saw that breaks into a carpet of isolated needles."""
    rng = np.random.default_rng(0xCA7E7)
    c = rng.integers(1, 6, N)
    ph = rng.uniform(0.0, TWO_PI, N)
    base = rng.uniform(0.3, 1.0, N)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        tilt = 1.0 - 0.95 * uu
        p = 1.5 + 5.5 * uu ** 1.5
        wv = 0.5 + 0.5 * np.sin(TWO_PI * c * uu + ph)
        m[i] = n ** -tilt * base * wv ** p
        m[i, 0] = 1.0
    return spec_sch(m, 1.0)


def partial_dunes():
    """600 seeded partials (2..900) whose amplitudes swell in dunes that roll diagonally through the spectrum:
    a_j = (0.5 + 0.5 cos(2 pi (C(u) - 0.35 log2 p_j) + phi_j))^3, C running 0 -> 13 cycles, accelerating.
    Frames: the dunes speed up while the tilt opens from dark to flat — spiky ridges marching across the table."""
    rng = np.random.default_rng(0xD0E5)
    K = 600
    pos = np.round(np.exp(rng.uniform(np.log(2.0), np.log(900.0), K)))
    w = rng.uniform(0.4, 1.0, K)
    phi = rng.uniform(0.0, 0.35, K)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        C = 13.0 * uu ** 1.3
        a = w * (0.5 + 0.5 * np.cos(TWO_PI * (C - 0.35 * np.log2(pos)) + TWO_PI * phi)) ** 3
        g = scatter(pos, a * pos ** -(1.2 - 1.2 * uu))
        g[0] += 1.0
        g[1] += 0.4
        m[i] = g
    return spec_sch(m, 1.0)


def partial_migration():
    """A 256-partial saw whose partials each migrate at their own seeded speed, up or down, bouncing off the
    floor and ceiling of the spectrum while the tilt opens; the fundamental stays as the anchor. Frames: drift
    0 -> +-3 octaves — a saw that dissolves into a scattered inharmonic flock, loud partials shrieking up high."""
    rng = np.random.default_rng(0x316A)
    K = 256
    start = np.arange(1, K + 1, dtype=float)
    v = rng.normal(0.0, 1.0, K) * 3.0
    v[0] = 0.0
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        lp = fold_oct(np.log2(start) + v * sm(uu) ** 1.2)
        m[i] = scatter(2.0 ** lp, start ** -(0.95 - 0.75 * uu))
    return spec_sch(m, 1.0)


def moth_eaten_saw():
    """A saw whose partials are eaten one by one on a seeded schedule until 3% survive, the survivors growing
    louder and the tilt opening as the rest disappear. Frames: 0 -> 97% of partials gone (fundamental kept) — a
    bright saw that ends as a sparse, randomly spaced glass chord."""
    rng = np.random.default_rng(0x307E)
    death = rng.uniform(0.02, 1.03, N)
    death[0] = 9.0
    surv = death > 1.0
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        alive = 1.0 - sm((uu - death) / 0.035)
        m[i] = n ** -(0.85 - 0.7 * uu) * alive * (1.0 + 6.0 * uu * surv)
    return spec_sin(m)


def brownian_partials():
    """The first 200 harmonics of a saw, each doing a seeded Brownian random walk in log-frequency with a step
    size that grows across the table, the tilt opening as they wander; the fundamental never moves. Frames: step
    0.004 -> 0.054 octave per frame — a saw that goes out of tune, smears, then scatters into a bright drifting cloud."""
    rng = np.random.default_rng(0xB70C)
    K = 200
    start = np.arange(1, K + 1, dtype=float)
    sig = 0.004 + 0.05 * u ** 2
    walk = np.cumsum(rng.normal(0.0, 1.0, (F, K)) * sig[:, None], axis=0)
    walk -= walk[0]
    walk[:, 0] = 0.0
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        m[i] = scatter(2.0 ** fold_oct(np.log2(start) + walk[i]), start ** -(1.1 - 0.85 * uu))
    return spec_sch(m, 1.0)


# ════════════════════════════════════════════════════════════════════════════════════════════
#  D · CHORDS AND SCALES INSIDE ONE CYCLE (SPEC)


# ════════════════════════════════════════════════════════════════════════════════════════════
CHORDS = [
    [1.0, 6 / 5, 3 / 2],                          # minor
    [1.0, 6 / 5, 3 / 2, 9 / 5],                   # minor 7
    [1.0, 9 / 8, 6 / 5, 3 / 2, 9 / 5],            # minor 9
    [1.0, 4 / 3, 3 / 2],                          # sus4
    [1.0, 9 / 8, 3 / 2],                          # sus2
    [1.0, 5 / 4, 45 / 32, 3 / 2, 15 / 8],         # maj7 #11
    [1.0, 16 / 15, 9 / 8, 6 / 5],                 # cluster
    list(2.0 ** (np.arange(24) / 24.0)),          # quarter-tone cloud
]


def chord_cathedral():
    """Eight chords in just intonation, every chord tone a small saw of its own and doubled in every octave over a
    root three octaves above the note (so it plays as a chord over a sub), with a loudness envelope climbing the
    octaves. Frames: minor -> m7 -> m9 -> sus4 -> sus2 -> maj7#11 -> cluster -> a quarter-tone cloud, the envelope
    rising 3 -> 8.8 octaves: an organ chord that ends as a bright microtonal smear."""
    C = len(CHORDS)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        ec, ew = 3.0 + 5.8 * uu, 1.25
        p = uu * (C - 1)
        j = min(int(np.floor(p)), C - 2)
        fr = float(sm((p - j - 0.5) * 6.0 + 0.5))
        g = np.zeros(N)
        for cc, wgt in ((CHORDS[j], 1.0 - fr), (CHORDS[j + 1], fr)):
            if wgt <= 0:
                continue
            for r in cc:
                for o in range(0, 7):
                    f0 = 8.0 * r * 2.0 ** o
                    if f0 > NMAX:
                        break
                    e = wgt * np.exp(-((np.log2(f0) - ec) ** 2) / (2 * ew ** 2))
                    hh = np.arange(1, int(NMAX / f0) + 1)
                    pos = np.round(f0 * hh).astype(int)
                    np.add.at(g, pos - 1, e * hh ** -1.6)
        g[0] += 0.55 * g.max()
        g[1] += 0.25 * g.max()
        m[i] = g
    return spec_sch(m, 2.0)


def saw_cluster():
    """A cluster of saws on integer multipliers m, all summed into one cycle: amp_n = sum_{m in S, m | n} (m/n)^a.
    Frames: the multiplier window S slides and widens from {1, 2} to {25..35} while the saws brighten (a 0.9 -> 0.4)
    — a saw that turns into a divisibility lattice where only partials with a divisor in the window survive."""
    ms = np.arange(1, 49)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        mc = 1.5 * 20.0 ** uu
        mw = 0.8 + 5.0 * uu
        wts = np.exp(-((ms - mc) / mw) ** 2)
        g = np.zeros(N)
        for mm, wt in zip(ms, wts):
            if wt < 1e-3:
                continue
            k = np.arange(mm, N + 1, mm)
            g[k - 1] += wt * (mm / k) ** (0.9 - 0.5 * uu)
        m[i] = g
    return spec_sch(m, 1.0)


def inversion_elevator():
    """A major triad of three saws (4:5:6 over the note) climbing through nine inversions: each step the lowest
    tone glides up an octave, passing through inharmonic, off-grid positions on the way, the saws brightening.
    Frames: the voicing rises three octaves — a warm chord that ends as a shrill stacked triad, glides smearing."""
    tones = [np.log2(4.0), np.log2(5.0), np.log2(6.0)]
    keys = [list(tones)]
    for _ in range(9):
        t = list(keys[-1])
        t[int(np.argmin(t))] += 1.0
        keys.append(t)
    keys = np.array(keys)
    S = len(keys) - 1
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        p = uu * S
        j = min(int(np.floor(p)), S - 1)
        fr = float(sm(p - j))
        lt = keys[j] * (1.0 - fr) + keys[j + 1] * fr
        g = np.zeros(N)
        for tl in lt:
            f0 = 2.0 ** tl
            kk = np.arange(1, int(NMAX / f0) + 1)
            g += scatter(f0 * kk, kk ** -(1.1 - 0.7 * uu))
        m[i] = g
    return spec_sch(m, 1.0)


def sub_chord_halo():
    """SUB. A sine-weight fundamental with a halo of chord tones (root 16, three harmonics each, doubled to the
    top) that rises from -42 dB to -2 dB and changes chord as it comes: minor -> diminished -> augmented ->
    cluster -> the cluster shattering into seeded off-chord positions. The sub never moves; the halo becomes a storm."""
    rng = np.random.default_rng(0x5C4A)
    chords = [[1.0, 6 / 5, 3 / 2], [1.0, 6 / 5, 36 / 25], [1.0, 5 / 4, 25 / 16], [1.0, 16 / 15, 9 / 8, 6 / 5, 5 / 4]]
    jit = rng.normal(0.0, 1.0, 2000)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        p = min(uu / 0.75, 1.0) * (len(chords) - 1)
        j = min(int(np.floor(p)), len(chords) - 2)
        fr = float(sm(p - j))
        shat = float(sm((uu - 0.68) / 0.32)) * 0.6
        g = np.zeros(N)
        q = 0
        for cc, wgt in ((chords[j], 1.0 - fr), (chords[j + 1], fr)):
            for r in cc:
                for o in range(0, 7):
                    for h in (1, 2, 3):
                        pos = 16.0 * r * h * 2.0 ** (o + shat * jit[q % 2000])
                        q += 1
                        if 1.0 <= pos <= NMAX:
                            g[int(np.round(pos)) - 1] += wgt * pos ** -0.2 / h
        lvl = 10.0 ** ((-42.0 + 40.0 * uu ** 0.8) / 20.0)
        g = g / max(g.max(), 1e-9) * lvl
        g[0] += 1.0
        g[1] += 0.3
        g[2] += 0.08
        m[i] = g
    return spec(m)


def pentatonic_partials():
    """A major-pentatonic scale laid across ten octaves as partials, each scale tone a small saw of its own,
    snapped to the grid — the whole scale sounding at once. Frames: the scale's root slides from the note itself up
    4.3 octaves while the tilt opens: a hollow organ cluster that ends as a bright pentatonic bell cloud."""
    semis = np.array([0, 2, 4, 7, 9], float)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        R = 20.0 ** uu
        tones = (R * 2.0 ** ((semis[None, :] + 12.0 * np.arange(11)[:, None]) / 12.0)).ravel()
        tones = tones[tones <= NMAX]
        g = np.zeros(N)
        for f0 in tones:
            hh = np.arange(1, int(NMAX / f0) + 1)
            np.add.at(g, np.round(f0 * hh).astype(int) - 1, f0 ** -(0.9 - 0.75 * uu) * hh ** -1.5)
        m[i] = g
    return spec_sch(m, 3.0)


def whole_tone_ladder():
    """A band-limited square body with a whole-tone scale of partials (six per octave, snapped) laid over it;
    a bright window climbs the whole-tone ladder while the square fades to a whisper. Frames: window 3 -> 9.6
    octaves, body 0 -> -30 dB: a hollow square that ends as a glinting ladder of whole tones high above."""
    pos = 6.0 * 2.0 ** (np.arange(0, 60) / 6.0)
    pos = pos[pos <= NMAX]
    kk = np.round(pos).astype(int)
    odd = (ni % 2 == 1)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        c = 3.0 + 6.6 * uu
        win = np.exp(-((np.log2(pos) - c) ** 2) / (2 * 0.7 ** 2))
        rung = 1.0 - 0.5 * (np.arange(len(pos)) % 2)
        g = np.zeros(N)
        np.add.at(g, kk - 1, win * rung * (0.25 + 0.75 * uu))
        g += odd * n ** -1.0 * 10.0 ** (-30.0 * uu / 20.0)
        m[i] = g
    return spec_sin(m, 0.05)


def tritave_partials():
    """Bohlen-Pierce microtonal partials: a stack of pure twelfths (1, 3, 9, 27, 81, 243, 729: the hollow tritave
    organ) gains the other twelve steps of the 13-per-tritave scale one at a time, each tone carrying a faint 3rd
    and 5th harmonic (odd, as BP intends). Frames: steps 1 -> 13 while the tilt opens — a hollow organ that ends as
    a bright, dense non-octave microtonal cloud."""
    order = [0, 6, 9, 3, 4, 10, 7, 1, 12, 2, 5, 8, 11]
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        live = 1.0 + 12.0 * uu ** 0.9
        g = np.zeros(N)
        for rank, st in enumerate(order):
            a = float(sm(live - rank))
            if a <= 0:
                continue
            for f0 in 3.0 ** (np.arange(0, 7) + st / 13.0):
                for h, hg in ((1, 1.0), (3, 0.22), (5, 0.09)):
                    pos = f0 * h
                    if pos <= NMAX:
                        g[int(np.round(pos)) - 1] += a * hg * f0 ** -(0.95 - 1.0 * uu)
        m[i] = g
    return spec_sin(m)


def pitch_grid_saw():
    """A saw whose harmonics are snapped to a pitch grid of s semitones (12 log2 k -> nearest multiple of s),
    their energy piling onto the grid points while the tilt opens. Frames: grid 0.25 -> 19.02 semitones through
    quarter tones, chromatic, whole tone, minor third ... to the tritave, where everything collapses onto
    1, 3, 9, 27, 81, 243, 729: a saw that becomes a snapped, clustered, finally hollow stack."""
    pk = 12.0 * np.log2(n[:NMAX])
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        amp = n[:NMAX] ** -(1.0 - 0.8 * uu)
        s = 0.25 * (19.02 / 0.25) ** uu
        q = s * np.round(pk / s)
        kk = np.round(2.0 ** (q / 12.0)).astype(int)
        ok = (kk >= 1) & (kk <= NMAX)
        g = np.zeros(N)
        np.add.at(g, kk[ok] - 1, amp[ok])
        m[i] = g ** 0.75
    return spec_sin(m)


# ════════════════════════════════════════════════════════════════════════════════════════════
#  E · HOLES, COMBS, NUMBER SERIES (SPEC)


# ════════════════════════════════════════════════════════════════════════════════════════════


def _divisor_count(K):
    d = np.zeros(K + 1, dtype=int)
    for i in range(1, K + 1):
        d[i::i] += 1
    return d[1:]


def _big_omega(K):
    om = np.zeros(K + 1, dtype=int)
    for p in primes_to(K):
        pk = p
        while pk <= K:
            om[pk::pk] += 1
            pk *= p
    return om[1:]


def _digit_sum(K, b):
    k = np.arange(1, K + 1)
    s = np.zeros(K, dtype=int)
    while k.any():
        s += k % b
        k = k // b
    return s


DIV = _divisor_count(N)
OMEGA = _big_omega(N)


def canyon_sub():
    """SUB. A saw with a canyon cut through its middle: harmonics 1-2 always stay, everything from 3 up to a
    rising edge is removed, and the air above the edge flattens and brightens. Frames: canyon edge 3 -> 400 —
    a saw that splits into a sub and a sheet of air with nothing in between."""
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        hi = 3.0 * (400.0 / 3.0) ** uu
        air = (n / hi) ** -(0.9 - 0.78 * uu) * 10.0 ** ((-4.0 - 10.0 * uu) / 20.0) * hi ** -0.9 / hi ** -0.9
        gate = sm((n - hi) / (0.06 * hi + 1.0) + 0.5)
        g = air * gate * (n >= 3)
        g[0] = 1.0
        g[1] = 0.45
        m[i] = g
    return spec_sch(m, 1.0)


def barber_pole_comb():
    """A comb whose teeth are spaced evenly in OCTAVES (a phaser's notches, not a flanger's) and scroll upward
    forever like a barber pole. Frames: teeth per octave 0.6 -> 10, scroll 0 -> 4 teeth, tooth sharpness
    1.5 -> 7.5 — a gently phased saw that ends as a razor-toothed spiral of needles."""
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        D = 0.6 * (10.0 / 0.6) ** uu
        comb = (0.5 + 0.5 * np.cos(TWO_PI * (D * ln - 4.0 * uu))) ** (1.5 + 6.0 * uu)
        m[i] = n ** -(0.95 - 0.55 * uu) * (0.02 + 0.98 * comb)
    return spec_sch(m, 2.0)


def divisor_storm():
    """Each harmonic weighted by its number of divisors raised to a growing power: amp = k^-0.7 (d(k)/log2(2k))^p.
    Frames: p 0.6 -> 6 — a grainy saw that crystallises onto the highly composite harmonics (12, 24, 36, 60, 120,
    360, 720, 840 ...), a stack of fifths and octaves ringing four octaves above the note."""
    dn = DIV / np.log2(2.0 * n)
    dn = dn / dn.max()
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        p = 0.6 + 5.4 * uu ** 1.2
        m[i] = n ** -0.7 * dn ** p
        m[i, 0] = max(m[i, 0], 0.3 * m[i].max())
    return spec_sch(m, 1.0)


def collatz_trail():
    """The Collatz orbit of 27 (111 steps, peaks at 9232) played as a hopping partial: each value is folded into
    range by octaves, the head is loud and every visited partial leaves a slowly decaying trail over a faint bed
    that brightens. Frames: steps 0 -> 111 — one bright partial over a thin saw that ends as a chaotic
    constellation of every harmonic the orbit touched."""
    orb = [27]
    while orb[-1] != 1:
        v = orb[-1]
        orb.append(v // 2 if v % 2 == 0 else 3 * v + 1)
    orb = np.array(orb, float)
    while (orb > NMAX).any():
        orb = np.where(orb > NMAX, orb / 2.0, orb)
    L = len(orb) - 1
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        h = uu * L
        age = h - np.arange(L + 1)
        a = np.where(age >= 0, 0.94 ** age, np.clip(1.0 + age, 0.0, 1.0))
        m[i] = 0.12 * n ** -(1.0 - 0.6 * uu) + scatter(orb, a)
    return spec_sch(m, 1.0)


def prime_factor_scan():
    """Harmonics chosen by how many prime factors they have (with multiplicity, Omega): a window on Omega slides
    from 1 (the primes) through 2 (semiprimes) ... to 9 (512, 768), over a held fundamental, the tilt opening from
    dark to flat. Frames: a dense prime shimmer that thins, level by level, into a sparse high chord of smooth numbers."""
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        c = 0.6 + 8.6 * uu
        m[i] = n ** -(0.9 - 0.9 * uu) * np.exp(-((OMEGA - c) ** 2) / (2 * 0.45 ** 2))
        m[i, 0] = 0.6 * m[i].max()
    return spec_sch(m, 1.0)


def beatty_gate():
    """WILD FROM FRAME 0. A Sturmian (Beatty) sequence gates the harmonics: partial k sounds when
    floor((k+1)a) - floor(k a) = 1. Frames: a starts on the golden ratio 0.618 (the Fibonacci word: a
    quasi-periodic lattice that never repeats) and falls to 0.06 while the tilt opens — gaps widening into a bright,
    sparse, rotation-ordered picket of partials."""
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        a = 0.618034 * (0.06 / 0.618034) ** uu
        mask = np.floor((n + 1.0) * a) - np.floor(n * a)
        m[i] = n ** -(0.9 - 0.75 * uu) * (0.004 + mask)
        m[i, 0] = 1.0 - 0.6 * uu
    return spec_sin(m)


def digit_sum_walk():
    """Harmonic k is weighted by exp(-L (s_b(k) - 1)), s_b = the digit sum of k in base b, so numbers that are
    'round' in base b ring and the rest fade. Frames: base 2 -> 3 -> 4 -> 5 -> 6 -> 8 -> 10 -> 12 -> 16 with the
    weight L rising 0.3 -> 2.2: a saw that becomes a powers-of-b tree, a different number system every few frames."""
    bases = [2, 3, 4, 5, 6, 8, 10, 12, 16]
    ds = np.array([_digit_sum(N, b) for b in bases], float)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        p = uu * (len(bases) - 1)
        j = min(int(np.floor(p)), len(bases) - 2)
        fr = float(sm((p - j - 0.5) * 3.0 + 0.5))
        Lw = 0.3 + 1.9 * uu
        s = ds[j] * (1.0 - fr) + ds[j + 1] * fr
        m[i] = n ** -0.5 * np.exp(-Lw * (s - 1.0))
    return spec_sch(m, 2.0)


# ════════════════════════════════════════════════════════════════════════════════════════════
#  F · TILT DANCES, SHIFTED SETS, SPECTRAL ILLUSIONS (SPEC)


# ════════════════════════════════════════════════════════════════════════════════════════════


def seesaw_pivot():
    """The spectrum balanced on a pivot that climbs from harmonic 2 to 720: around it the partials fall away
    (a peak) or rise away (a valley), the sign flipping faster and faster and swinging harder. Frames: pivot
    2 -> 720, see-saw swings 0 -> 7, depth 0.6 -> 2 — a tilt dance ending between a whistle and a hollow scoop."""
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        P = 2.0 ** (1.0 + 8.5 * sm(uu))
        s = (0.6 + 1.4 * uu) * np.sin(TWO_PI * (0.75 * uu + 3.2 * uu ** 2.2))
        m[i] = np.exp(-s * np.abs(np.log(n / P))) * n ** -0.3
    return spec_sch(m, 1.0)


def scissor_tilt():
    """Odd and even harmonics on two opposing tilts that cross like scissor blades, three times, harder each time.
    Frames: odd tilt 1.6 -> 0.3, even tilt 0.2 -> 1.6, crossing swing +-1.0 — hollow square, saw, a bright
    octave-up saw, and back, ending with blazing odd partials over dark evens."""
    odd = (ni % 2 == 1)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        wob = 1.0 * np.sin(TWO_PI * 3.0 * uu ** 1.3)
        so = 1.6 - 1.3 * uu + wob
        se = 0.2 + 1.4 * uu - wob
        m[i] = np.where(odd, n ** -so, n ** -se)
    return spec_sch(m, 1.0)


def snap_grid():
    """WILD FROM FRAME 0. An inharmonic series f_j = r j + s (r not an integer) snapped to the nearest harmonic,
    over a held fundamental, drawn in sine phase so the lattice shows as spikes. Frames: spacing r 2.3 -> 11.3 and
    offset s swinging +-2.9 — an irregular rounding pattern (gaps of 2, 3, 2, 2, 3 ...) that spreads into a bright
    bell-like grid with a jagged rhythm to its partials."""
    j = np.arange(1, NMAX + 1, dtype=float)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        r = 2.3 + 9.0 * sm(uu) ** 1.1
        s = 2.9 * np.sin(1.5 * np.pi * uu)
        pos = np.round(r * j + s)
        ok = (pos >= 1) & (pos <= NMAX)
        g = np.zeros(N)
        np.add.at(g, pos[ok].astype(int) - 1, j[ok] ** -(1.0 - 0.85 * uu))
        g[0] += 0.6 * g.max() * (1.0 - 0.7 * uu)
        m[i] = g
    return spec_sin(m, 0.1)


def mirror_shift():
    """WILD FROM FRAME 0. An odd-harmonic series frequency-shifted DOWN by D, partials passing below zero reflecting
    back up, so two interleaved series cross each other. Frames: D 20 -> 380 — a square already folded into its own
    mirror image that ends as a pair of interfering combs with a bright seam at the shift."""
    j = np.arange(0, NMAX // 2, dtype=float)
    k = 2.0 * j + 1.0
    amp = k ** -0.9
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        D = 20.0 + 360.0 * uu ** 1.6
        pos = np.abs(k - D)
        ok = (pos >= 1.0) & (pos <= NMAX)
        g = scatter(pos[ok], amp[ok])
        g[0] += 0.4 * g.max()
        m[i] = g
    return spec(m)


def shepard_spiral():
    """SEAMLESS LOOP. A Shepard-Risset illusion: octave families of a major-ninth chord (1, 9/8, 5/4, 3/2, 15/8)
    under a bell-shaped loudness envelope in log-frequency, every partial rising two octaves across the table while
    the envelope orbits two octaves; the last frame flows back into the first. Scan it with an LFO: it rises forever."""
    fam = np.array([1.0, 9 / 8, 5 / 4, 3 / 2, 15 / 8])
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        c = 5.4 + 2.0 * np.sin(TWO_PI * uu)
        pos = (fam[None, :] * 2.0 ** (np.arange(-2, 11)[:, None] + 2.0 * uu)).ravel()
        pos = pos[(pos >= 1.0) & (pos <= NMAX)]
        a = np.exp(-((np.log2(pos) - c) ** 2) / (2 * 1.2 ** 2))
        m[i] = scatter(pos, a)
    return spec(m)


def undertone_rain():
    """The undertone series: partials at V/1, V/2, V/3 ... (the harmonic series upside down), snapped to the
    grid, the top one loudest. Frames: V 8 -> 1000 — a low cluster that rises into a bright top-heavy spray whose
    gaps shrink as it falls toward the fundamental, the mirror image of a saw."""
    j = np.arange(1, 201, dtype=float)
    amp = j ** -0.8
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        V = 8.0 * (NMAX / 8.0) ** uu
        pos = V / j
        ok = pos >= 1.0
        g = scatter(pos[ok], amp[ok])
        g[0] += 0.15 * g.max()
        m[i] = g
    return spec_sin(m, 0.0)


def interval_shadow():
    """Every partial of a square (odd harmonics) casts two shadows, one a musical interval above it and one that
    interval twice, snapped to the grid, while the tilt opens. Frames: the interval walks m2, M2, m3, M3, P4,
    tritone, P5, m6, M6, m7, M7, octave, ninth — a hollow square that grows a parallel-interval ghost."""
    iv = np.log2([16 / 15, 9 / 8, 6 / 5, 5 / 4, 4 / 3, 45 / 32, 3 / 2, 8 / 5, 5 / 3, 16 / 9, 15 / 8, 2.0, 9 / 4])
    k = np.arange(1, NMAX + 1, 2, dtype=float)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        base = k ** -(1.05 - 0.8 * uu)
        p = uu * (len(iv) - 1)
        j = min(int(np.floor(p)), len(iv) - 2)
        fr = float(sm((p - j - 0.5) * 2.5 + 0.5))
        rho = 2.0 ** (iv[j] * (1.0 - fr) + iv[j + 1] * fr)
        g = np.zeros(N)
        np.add.at(g, k.astype(int) - 1, base)
        for mult, gg in ((rho, 0.85), (rho * rho, 0.55)):
            pos = np.round(k * mult)
            ok = pos <= NMAX
            np.add.at(g, pos[ok].astype(int) - 1, gg * (0.2 + 0.8 * min(1.0, uu * 4)) * base[ok])
        m[i] = g
    return spec_sch(m, 1.0)


def subset_counter():
    """WILD FROM FRAME 0. Every combination of seven partial sets, counted in Gray code across the 128 frames (one set
    toggles per frame, starting mid-count): odd 3-99, even 2-64, primes above 100, perfect squares, multiples of 7,
    Fibonacci numbers, and a dense air bed above 200, all over a held fundamental. A spectral sequencer: scan it or
    random-pick the frames. Level fix (fb638 critique): count 0 is the fundamental alone (a sine, -3 dB RMS) and
    its neighbour 127 is the air bed alone, which the plain Schroeder law squeezed into one needle (-10.8 dB): a
    7.75 dB pop at frame 83. A 0.45 time-map floor spreads the air bed (-7.6 dB; the step is now 4.6 dB); every
    toggle and every magnitude is unchanged."""
    sets = []
    k = n
    sets.append(((ni % 2 == 1) & (ni >= 3) & (ni <= 99)) * k ** -0.8)
    sets.append(((ni % 2 == 0) & (ni <= 64)) * k ** -0.7)
    pm = np.zeros(N, bool)
    pm[PRIMES - 1] = True
    sets.append((pm & (ni > 100)) * 0.25 * k ** -0.2)
    sq = np.zeros(N)
    sq[(np.arange(2, 32) ** 2) - 1] = 1.0
    sets.append(sq * 0.7 * k ** -0.3)
    sets.append(((ni % 7 == 0)) * k ** -0.55)
    fb = np.zeros(N)
    a, b = 2, 3
    while a <= N:
        fb[a - 1] = 1.0
        a, b = b, a + b
    sets.append(fb * 0.8 * k ** -0.2)
    sets.append((ni >= 200) * 0.02)
    sets = np.array(sets)
    m = np.zeros((F, N))
    for i in range(F):
        c = (i + 45) % 128
        gc = c ^ (c >> 1)
        g = np.zeros(N)
        for b_ in range(7):
            if (gc >> b_) & 1:
                g += sets[b_]
        g[0] += 1.0
        m[i] = g
    return spec_sch(m, 1.0, floor=0.45)


def partial_crossfire():
    """Two flocks of twelve partials, each partial carrying its own harmonic series: a loud flock low (2..13) and a
    faint one high (300..900). Frames: they glide toward each other in log-frequency on staggered schedules, pass
    through each other mid-table and swap registers — a low buzz that flies up into a shrieking inharmonic tangle."""
    rng = np.random.default_rng(0xC055)
    A = np.log2(np.arange(2, 14, dtype=float))
    B = np.log2(np.sort(np.exp(rng.uniform(np.log(300.0), np.log(900.0), 12))))
    start = np.concatenate((A, B))
    end = np.concatenate((B[::-1] - 0.3, A[::-1] + 0.2))
    delay = rng.uniform(0.0, 0.35, 24)
    amp0 = np.concatenate((np.arange(1, 13) ** -0.3, np.full(12, 0.18)))
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        e = sm((uu - delay) / 0.65)
        lp = start * (1.0 - e) + end * e
        g = np.zeros(N)
        for f0, a0 in zip(2.0 ** lp, amp0):
            kk = np.arange(1, max(1, int(NMAX / f0)) + 1)
            g += scatter(f0 * kk, a0 * kk ** -1.1)
        g[0] += 0.5 * g.max() * (1.0 - 0.8 * uu)
        m[i] = g
    return spec_sch(m, 1.0)


def shuffled_saw():
    """WILD FROM FRAME 0. A saw whose partial amplitudes are shuffled by seeded swaps (the fundamental is never
    touched), drawn in sine phase so you SEE the ramp break: frame 0 already carries 150 swaps (a stuttering,
    notched saw); then the swaps come faster and reach further. Frames: 16 -> 76 swaps per frame, reach
    8 -> 508 harmonics — its loud low partials end up anywhere, a random carpet spending a saw's budget."""
    rng = np.random.default_rng(0x5ADF)
    saw = n ** -1.0
    saw[NMAX:] = 0.0
    perm = np.arange(N)

    def swaps(S, R):
        for _ in range(S):
            a = int(rng.integers(1, NMAX))
            b = int(np.clip(a + rng.integers(-R, R + 1), 1, NMAX - 1))
            perm[a], perm[b] = perm[b], perm[a]

    swaps(150, 200)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        swaps(int(16 + 60 * uu), int(8 + 500 * uu))
        m[i] = saw[perm]
    return spec_sin(m)


def sub_under_static():
    """SUB. A pure sub (fundamental plus a little second) under a seeded random carpet of 1000 partials whose
    amplitudes each cycle 1..4 times; the carpet rises from -66 dB (inaudible) to -6 dB. Frames: a clean sine
    that ends as a sub pinned under a roaring wall of static."""
    rng = np.random.default_rng(0x5057)
    base = rng.uniform(0.2, 1.0, N) ** 2
    c = rng.integers(1, 5, N)
    ph = rng.uniform(0.0, TWO_PI, N)
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        car = n ** -0.25 * base * (0.35 + 0.65 * (0.5 + 0.5 * np.sin(TWO_PI * c * uu + ph)))
        car[:2] = 0.0
        g = car / car.max() * 10.0 ** ((-66.0 + 60.0 * uu ** 0.9) / 20.0)
        g[0] = 1.0
        g[1] = 0.28
        m[i] = g
    return spec(m)


def square_crown():
    """A square wearing a crown: unipolar Hann spikes carrying a climbing partial ride ONLY its top plateau and
    stab upward out of it, so the wave loses its odd-only symmetry, while the square itself fades back.
    Frames: spikes 1 -> 5, spike partial 15 -> 700, crown 0.8 -> 2.4 of the plateau, square 1 -> 0.2: a square
    with one jewel that ends as a bare row of shrieking upward teeth."""
    Ns = SIZE * 8
    tt = np.arange(Ns) / Ns
    sq = sq_ph(tt, np.full(Ns, 1.0 / Ns))
    y = np.zeros((F, Ns))
    for i, uu in enumerate(u):
        nb = 1.0 + 4.0 * uu
        k = 15.0 * (700.0 / 15.0) ** (uu ** 0.7)
        a = 0.8 + 1.6 * uu ** 0.7
        L = 0.09 + 0.03 * uu
        crown = np.zeros(Ns)
        for rank, j in enumerate((2, 1, 3, 0, 4)):
            g = float(sm(nb - rank))
            if g <= 0:
                continue
            d = wrapd(tt - (0.05 + 0.1 * j))
            w = np.where(np.abs(d) < 0.5 * L, 0.5 + 0.5 * np.cos(TWO_PI * d / L), 0.0)
            crown += g * w * (0.5 + 0.5 * np.cos(TWO_PI * k * d))
        y[i] = sq * (1.0 - 0.8 * sm(uu)) + a * crown
    return tband(np.roll(y, Ns // 4, axis=1))


# ════════════════════════════════════════════════════════════════════════════════════════════
#  G · MORE PLANS — bumps, avalanches, polyrhythms, strums


# ════════════════════════════════════════════════════════════════════════════════════════════


def formant_picket():
    """One bright spectral bump multiplies into a picket fence: B Gaussian bumps in log-frequency, spread evenly
    from harmonic 2 to 630, each narrowing as their number grows, over a faint saw bed. Frames: B 1 -> 12 —
    a single vowel-like hump that splits into a fence of a dozen formants, sharp as combs by the end.
    Level fix (fb638 critique): each new bump fades in at the TOP of the spectrum, where the table's average
    power is near zero, so the plain Schroeder law made it arrive all at once: single-frame holes at frames 5
    and 16 (-15 dB against -9, crest 6). A 0.35 time-map floor spreads the arrivals; the magnitudes are unchanged."""
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        B = 1.0 + 11.0 * uu
        g = 0.003 * n ** -0.5
        for b in range(12):
            a = float(sm(B - b))
            if a <= 0:
                continue
            c = 1.0 + (b + 0.5) * (8.3 / max(B, 1.0))
            w = 0.5 / np.sqrt(B)
            g = g + a * np.exp(-((ln - c) ** 2) / (2 * w * w))
        m[i] = g * n ** -0.25
    return spec_sch(m, 3.0, floor=0.35)


def saw_avalanche():
    """A saw whose partials slide DOWN the spectrum and pile up: bin k -> 1 + (k - 1) 2^(-S), S 0 -> 7 octaves,
    energy summing where they land (fractional, so the pile-up glides) and compressing as it heaps. Frames: a saw
    that collapses into a fat, dark, buzzing cluster of the lowest eight partials."""
    amp = n ** -0.8
    amp[NMAX:] = 0.0
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        S = 7.0 * sm(uu / 0.9)
        g = scatter(1.0 + (n - 1.0) * 2.0 ** (-S), amp)
        m[i] = g ** (1.0 - 0.4 * uu)
    return spec(m)


def polyrhythm_partials():
    """WILD FROM FRAME 0. Harmonics grouped by their smallest prime factor (2, 3, 5, 7, 11+), each group pulsing at
    its own rate across the table — 2, 3, 5, 7 and 11 swells — already strobing at frame 0 and sharpening to hard
    gates. Scan it with an LFO and the timbre plays a five-against-seven groove; the tilt opens as it goes."""
    spf = np.zeros(N, int)
    for p in primes_to(N)[::-1]:
        spf[p - 1::p] = p
    grp = np.select([spf == 2, spf == 3, spf == 5, spf == 7], [0, 1, 2, 3], default=4)
    rates = np.array([2.0, 3.0, 5.0, 7.0, 11.0])
    phs = np.array([0.0, 0.3, 0.55, 0.15, 0.8])
    m = np.zeros((F, N))
    for i, uu in enumerate(u):
        q = 3.0 + 9.0 * uu ** 1.4
        pul = (0.5 + 0.5 * np.cos(TWO_PI * (rates * uu + phs))) ** q
        g = n ** -(0.95 - 0.55 * uu) * (0.01 + pul[grp])
        g[0] = 1.0
        m[i] = g
    return spec_sin(m)


def cycle_strum():
    """A chord strummed INSIDE every cycle: six sine-bodied plucks (a little 2nd and 3rd harmonic each) of a
    minor-ninth voicing enter one after another, each decaying before the next cycle begins. Frames: strum spread
    0.35 -> 0.95 cycle, pluck decay 0.5 -> 0.05, chord root 2 -> 40 partials — a slow harp-roll that ends as a
    rattling strum of bright pings."""
    Ns = SIZE * 4
    tt = np.arange(Ns) / Ns
    ratios = np.array([1.0, 1.5, 2.4, 3.0, 3.6, 4.5])
    fade = np.minimum(1.0, (1.0 - tt) / 0.04) * np.minimum(1.0, tt / 0.004)
    y = np.zeros((F, Ns))
    for i, uu in enumerate(u):
        R = 2.0 * 20.0 ** uu
        spread = 0.35 + 0.6 * uu
        d = 0.5 * 0.1 ** uu
        g = np.zeros(Ns)
        for j, r in enumerate(ratios):
            tau = tt - spread * j / len(ratios)
            env = np.where(tau >= 0, np.minimum(1.0, tau / 0.002) * np.exp(-np.maximum(tau, 0.0) / d), 0.0)
            x = TWO_PI * R * r * tt + 0.7 * j
            g += env * (np.sin(x) + 0.35 * np.sin(2 * x) + 0.15 * np.sin(3 * x)) / (1.0 + 0.2 * j)
        y[i] = g * fade
    return tband(y)


def sch_phase(m, wraps=1.0, rot=0.0, floor=0.0):
    """ONE fixed phase set for a whole table: Schroeder's power-weighted chirp law computed from the table's
    average (per-frame power-normalised) spectrum. Harmonic k arrives when the cumulative power reaches it, so
    the energy spreads evenly through the cycle: a low crest factor (louder at equal peak) and a visible chirp
    shape. Fixed across frames, so the morph never smears.
    floor (fb638 level fix, 0 = the original law): the fraction of the time map given evenly to every harmonic the
    table ever uses. Where the AVERAGE power is near zero the cumulative power is flat, so every partial there
    arrives at the same instant: a frame whose energy passes through such a region (a bump entering from the
    top, a lone air bed) collapses into one needle and plays 5-8 dB quiet at peak 1. The floor spreads those
    arrivals; the magnitudes and the fingerprint are untouched."""
    m = np.asarray(m, float)
    pw_ = m ** 2
    pw_ = pw_ / np.maximum(pw_.sum(axis=1, keepdims=True), 1e-30)
    p = pw_.mean(axis=0)
    p = p / max(p.sum(), 1e-30)
    if floor > 0.0:
        act = (m.max(axis=0) > 0).astype(float)
        p = (1.0 - floor) * p + floor * act / max(act.sum(), 1.0)
    C = np.concatenate(([0.0], np.cumsum(p)[:-1]))
    D = np.concatenate(([0.0], np.cumsum(n * p)[:-1]))
    S = n * C - D
    return -TWO_PI * wraps * S + TWO_PI * rot * n


def spec_sch(m, wraps=1.0, rot=0.0, floor=0.0):
    m = np.maximum(np.nan_to_num(np.asarray(m, float)), 0.0)
    m[:, NMAX:] = 0.0
    return spec(m, sch_phase(m, wraps, rot, floor))


def spec_sin(m, q=0.35):
    """Sine-phase render, rotated half a cycle (so a saw's jump sits mid-cycle, not on the seam), plus a gentle
    quadratic dispersion q that smears each edge into a short chirp: sparse lattices draw as sharp, readable
    shapes instead of noise, without the needle-spike crest of pure sine phase. Fixed across frames."""
    return spec(m, -0.5 * np.pi + np.pi * n + np.pi * q * n * n / NMAX)


def harmonic_rake():
    """A 200-partial saw raked apart in TIME: every harmonic k is windowed into a grain placed at its own
    golden-ratio spot (k * 0.618 mod 1) in the cycle. Frames: grain width 1/2 cycle -> 1/60 cycle, so a smeared
    saw shatters into a scatter of pitched grains, each harmonic sparking at its own moment."""
    Ns = SIZE * 2
    tt = np.arange(Ns) / Ns
    kk = np.arange(1, 201, dtype=float)
    pos = (kk * 0.6180339887) % 1.0
    y = np.zeros((F, Ns))
    for i, uu in enumerate(u):
        L = 0.5 * (1.0 / 30.0) ** uu
        g = np.zeros(Ns)
        for k, p in zip(kk, pos):
            d = wrapd(tt - p)
            w = np.where(np.abs(d) < 0.5 * L, 0.5 + 0.5 * np.cos(TWO_PI * d / L), 0.0)
            g += w * np.sin(TWO_PI * k * tt) / k ** (0.9 - 0.6 * uu)
        y[i] = g
    return tband(np.roll(y, Ns // 2, axis=1))


def prime_stairs():
    """A staircase that climbs the PRIMES inside every cycle: the cycle is cut into S steps and step j sounds the
    j-th prime (times a rising scale) as its instantaneous frequency, phase-continuous with a short glide, on a sine
    carrier with a little 2nd and 3rd harmonic. Frames: steps 3 -> 40 (2, 3, 5 ... 173), scale 1 -> 4 — a warbling
    sine that ends as a fast, irregular stair-chirp up to partial ~690, never landing on a musical interval twice."""
    Ns = SIZE * 4
    tt = np.arange(Ns) / Ns
    P = PRIMES[:40].astype(float)
    y = np.zeros((F, Ns))
    for i, uu in enumerate(u):
        S = int(np.round(3 + 37 * uu ** 0.9))
        sc = 4.0 ** uu
        notes = P[:S] * sc
        pos = tt * S
        j = np.floor(pos).astype(int) % S
        fr = pos - np.floor(pos)
        mix = sm(fr / 0.2)
        lf = np.log(notes[(j - 1) % S]) * (1.0 - mix) + np.log(notes[j]) * mix
        ph, dt = phase_from_freq(np.exp(lf))
        x = TWO_PI * ph
        y[i] = np.sin(x) + 0.3 * np.sin(2 * x) + 0.12 * np.sin(3 * x)
    return tband(y)


TABLES = [
    ("GRAIN CONSTELLATION", grain_constellation),
    ("SQUARE CROWN", square_crown),
    ("GRAIN FUNNEL", grain_funnel),
    ("SUB SPARK", sub_spark),
    ("SPARK RAIN", spark_rain),
    ("BURST LADDER", burst_ladder),
    ("CREST RINGER", crest_ringer),
    ("SINE CHIRP FADE", sine_chirp_fade),
    ("SQUARE SIREN", square_siren),
    ("CHIRP BRAID", chirp_braid),
    ("ARPEGGIO INSIDE", arpeggio_inside),
    ("PULSE SCALE RUN", pulse_scale_run),
    ("CHIRP HORIZON", chirp_horizon),
    ("WAVELET CHIRP", wavelet_chirp),
    ("CARPET TIDE", carpet_tide),
    ("PARTIAL DUNES", partial_dunes),
    ("PARTIAL MIGRATION", partial_migration),
    ("MOTH-EATEN SAW", moth_eaten_saw),
    ("BROWNIAN PARTIALS", brownian_partials),
    ("HARMONIC RAKE", harmonic_rake),
    ("CHORD CATHEDRAL", chord_cathedral),
    ("SAW CLUSTER", saw_cluster),
    ("INVERSION ELEVATOR", inversion_elevator),
    ("SUB CHORD HALO", sub_chord_halo),
    ("PENTATONIC PARTIALS", pentatonic_partials),
    ("WHOLE TONE LADDER", whole_tone_ladder),
    ("TRITAVE PARTIALS", tritave_partials),
    ("PITCH GRID SAW", pitch_grid_saw),
    ("CANYON SUB", canyon_sub),
    ("BARBER POLE COMB", barber_pole_comb),
    ("DIVISOR STORM", divisor_storm),
    ("COLLATZ TRAIL", collatz_trail),
    ("PRIME FACTOR SCAN", prime_factor_scan),
    ("GOLDEN RATIO GATE", beatty_gate),
    ("DIGIT SUM WALK", digit_sum_walk),
    ("SEESAW PIVOT", seesaw_pivot),
    ("SCISSOR TILT", scissor_tilt),
    ("SNAP GRID", snap_grid),
    ("MIRROR SHIFT", mirror_shift),
    ("SHEPARD SPIRAL", shepard_spiral),
    ("UNDERTONE RAIN", undertone_rain),
    ("INTERVAL SHADOW", interval_shadow),
    ("PARTIAL SEQUENCER", subset_counter),
    ("PARTIAL CROSSFIRE", partial_crossfire),
    ("SHUFFLED SAW", shuffled_saw),
    ("SUB UNDER STATIC", sub_under_static),
    ("FORMANT PICKET", formant_picket),
    ("SAW AVALANCHE", saw_avalanche),
    ("POLYRHYTHM PARTIALS", polyrhythm_partials),
    ("CYCLE STRUM", cycle_strum),
    ("PRIME STAIRS", prime_stairs),
]
CATEGORY = {ident: "Spectral" for ident, _ in TABLES}
