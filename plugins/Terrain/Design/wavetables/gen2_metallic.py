"""
gen2_metallic.py — Terrain factory bank, THE 500: new METALLIC candidates (quota 18).

Inharmonic metal, built from first principles and nothing else: modal ratio sets (free bar, cantilever
tine, clamped/spinning disc, bowl rim, plate, bell) PROJECTED onto the 2048-sample harmonic grid through a
virtual fundamental; stiff-string stretch; frequency shifting and ring modulation; clangorous FM; square-wave
cymbal hash and XOR logic; one-sided collisions (a fork buzzing on a plate, a ball bearing bouncing); spectral
self-convolution. Every table is ONE process whose amount is the frame axis — frame 0 plays, the last frame is
wild. A "virtual fundamental" v means the metal's first mode sits on harmonic v: the table sounds log2(v)
octaves above the note, exactly like a real bell sounds above its hum.

Deterministic (seeded RNG only), numpy only, each table renders in well under 5 s.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, wtlib

F, SZ, NH = wtlib.FRAMES, wtlib.SIZE, wtlib.NH
U   = np.linspace(0.0, 1.0, F)
UU  = U[:, None]
K   = wtlib.n_ax.astype(float)          # 1..1024
KK  = K[None, :]
TOP = 1000                              # keep harmonics <= TOP (a gentle guard below Nyquist)


# ── shape kit ─────────────────────────────────────────────────────────────────────────────
def sm(x):
    x = np.clip(np.asarray(x, float), 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)

def ramp(u, a, b):
    """0 before a, smooth 0->1 between a and b, 1 after."""
    return sm((np.asarray(u, float) - a) / (b - a))

def geo(a, b, x):
    return a * (b / a) ** np.asarray(x, float)

def fold(x, top=TOP):
    """Mirror partial positions at the top of the grid (and at 1) — the 'aliasing' reflection."""
    y = np.mod(np.asarray(x, float) - 1.0, 2.0 * (top - 1.0))
    return 1.0 + np.where(y > top - 1.0, 2.0 * (top - 1.0) - y, y)

def blank(cplx=False):
    return np.zeros((F, NH), complex if cplx else float)

def put(m, i, x, amp, ph=None):
    """Add partials at (fractional) bin positions x: a fractional position is split over the two
    neighbouring harmonics (in-cycle beating = the clang survives the grid). With ph the matrix is
    complex and each partial carries its own phase."""
    x = np.atleast_1d(np.asarray(x, float))
    if x.size == 0:
        return
    amp = np.broadcast_to(np.asarray(amp, float), x.shape)
    lo = np.floor(x).astype(np.intp)
    fr = x - lo
    if ph is not None:
        amp = amp * np.exp(1j * np.broadcast_to(np.asarray(ph, float), x.shape))
    for idx, w in ((lo, 1.0 - fr), (lo + 1, fr)):
        ok = (idx >= 1) & (idx <= TOP)
        if ok.any():
            np.add.at(m[i], idx[ok] - 1, (amp * w)[ok])

def reson(m, i, x, amp, bw):
    """Add partials as Lorentzian resonances (finite Q): skirts of width bw bins fill the neighbours. Each
    partial is normalised on its two nearest harmonics, so a high-Q mode gliding between grid points keeps
    its level (a bare Lorentzian narrower than a bin would flicker by tens of dB)."""
    x = np.atleast_1d(np.asarray(x, float))
    amp = np.broadcast_to(np.asarray(amp, float), x.shape)
    bw = np.broadcast_to(np.asarray(bw, float), x.shape)
    ok = (x >= 0.5) & (x <= TOP + 0.5)
    x, amp, bw = x[ok], amp[ok], bw[ok]
    fr = x - np.floor(x)
    amp = amp / (1.0 / (1.0 + (fr / bw) ** 2) + 1.0 / (1.0 + ((1.0 - fr) / bw) ** 2))
    for s in range(0, x.size, 256):
        d = (K[None, :] - x[s:s + 256, None]) / bw[s:s + 256, None]
        m[i] += (amp[s:s + 256, None] / (1.0 + d * d)).sum(axis=0)

def chirp_phase(c=1.0, seed=None):
    """A per-harmonic phase law phi_k = -pi c k^2 / NH: each partial is placed at its own moment in the
    cycle (low partials early, high late) — low crest, and the frame SHOWS its partial structure as a sweep."""
    ph = -np.pi * c * K * K / NH
    if seed is not None:
        ph = ph + 0.35 * np.random.default_rng(seed).standard_normal(NH)
    return ph

def done(m, phase=None):
    """magnitude matrix (or complex matrix) -> finalized frames."""
    m = np.array(m)
    m[:, TOP:] = 0
    if np.iscomplexobj(m):
        spec = np.zeros((F, SZ // 2 + 1), complex)
        spec[:, 1:NH + 1] = m
        return wtlib.finalize(np.fft.irfft(spec, n=SZ, axis=1))
    return wtlib.finalize(wtlib.cycles_from_mags(np.abs(m), phase))

# ── time-domain kit: build at OS x, keep harmonics <= TOP, back to 2048 ────────────────────
def tgrid(os_=8):
    return np.arange(SZ * os_) / (SZ * os_)

def tdone(Y, roll=0, top=TOP):
    Y = np.asarray(Y, float)
    S = np.fft.rfft(Y, axis=1)
    spec = np.zeros((Y.shape[0], SZ // 2 + 1), complex)
    spec[:, 1:top + 1] = S[:, 1:top + 1]
    y = np.fft.irfft(spec, n=SZ, axis=1)
    if roll:
        y = np.roll(y, roll, axis=1)
    return wtlib.finalize(y)

def bl_saw(tt, nmax=TOP, shift=0.0):
    """Band-limited rising saw on grid tt (one cycle), jump at t = shift."""
    N = tt.size
    spec = np.zeros(N // 2 + 1, complex)
    k = np.arange(1, nmax + 1)
    spec[1:nmax + 1] = (-1j) * (1.0 / k) * np.exp(-2j * np.pi * k * shift) * (N / 2) * (-2 / np.pi) * -1
    return np.fft.irfft(spec, n=N)

def circ_conv(a, b):
    return np.fft.irfft(np.fft.rfft(a) * np.fft.rfft(b), n=a.size)


# ══════════════════════════════════════════════════════════════════════════════════════════
#  THE TABLES
# ══════════════════════════════════════════════════════════════════════════════════════════

def shrapnel_bell():
    """SHRAPNEL BELL — a minor-third church bell (hum 0.5, prime 1, tierce 1.183, quint 1.506, nominal 2 ...
    plus a 1.25-power upper series) on virtual fundamental 8. Frame axis: the bell SHATTERS — each partial
    sheds up to 24 seeded shards that are born at random moments and fly apart (scatter grows to 70 % of the
    partial's frequency, shards past the top mirror back down) while the tilt brightens. Clean bell ->
    cracked bell -> a spray of metal dust."""
    rng = np.random.default_rng(0x5A1)
    low = np.array([0.5, 1.0, 1.183, 1.506, 2.0, 2.514, 2.662, 3.011, 3.46, 4.17, 5.43, 6.80])
    j = np.arange(13, 73)
    r = np.concatenate([low, 6.80 * (j / 12.0) ** 1.25])
    P, NF, v = r.size, 24, 8.0
    x0 = v * r
    a0 = x0 ** -1.05
    a0[:12] *= 2.2
    off = rng.standard_normal((P, NF))
    off[:, 0] = 0.0
    birth = rng.uniform(0.0, 1.0, (P, NF)) ** 0.75
    share = rng.uniform(0.25, 1.0, (P, NF))
    m = blank()
    for i, u in enumerate(U):
        spread = 0.7 * sm(u) ** 1.5
        alive = sm((u - birth) / 0.12)
        w = share * alive
        w[:, 0] = 1.0 - 0.8 * sm(u)
        w = w / np.sqrt((w ** 2).sum(axis=1, keepdims=True) + 1e-12)
        x = fold(x0[:, None] * np.exp(spread * off))
        amp = (a0 * (x0 / v) ** (0.75 * sm(u)))[:, None] * w
        put(m, i, x.ravel(), amp.ravel())
    return done(m)


def piano_wire_snap():
    """PIANO WIRE SNAP — a 1000-partial saw given stiff-string inharmonicity x_k = k*sqrt(1 + B k^2), every
    partial keeping its saw phase. Frame axis: B climbs 1e-8 -> 0.03 (6.5 decades): saw -> piano wire ->
    the stretched upper partials hit the top of the grid and MIRROR back down, folding over and over until
    only the lowest seven stay in tune and the rest is a quadratic-residue spray. Frame shapes: a saw whose
    edge sprouts a dispersive 'boing' chirp that swallows the cycle. The hammer's strike point also moves
    from 1/9 to 1/2.15 of the wire (its comb of missing partials walks down the series) and the hammer
    gets harder: a felt low-pass at harmonic 5 opens to 650. Dark struck wire -> bright stretched -> snapped."""
    k = np.arange(1, TOP + 1, dtype=float)
    B = geo(1e-7, 0.05, U ** 0.8)
    m = blank(True)
    for i, u in enumerate(U):
        beta = geo(1.0 / 9.0, 1.0 / 2.15, sm(u))
        kc = geo(5.0, 650.0, u ** 0.8)
        a = k ** -(1.0 - 0.4 * sm(u)) / (1.0 + (k / kc) ** 2) * np.exp(-(k / 960.0) ** 8) * (np.abs(np.sin(np.pi * k * beta)) + 0.02)
        x = fold(k * np.sqrt(1.0 + B[i] * k * k))
        put(m, i, x, a, ph=np.full(k.size, -np.pi / 2))
    return np.roll(done(m), SZ // 2, axis=1)


def ring_mod_saw_ladder():
    """RING MOD SAW LADDER — TIME. A band-limited saw (edge moved to mid-cycle) ring-modulated by a sine whose
    ratio glides 1 -> 46 (non-integer ratios live inside a Tukey window that closes at the cycle edge, so the
    frame still loops). Frame axis: modulator ratio up, ring-mod depth 35 % -> 100 %, a second modulator at
    ratio*sqrt(2) joins, and the modulator hardens from sine toward square at the end. Plain-ish saw ->
    bell-saw -> a pair of beating shards."""
    tt = tgrid(8)
    saw = bl_saw(tt, shift=0.5)
    saw /= np.abs(saw).max()
    tc = tt - 0.5
    win = np.where(np.abs(tc) < 0.38, 1.0, 0.5 + 0.5 * np.cos(np.pi * (np.abs(tc) - 0.38) / 0.12))
    Y = np.zeros((F, tt.size))
    mr = geo(1.0, 46.0, U ** 1.1)
    for i, u in enumerate(U):
        g = 1.0 + 9.0 * ramp(u, 0.7, 1.0)
        mod = np.tanh(g * np.sin(2 * np.pi * mr[i] * tc)) / np.tanh(g)
        mod2 = np.sin(2 * np.pi * mr[i] * 1.41421 * tc + 0.7)
        d = 0.6 + 0.4 * ramp(u, 0.0, 0.3)
        rm = saw * win * (mod + 0.55 * ramp(u, 0.35, 0.7) * mod2)
        Y[i] = (1.0 - d) * saw + d * rm
    return tdone(Y)


def shifted_saw_clang():
    """SHIFTED SAW CLANG — a saw on virtual fundamental 4 run through a frequency shifter: every partial
    moves by the SAME number of harmonics (4k + D), so the ratios stop being integer — the classic
    shifter clang — while each partial keeps its saw phase (the teeth roll instead of smearing). Frame
    axis: D glides 0 -> 13.3 harmonics, then the mirror sideband |4k - D| fades in (single sideband ->
    ring modulation) and the teeth tip into a barber-pole of beating."""
    k = np.arange(1, 250, dtype=float)
    D = 13.3 * U ** 1.25
    ph = -np.pi / 2 + np.pi * (k / 12.0) ** 2
    m = blank(True)
    for i, u in enumerate(U):
        a = k ** -(1.05 - 0.5 * sm(u))
        put(m, i, 4.0 * k + D[i], a, ph=ph)
        lsb = 0.85 * ramp(u, 0.45, 0.95)
        if lsb > 0:
            put(m, i, np.abs(4.0 * k - D[i]) + 0.0, a * lsb, ph=-ph)
    return np.roll(done(m), SZ // 2, axis=1)


def cowbell_to_cymbal():
    """COWBELL TO CYMBAL — TIME, the analog drum-machine trick on the grid: square oscillators at unrelated
    integer rates (27 + 40 = the cowbell pair, ratio 1.48; then 11, 15, 19, 26 join for the cymbal),
    summed, then band-passed. Frame axis: two squares -> six -> the mix is multiplied by the sign-product of
    all six (the XOR ring-mod hash) while the band-pass climbs from harmonic 70 to 700 and opens. Cowbell,
    then a clangy six-square chord, then the hiss of a hat."""
    tt = tgrid(16)
    rng = np.random.default_rng(808)
    fr = [27, 40, 11, 15, 19, 26]
    ph = rng.uniform(0, 1, len(fr))
    sq = [np.where(np.mod(f * tt + p, 1.0) < 0.5, 1.0, -1.0) for f, p in zip(fr, ph)]
    xor = np.prod(sq, axis=0)
    Y = np.zeros((F, tt.size))
    for i, u in enumerate(U):
        g = [1.0, 0.9] + [ramp(u, 0.12 + 0.08 * j, 0.3 + 0.08 * j) * 0.8 for j in range(4)]
        mix = sum(gi * s for gi, s in zip(g, sq))
        x = ramp(u, 0.55, 0.95)
        Y[i] = (1.0 - x) * mix + x * mix * xor
    S = np.fft.rfft(Y, axis=1)[:, :NH + 1]
    c = geo(70.0, 700.0, sm(U))
    wdt = 0.55 + 0.6 * U
    kk = np.arange(NH + 1, dtype=float); kk[0] = 1.0
    bp = np.exp(-0.5 * (np.log(kk[None, :] / c[:, None]) / wdt[:, None]) ** 2) + 0.02
    m = np.abs(S[:, 1:]) * bp[:, 1:]
    return done(m)


def logic_gate_hi_hat():
    """LOGIC GATE HI HAT — TIME, wild from frame 0: three pulse oscillators at unrelated integer rates are
    combined bitwise: XOR of two -> XOR of three -> XOR gated by AND; their rates climb stepwise
    (13/17/23 -> 67/89/113) and the duty narrows 50 % -> 12 %, so the hash thins into ticking needles. The three rates are kept coprime, so the pattern always
    spans the whole cycle.
    A metal hat built from logic, the shapes are pure 'what is this' block patterns."""
    tt = tgrid(16)
    rng = np.random.default_rng(0xB17)
    ph = rng.uniform(0, 1, 4)
    f0 = np.array([13, 17, 23, 5]); f1 = np.array([67, 89, 113, 9])
    Y = np.zeros((F, tt.size))
    for i, u in enumerate(U):
        f = np.round(geo(f0, f1, sm(u))).astype(int)
        while np.gcd.reduce(f[:3]) > 1:          # a shared factor would shorten the pattern's period
            f[2] += 1
        duty = 0.5 - 0.38 * sm(u)
        b = [np.mod(f[j] * tt + ph[j], 1.0) < duty for j in range(3)]
        gate = np.mod(f[3] * tt + ph[3], 1.0) < 0.6
        x2 = b[0] ^ b[1]
        x3 = x2 ^ b[2]
        xg = x3 & gate
        w2, w3, wg = 1.0 - ramp(u, 0.0, 0.4), ramp(u, 0.0, 0.4) * (1 - ramp(u, 0.6, 1.0)), ramp(u, 0.6, 1.0)
        Y[i] = w2 * (2.0 * x2 - 1) + w3 * (2.0 * x3 - 1) + wg * (2.0 * xg - 1)
    return tdone(Y)


def tuning_fork_buzz():
    """TUNING FORK BUZZ — TIME, subby. A fork (fundamental + its clang mode at ratio 6.27, split over
    harmonics 6/7, + mode 17.55 split over 17/18) pressed against a steel plate: the displacement hits a one-sided wall that
    bounces it back, and every contact rings the plate (modes 29, 47, 71, 103, 139, 188, circular
    convolution, so the ring wraps seamlessly). Frame axis: the wall moves in (threshold 0.9 -> 0.14, grazing at frame 0),
    restitution grows, the plate ring rises from -18 dB to +2 dB against the fork, a second wall
    appears underneath. Pure fork -> rattle -> buzzsaw."""
    tt = tgrid(8)
    N = tt.size
    fork = (np.sin(2 * np.pi * tt) + 0.28 * (0.73 * np.sin(2 * np.pi * 6 * tt + 0.3) + 0.27 * np.sin(2 * np.pi * 7 * tt + 0.3))
            + 0.12 * (0.45 * np.sin(2 * np.pi * 17 * tt + 1.1) + 0.55 * np.sin(2 * np.pi * 18 * tt + 1.1)))
    modes = np.array([29, 47, 71, 103, 139, 188, 251, 317, 402, 509, 613, 740])
    Y = np.zeros((F, N))
    for i, u in enumerate(U):
        c = geo(0.9, 0.14, sm(u))
        c2 = geo(1.6, 0.4, ramp(u, 0.45, 1.0))
        r = 0.2 + 0.7 * u
        pen = np.maximum(fork - c, 0.0)
        pen2 = np.maximum(-fork - c2, 0.0)
        body = fork - (1.0 + r) * pen + (1.0 + r) * pen2
        lam = 0.02 + 0.10 * u
        ir = sum(np.exp(-tt / lam) * np.sin(2 * np.pi * f * tt) * f ** -0.15 for f in modes)
        ir /= (1.0 - np.exp(-1.0 / lam))
        ring = circ_conv(pen + pen2, ir)
        ring = ring / (np.sqrt((ring ** 2).mean()) + 1e-12)
        Y[i] = body + np.sqrt((body ** 2).mean()) * (0.12 + 1.2 * u ** 1.3) * ring
    return tdone(Y)


def singing_bowl_scream():
    """SINGING BOWL SCREAM — rim modes of a bowl, f_n ~ n(n^2-1)/sqrt(n^2+1) (n = 2..24: 1, 2.83, 5.42, 8.77,
    12.9 ...) on virtual fundamental 10, each mode a doublet (the bowl is never perfectly round). Frame
    axis: rubbing harder — the doublets split wider (slow beat -> roughness), the Q drops, and stick-slip
    drives mode 1 and then mode 2 as full harmonic series on top of the inharmonic rim. A glassy ring
    that ends screaming."""
    n = np.arange(2, 25, dtype=float)
    rr = n * (n * n - 1) / np.sqrt(n * n + 1)
    rr /= rr[0]
    v = 10.0
    x = v * rr
    a = rr ** -0.85
    m = blank()
    j = np.arange(1, 101, dtype=float)
    for i, u in enumerate(U):
        dl = (0.25 + 3.2 * u ** 1.4) * np.sqrt(n - 1)
        bw = 0.08 + 1.2 * u ** 2
        reson(m, i, x - dl / 2, a * 0.6, bw)
        reson(m, i, x + dl / 2, a * 0.6, bw)
        b = ramp(u, 0.2, 0.8)
        if b > 0:
            put(m, i, v * j, b * 0.9 * j ** -(1.5 - 1.1 * u))
            put(m, i, x[1] * j[:35], b * b * 0.55 * j[:35] ** -(1.4 - 0.6 * u))
    return done(m, chirp_phase(1.0, seed=100))


def steelpan_circle():
    """STEELPAN CIRCLE — a tenor pan's notes on virtual fundamental 6: each note tuned fundamental/octave/
    twelfth (1, 2, 3) plus two soft inharmonic dents (4.2, 5.1). Frame axis: walking the pan's circle of
    fifths — the struck note, then its neighbours ring sympathetically one by one (G, F, D, Bb, A, Eb ...,
    then the octave ring), the pan drifts out of tune (seeded detune up to 5 %) and the hammer overdrives
    every note into a stretched brass series. One note -> the whole pan singing -> a battered drum."""
    rng = np.random.default_rng(0x9A7)
    order = [0, 7, 5, 2, 10, 9, 3, 4, 8, 11, 6, 1]
    notes = [(s, 1.0) for s in order] + [(s + 12, 0.7) for s in order[:8]]
    v = 6.0
    base = np.array([1.0, 2.0, 3.0, 4.2, 5.1])
    ba = np.array([1.0, 0.55, 0.32, 0.10, 0.07])
    det = rng.standard_normal((len(notes), 5))
    m = blank()
    for i, u in enumerate(U):
        dt = 0.05 * u ** 2
        hv = ramp(u, 0.4, 1.0)
        v = 6.0 * 2 ** (7 * sm(u) / 12.0)
        for q, (s, lv) in enumerate(notes):
            on = 1.0 if q == 0 else ramp(u, 0.03 + 0.032 * q, 0.1 + 0.032 * q) * 0.8 ** (q ** 0.6)
            if on <= 0:
                continue
            f0 = v * 2 ** (s / 12.0)
            x = f0 * base * (1.0 + dt * det[q])
            put(m, i, x, on * lv * ba ** (1.6 - 0.9 * u))
            if hv > 0:
                jj = np.arange(4, 60, dtype=float)
                put(m, i, fold(f0 * jj * (1 + 0.006 * u * jj)), on * lv * hv * 0.55 * jj ** -(1.2 - 0.5 * hv))
    return done(m)


def brake_squeal():
    """BRAKE SQUEAL — TIME FM, glassy-thin at frame 0: a whistle at harmonic 24 whose frequency swings once per
    cycle (FM at ratio 1, so the frame loops) plus a stick-slip judder that morphs the modulator from sine
    to a saw-like ramp. Frame axis: the squeal climbs 24 -> 78, the swing grows (index 1.5 -> 90) and the
    judder rate rises, so a thin whistle turns into a screaming, tearing chirp that fills the cycle."""
    tt = tgrid(8)
    Y = np.zeros((F, tt.size))
    for i, u in enumerate(U):
        c = np.round(geo(24.0, 78.0, u))
        beta = geo(1.5, 90.0, u)
        sh = ramp(u, 0.3, 1.0)
        mod = (1 - sh) * np.sin(2 * np.pi * tt) + sh * 0.9 * (np.sin(2 * np.pi * tt) + 0.5 * np.sin(4 * np.pi * tt) + 0.33 * np.sin(6 * np.pi * tt))
        jud = 0.25 * u * np.sin(2 * np.pi * np.round(2 + 9 * u) * tt)
        Y[i] = np.sin(2 * np.pi * c * tt + beta * mod + 4 * u * jud) * (1.0 + jud) + 0.25 * (1 - u) * np.sin(2 * np.pi * tt)
    return tdone(Y)


def sub_anvil():
    """SUB ANVIL — SUBBY. A sine sub (h1, h2 -13 dB) carrying the modes of a steel block
    (sqrt(i^2 + (j/0.62)^2 + (l/0.41)^2), first 40, first mode on harmonic 23). Frame axis: the anvil
    rises from -42 dB to -6 dB under the sub, its modes lose Q, the hammer ring-modulates them by the
    sub (sidebands +-1..6 harmonics = buzz), and at the end every pair of modes intermodulates (sum and
    difference tones). The fundamental stays the loudest partial the whole way."""
    rng = np.random.default_rng(0xA11)
    g = np.arange(0, 6)
    a3, b3, c3 = np.meshgrid(g, g, g, indexing='ij')
    r = np.sqrt(a3 ** 2 + (b3 / 0.62) ** 2 + (c3 / 0.41) ** 2).ravel()
    r = np.unique(np.round(r[r > 0], 4))[:40]
    r = r / r[0]
    x = 23.0 * r
    a = rng.uniform(0.4, 1.0, r.size) * r ** -0.5
    m = blank()
    jj = np.arange(1, 7)
    for i, u in enumerate(U):
        m[i, 0] += 1.0; m[i, 1] += 0.22; m[i, 2] += 0.05
        lev = 10 ** ((-42 + 36 * sm(u)) / 20)
        reson(m, i, x, lev * a, 0.05 + 0.6 * u)
        sb = ramp(u, 0.25, 0.8)
        if sb > 0:
            for q in jj:
                put(m, i, x + q, lev * a * sb * 0.62 ** q)
                put(m, i, x - q, lev * a * sb * 0.62 ** q)
        im = ramp(u, 0.55, 1.0)
        if im > 0:
            p, q = np.triu_indices(16, 1)
            w = lev * im * 0.45 * a[p] * a[q]
            put(m, i, fold(x[p] + x[q]), w)
            put(m, i, np.abs(x[p] - x[q]) + 1.0, w)
    return done(m)


def cantilever_tine():
    """CANTILEVER TINE — glassy. Clamped-free beam (a kalimba tine) mode ratios (beta_n/1.8751)^2 = 1, 6.27,
    17.55, 34.39, 56.84, 84.91 ... on a virtual fundamental that climbs 3 -> 11 (the tine is pushed
    shorter). Frame axis: shorter tine + the buzzer ring rattles — every mode grows a comb of sidebands at
    +- multiples of the tine's fundamental — and the modes lose Q. A pure glass ping that ends as a
    rattling buzz-kalimba."""
    bl = np.array([1.8751, 4.6941, 7.8548, 10.9955, 14.1372])
    bl = np.concatenate([bl, (2 * np.arange(6, 20) - 1) * np.pi / 2])
    r = (bl / bl[0]) ** 2
    m = blank()
    for i, u in enumerate(U):
        v = geo(3.0, 11.0, sm(u))
        x = v * r
        ok = x < TOP
        a = r[ok] ** -(0.9 - 0.45 * u) * (1.0 - sm((x[ok] - 850.0) / 150.0))
        reson(m, i, x[ok], a, 0.03 + 0.35 * u ** 2)
        bz = ramp(u, 0.15, 0.85)
        if bz > 0:
            j = np.arange(1, 31, dtype=float)
            for xm, am in zip(x[ok], a):
                w = bz * am * 0.5 * j ** -(1.2 - 0.6 * u)
                put(m, i, xm + v * j, w)
                put(m, i, np.abs(xm - v * j) + 0.5, w)
    return done(m, chirp_phase(0.7, seed=70))


def undercut_bar():
    """UNDERCUT BAR — the bar tuner's process as a morph: free-free bar ratios (1, 2.756, 5.404, 8.933 ...)
    on virtual fundamental 8, pulled toward the marimba tuning (1 : 4 : 10 ...) by carving the underside,
    then OVER-cut past it (the correction doubled) so the upper modes race up and mirror off the top.
    The mallet hardens along the way (tilt n^-2.2 -> n^-0.4) and the torsional modes wake up. A soft
    mallet -> a tuned marimba -> a ruined, shrieking bar."""
    free = np.array([1.0, 2.756, 5.404, 8.933, 13.345, 18.64, 24.81, 31.87, 39.81, 48.63, 58.3, 68.9])
    tuned = np.array([1.0, 4.0, 10.0, 16.8, 25.0, 34.6, 45.2, 57.0, 70.0, 84.3, 99.8, 116.5])
    tors = np.array([2.1, 4.35, 6.62, 8.9, 11.2, 13.5, 15.8])
    v = 8.0
    n = np.arange(1, free.size + 1, dtype=float)
    m = blank()
    for i, u in enumerate(U):
        c = 2.0 * u
        r = np.exp(np.log(free) + c * (np.log(tuned) - np.log(free)))
        a = n ** -(2.2 - 1.8 * u)
        bw = 0.06 + 1.1 * u ** 1.5
        reson(m, i, fold(v * r), a, bw)
        tw = ramp(u, 0.3, 1.0)
        if tw > 0:
            reson(m, i, fold(v * tors * (1 + 0.6 * u)), tw * 0.35 * np.arange(1, 8.0) ** -(1.0 - 0.6 * u), bw)
    return done(m, chirp_phase(1.3))


def cracked_carillon():
    """CRACKED CARILLON — five bells (bell partial set: hum, prime, minor tierce, quint, nominal + upper
    series) tuned to a minor-ninth chord (1, 6/5, 3/2, 9/5, 9/4) on virtual fundamental 5. Frame axis: one
    bell -> the other four join -> the bells CRACK: each partial splits into a widening pair, seeded
    partials drop out, the nominals rattle, and the tilt brightens. A carillon chord that ends broken."""
    rng = np.random.default_rng(0xCA7)
    low = np.array([0.5, 1.0, 1.2, 1.5, 2.0, 2.51, 2.67, 3.01, 4.16, 5.43, 6.8])
    j = np.arange(12, 80)
    br = np.concatenate([low, 6.8 * (j / 11.0) ** 1.28])
    chord = [1.0, 1.2, 1.5, 1.8, 2.25]
    v = 5.0
    drop = [rng.uniform(0, 1, br.size) for _ in chord]
    sgn = [rng.choice([-1.0, 1.0], br.size) * rng.uniform(0.4, 1.0, br.size) for _ in chord]
    m = blank()
    for i, u in enumerate(U):
        ck = ramp(u, 0.42, 1.0)
        tilt = 1.5 - 1.25 * sm(u)
        for b, cr in enumerate(chord):
            on = 1.0 if b == 0 else ramp(u, 0.05 + 0.08 * b, 0.15 + 0.08 * b) * 0.85
            if on <= 0:
                continue
            x = v * cr * br
            a = on * x ** -tilt
            a[:5] *= 2.0
            a = a * np.where(drop[b] < 0.55 * ck, 0.05, 1.0)
            sp = 1.0 + ck * 0.045 * sgn[b]
            put(m, i, fold(x * sp), a * 0.6)
            put(m, i, fold(x / sp), a * 0.6)
            if ck > 0:
                for q in (1, 2, 3):
                    put(m, i, x[4] + q * v * 0.5, a[4] * ck * 0.5 ** q)
                    put(m, i, x[4] - q * v * 0.5, a[4] * ck * 0.5 ** q)
    return done(m)


def gong_cascade():
    """GONG CASCADE — TIME, huge-heavy. A sparse gong chord (modes on harmonics 3 5 8 11 13 17 22 27 31 38 47 56 67 79 94,
    seeded phases) through a sine waveshaper whose drive rises 0.3 -> 20: at low drive it is the chord,
    then every mode intermodulates with every other (sum AND difference tones — the difference tones
    land low and heavy), until the cascade is a roaring wall. The nonlinear energy cascade of a
    tam-tam, frozen into frames."""
    tt = tgrid(8)
    rng = np.random.default_rng(0x6096)
    f = np.array([3, 5, 8, 11, 13, 17, 22, 27, 31, 38, 47, 56, 67, 79, 94])
    ph = rng.uniform(0, 2 * np.pi, f.size)
    a = f ** -0.55
    x = sum(ai * np.sin(2 * np.pi * fi * tt + p) for ai, fi, p in zip(a, f, ph))
    x /= np.abs(x).max()
    Y = np.zeros((F, tt.size))
    for i, u in enumerate(U):
        g = geo(0.3, 20.0, u)
        Y[i] = np.sin(g * x + 0.6 * ramp(u, 0.6, 1.0) * g * x * x)
    return tdone(Y)


def opera_gong_bend():
    """OPERA GONG BEND — the gong that glides: 80 shallow-shell modes (ratios m^0.85, seeded jitter) on
    virtual fundamental 4, the low modes bending further than the high ones (amplitude-dependent
    stiffness), so partials slide past each other. Frame axis: the pitch-bend whoops up, then the gong is
    driven into its nonlinear regime: subharmonics at x/2 and 3x/2 (period doubling) appear, then the
    modes break into a seeded chaotic carpet. Tuned gong -> whooping bend -> bifurcation."""
    rng = np.random.default_rng(0x0BE)
    mm = np.arange(1, 81, dtype=float)
    r = mm ** 0.85 * (1.0 + 0.03 * rng.standard_normal(mm.size))
    r[0] = 1.0
    v = 4.0
    a0 = r ** -0.75
    chaos_x = rng.uniform(1.0, 700.0, 500)
    chaos_a = rng.uniform(0.2, 1.0, 500) * chaos_x ** -0.5
    birth = rng.uniform(0.0, 1.0, 500)
    m = blank()
    for i, u in enumerate(U):
        kap = 0.38 * np.sin(np.pi * min(u / 0.6, 1.0)) ** 1.5 + 0.15 * ramp(u, 0.6, 1.0) * np.sin(2 * np.pi * 3 * (u - 0.6))
        x = v * r * (1.0 + kap * (1.0 - mm / 80.0) ** 2)
        reson(m, i, fold(x), a0 * (x / v) ** (0.55 * u), 0.12 + 0.2 * u)
        sh = ramp(u, 0.45, 0.8)
        if sh > 0:
            put(m, i, x / 2.0, sh * 0.7 * a0)
            put(m, i, fold(1.5 * x), sh * 0.5 * a0)
        ch = ramp(u, 0.72, 1.0)
        if ch > 0:
            put(m, i, chaos_x, chaos_a * chaos_x ** (0.35 * ch) * sm((ch - birth) / 0.2) * 0.6)
    return done(m)


def foundry_bell_fm():
    """FOUNDRY BELL FM — TIME, clangorous 3-operator FM: carrier ratio 2 <- modulator ratio 7 (the 3.5 bell
    ratio) <- modulator ratio 11. Frame axis: index 2 grows 0.8 -> 9, index 3 joins at a third of the way
    and grows to 6.5 while op 3's waveform thickens from sine toward a ramp (3 harmonics), so the bell
    cascades into a molten FM clang. Recognisable DX-type bell at frame 0."""
    tt = tgrid(8)
    Y = np.zeros((F, tt.size))
    for i, u in enumerate(U):
        I2 = geo(0.8, 9.0, u)
        I3 = 6.5 * ramp(u, 0.33, 1.0) ** 1.3
        sh = ramp(u, 0.6, 1.0)
        o3 = np.sin(2 * np.pi * 11 * tt) + sh * (0.5 * np.sin(2 * np.pi * 22 * tt) + 0.33 * np.sin(2 * np.pi * 33 * tt))
        o2 = np.sin(2 * np.pi * 7 * tt + I3 * o3)
        Y[i] = np.sin(2 * np.pi * 2 * tt + I2 * o2)
    return tdone(Y)


def thunder_sheet():
    """THUNDER SHEET — huge-heavy, wild from frame 0: a big thin steel sheet = a sub body (h1..h4) under a
    carpet of plate modes (constant modal density: seeded uniform positions, 150 -> 950 modes) with a
    rumble band that ROLLS down the sheet from harmonic 700 to 25 while the sheet is shaken harder
    (Q drops, tilt lifts). Thunder rolling off a sheet of steel."""
    rng = np.random.default_rng(0x7D5)
    NM = 950
    xm = rng.uniform(5.0, 990.0, NM)
    am = rng.uniform(0.15, 1.0, NM) * xm ** -0.5
    order = rng.permutation(NM)
    rank = np.empty(NM); rank[order] = np.arange(NM)
    m = blank()
    for i, u in enumerate(U):
        n_on = 150 + 800 * u
        on = np.clip(n_on - rank, 0.0, 1.0)
        c = geo(700.0, 25.0, sm(u))
        band = 1.0 + 4.0 * np.exp(-0.5 * (np.log(xm / c) / 0.5) ** 2)
        reson(m, i, xm, on * am * band * xm ** (0.25 * u), 0.2 + 0.5 * u)
        m[i, :4] += np.array([2.2, 1.1, 0.6, 0.35]) * (1.0 + 0.5 * u)
    return done(m)


def spinning_saw_blade():
    """SPINNING SAW BLADE — a circular blade clamped at its centre: nodal-diameter modes at (n/2)^2 on virtual
    fundamental 3 (n = 2..36). Frame axis: spin speed. A spinning disc splits every mode into a forward
    and a backward travelling wave, x_n +- n*Omega; as Omega rises the backward waves dive, hit zero at
    the CRITICAL SPEED and fold back (flutter), while the tooth-passing whine (24 teeth) climbs in over
    them. A ringing blade -> splitting -> the scream of a saw at speed."""
    n = np.arange(2, 37, dtype=float)
    v = 3.0
    x0 = v * (n / 2.0) ** 2
    a = n ** -0.7
    m = blank()
    for i, u in enumerate(U):
        Om = 2.3 * u ** 1.2
        reson(m, i, x0 + n * Om, a * 0.7, 0.1 + 0.1 * u)
        reson(m, i, np.abs(x0 - n * Om) + 0.8, a * 0.7, 0.1 + 0.1 * u)
        wh = ramp(u, 0.12, 0.8)
        if wh > 0 and Om > 0:
            j = np.arange(1, 60, dtype=float)
            put(m, i, 24.0 * Om * j, wh * 2.2 * Om * j ** -(1.1 - 0.75 * u))
    return done(m)


def folded_ring_clang():
    """FOLDED RING CLANG — TIME. Two ring-modulated pairs (11 x 29 and 4 x 47: partials 18, 40, 43, 51) over a
    faint fundamental — a sparse, high, clean clang — through a sine wavefolder whose gain rises 0.6 -> 11 with a creeping DC bias
    (asymmetric folds = even products). Frame shapes: a lumpy bell wave folding into ever tighter
    ripples; the spectrum fills from four partials to a wall."""
    tt = tgrid(8)
    x = np.sin(2 * np.pi * 11 * tt) * np.sin(2 * np.pi * 29 * tt) + 0.6 * np.sin(2 * np.pi * 4 * tt + 0.4) * np.sin(2 * np.pi * 47 * tt) + 0.3 * np.sin(2 * np.pi * tt)
    x /= np.abs(x).max()
    Y = np.zeros((F, tt.size))
    for i, u in enumerate(U):
        g = geo(0.6, 11.0, u)
        Y[i] = np.sin(0.5 * np.pi * g * (x + 0.35 * u))
    return tdone(Y)


def spring_coil_chirp():
    """SPRING COIL CHIRP — a spring tank as a feedback loop, solved in the frequency domain (exact periodic
    steady state): H_k = 1 / (1 - g e^{-i theta_k}), theta_k = 2 pi (k/5 + D sqrt(k)) — a delay of a fifth of
    a cycle plus the spring's dispersion — fed by a half-dispersed pulse through the tank's own band-pass
    (springs pass only a mid band; it climbs from harmonic 18 to 160). Frame axis: D grows 1.5 -> 20 and the
    loop gain 0.45 -> 0.9: a combed nasal band whose teeth start to CHIRP (dense at the bottom, sparse at
    the top) while the in-cycle 'pew' wraps round and round."""
    k = K.copy()
    m = blank(True)
    for i, u in enumerate(U):
        D = 1.5 + 18.5 * u ** 1.4
        g = 0.45 + 0.45 * sm(u)
        th = 2 * np.pi * (k / 5.0 + D * np.sqrt(k))
        H = 1.0 / (1.0 - g * np.exp(-1j * th))
        tank = np.exp(-0.5 * (np.log(k / geo(18.0, 160.0, u)) / 0.75) ** 2) + 0.03
        src = k ** -0.5 * tank * np.exp(-(k / 980.0) ** 8) * np.exp(-1j * (np.pi / 2 + np.pi * D * np.sqrt(k)))
        m[i] = src * H
    return np.roll(done(m), SZ // 2, axis=1)


def ball_bearing_bounce():
    """BALL BEARING BOUNCE — TIME, in-cycle structure. A steel ball bouncing on a steel plate inside one cycle:
    impacts at geometrically shrinking intervals (restitution r), each ringing a circular plate's modes (1, 2.08, 3.41, 3.89, 5.0, 6.2 ... on harmonic 4) with a
    strength and brightness set by the impact velocity (soft late bounces only wake the low modes) plus the
    sharp steel-on-steel contact click; the
    rings are periodic steady-state decays, so the wrap is seamless. Frame axis: r rises 0.2 -> 0.94 (one
    ping per cycle -> a machine-gun accelerando converging to a buzz) while the plate shrinks (its modes
    climb x4) and rings longer."""
    tt = tgrid(8)
    base = 4.0 * np.array([1.0, 2.08, 3.41, 3.89, 5.0, 6.2, 7.13, 7.51, 8.66])
    mi = np.arange(base.size)
    mph = np.random.default_rng(0xBB).uniform(0, 2 * np.pi, base.size)
    Y = np.zeros((F, tt.size))
    for i, u in enumerate(U):
        r = 0.2 + 0.74 * sm(u)
        lam = 0.15 + 0.35 * u
        modes = np.round(base * geo(1.0, 4.0, u))
        T1 = 0.6 * (1.0 - r)
        y = np.zeros(tt.size)
        tcur, vv = 0.2, 1.0
        for q in range(80):
            tau = np.mod(tt - tcur, 1.0)
            env = np.exp(-tau / lam) / (1.0 - np.exp(-1.0 / lam))
            amps = vv * modes ** -0.3 * vv ** (0.6 * mi)
            y += env * sum(a_ * np.sin(2 * np.pi * f_ * tau + p_) for a_, f_, p_ in zip(amps, modes, mph))
            dd = np.mod(tt - tcur + 0.5, 1.0) - 0.5
            y += 0.7 * vv * np.exp(-0.5 * (dd / 0.0015) ** 2) * np.sign(dd) * (1.0 - np.exp(-np.abs(dd) / 0.0008))
            tcur += T1 * r ** q
            vv *= r ** 0.5
            if vv < 0.03 or tcur > 0.93:
                break
        Y[i] = y
    return tdone(Y)


def wind_chime_moire():
    """WIND CHIME MOIRE — tubular chimes (free-tube partials (2n+1)^2: 1, 2.78, 5.44, 9, 13.4, 18.8) at seeded
    pitches on virtual fundamental 9. Every partial is drawn as a pair of neighbouring harmonics whose
    balance rotates at its own rate across the frames (cos^2 / sin^2), so each chime beats INSIDE the cycle
    and the beats slide like moire. Frame axis: 1 chime -> 14 chimes (each newcomer higher), rotation rates speed up, and the
    chimes start to knock together (a bright click comb at the end)."""
    rng = np.random.default_rng(0xC417)
    pr = np.array([1.0, 2.78, 5.44, 9.0, 13.4, 18.8])
    NC = 14
    pitch = np.concatenate([[1.0], np.sort(2 ** (rng.uniform(-0.2, 2.2, NC - 1)))])
    rate = rng.uniform(0.5, 3.0, (NC, pr.size))
    v = 9.0
    m = blank()
    for i, u in enumerate(U):
        for c in range(NC):
            on = 1.0 if c == 0 else ramp(u, 0.04 * c, 0.04 * c + 0.12) * 0.8
            if on <= 0:
                continue
            x = np.floor(v * pitch[c] * pr)
            th = np.pi * u * rate[c] * (1.0 + 3.0 * u)
            a = on * pr ** -0.8
            put(m, i, x, a * np.cos(th) ** 2)
            put(m, i, x + 1, a * np.sin(th) ** 2)
        kn = ramp(u, 0.7, 1.0)
        if kn > 0:
            m[i] += kn * 0.25 * (K ** -0.3) * (0.5 + 0.5 * np.cos(2 * np.pi * K / 7.0)) * (K <= TOP)
    return done(m)


def crystal_splinter():
    """CRYSTAL SPLINTER — glassy-thin at frame 0: four lone partials (23, 37, 61, 97) like a struck crystal.
    Frame axis: the crystal goes nonlinear GENERATION by generation — first the pairwise sum and
    difference tones appear, then those mix with the originals, then a third generation — every product
    reflected at the top of the grid. Four needles -> a lattice -> a splintered spray (hundreds of partials)."""
    base = np.array([23.0, 37.0, 61.0, 97.0])
    ba = np.array([1.0, 0.8, 0.65, 0.5])
    def prods(x, a, y, b):
        X = np.concatenate([(x[:, None] + y[None, :]).ravel(), np.abs(x[:, None] - y[None, :]).ravel()])
        A = np.concatenate([(a[:, None] * b[None, :]).ravel()] * 2)
        ok = X >= 1
        return fold(X[ok]), A[ok]
    g1x, g1a = prods(base, ba, base, ba)
    g2x, g2a = prods(g1x, g1a, base, ba)
    g3x, g3a = prods(g2x, g2a, g1x, g1a)
    m = blank()
    for i, u in enumerate(U):
        put(m, i, base, ba)
        for gx, ga, (lo, hi), lv in ((g1x, g1a, (0.08, 0.4), 0.6), (g2x, g2a, (0.35, 0.72), 0.45), (g3x, g3a, (0.62, 1.0), 0.3)):
            w = ramp(u, lo, hi)
            if w > 0:
                if gx.size < 1000:
                    reson(m, i, gx, lv * w * ga * (1 + u * gx / 200.0), 0.04 + 0.9 * u ** 2)
                else:
                    put(m, i, gx, lv * w * ga * (1 + u * gx / 200.0))
    return done(m, chirp_phase(0.3))


def pelog_metallophone():
    """PELOG METALLOPHONE — gamelan bars (trapezoid-bar partials 1, 2.39, 4.4, 6.9) on the seven notes of a
    pelog scale (0, 120, 270, 540, 670, 785, 950 cents), virtual fundamental 6, three octaves. Every bar
    comes as a PAIR tuned slightly apart (ombak — the gamelan's deliberate beating). Frame axis: one pair ->
    the scale fills in -> the ombak detune widens from a slow shimmer to roughness and the bars' upper
    partials stretch. A single gong-bar -> a whole shimmering gamelan -> a seething cluster."""
    cents = np.array([0, 120, 270, 540, 670, 785, 950])
    notes = list(cents) + list(cents + 1200) + list(cents + 2400)
    pr = np.array([1.0, 2.39, 4.4, 6.9, 10.1])
    pa = np.array([1.0, 0.5, 0.3, 0.18, 0.1])
    v = 6.0
    m = blank()
    for i, u in enumerate(U):
        omb = geo(0.12, 3.5, u)
        st = 1.0 + 0.05 * u * np.arange(pr.size)
        for q, c in enumerate(notes):
            on = 1.0 if q == 0 else ramp(u, 0.03 + 0.026 * q, 0.1 + 0.026 * q) * 0.85
            if on <= 0:
                continue
            x = v * 2 ** (c / 1200.0) * pr * st
            a = on * pa ** (1.3 - 0.8 * u)
            reson(m, i, x - omb / 2, a, 0.05 + 0.5 * u)
            reson(m, i, x + omb / 2, a, 0.05 + 0.5 * u)
    return done(m)


def grinder_sparks():
    """GRINDER SPARKS — TIME, industrial. An angle-grinder disc's whine (a tooth-rate harmonic series) with a
    spray of sparks: seeded random clicks, each a tiny decaying burst of a high metal mode. Frame axis:
    the disc bites and bogs down (whine rate 36 -> 12 harmonics, its series brightens) while the spark
    count climbs 0 -> 260 and they get hotter. A clean whine -> a shower of sparks."""
    tt = tgrid(8)
    rng = np.random.default_rng(0x6A1)
    NS = 260
    pos = rng.uniform(0, 1, NS)
    fr = rng.uniform(140, 820, NS)
    amp = rng.uniform(0.3, 1.0, NS)
    Y = np.zeros((F, tt.size))
    N = tt.size
    for i, u in enumerate(U):
        fq = geo(36.0, 12.0, sm(u))
        f_lo = int(np.floor(fq)); w_hi = fq - f_lo
        spec = np.zeros(N // 2 + 1, complex)
        for ft, w in ((f_lo, 1.0 - w_hi), (f_lo + 1, w_hi)):
            J = np.arange(1, TOP // ft + 1)
            spec[ft * J] += w * J ** -(1.3 - 0.6 * u) * N / 2 * -1j
        y = np.roll(np.fft.irfft(spec, n=N), N // 2)
        y /= np.abs(y).max()
        nsf = NS * sm(u) ** 0.8
        ns = min(NS, int(np.ceil(nsf)))
        lam = 0.004 + 0.006 * u
        for q in range(ns):
            tau = np.mod(tt - pos[q], 1.0)
            sel = tau < 12 * lam
            y[sel] += min(1.0, nsf - q) * (0.35 + 0.45 * u) * amp[q] * np.exp(-tau[sel] / lam) * np.sin(2 * np.pi * fr[q] * tau[sel])
        Y[i] = y
    return tdone(Y)


def golden_plate():
    """SQUARE TO GOLDEN PLATE — a simply-supported rectangular plate, modes f_mn ~ m^2 + (n/alpha)^2 (m, n = 1..32)
    struck near a corner, first mode on harmonic 5. Frame axis: the plate's GEOMETRY stretches — aspect 1
    (a square: modes pile onto each other, few strong degenerate partials, almost tonal) -> the golden
    rectangle (maximally irregular) -> a long 3.4:1 strip (the modes fall into families = ladder-like
    rhythm) — while the strike point walks from near a corner to the dead centre, where every even mode
    has a node and drops out, and the hammer hardens (tilt x^-0.55 -> x^+0.45). The same hammer on three different sheets of steel."""
    mm, nn = np.meshgrid(np.arange(1, 33.0), np.arange(1, 33.0), indexing='ij')
    mm, nn = mm.ravel(), nn.ravel()
    m = blank()
    for i, u in enumerate(U):
        sx, sy = 0.13 + 0.37 * sm(u), 0.21 + 0.29 * sm(u)
        amp0 = (np.abs(np.sin(np.pi * mm * sx) * np.sin(np.pi * nn * sy)) + 0.01) / (mm * nn) ** 0.55
        al = geo(1.0, 3.4, u)
        f = mm ** 2 + (nn / al) ** 2
        x = 5.0 * f / (1.0 + 1.0 / al ** 2)
        ok = x <= TOP
        reson(m, i, x[ok], amp0[ok] * x[ok] ** (-0.55 + 1.0 * u), 0.12)
    return done(m)


def bell_times_bell():
    """BELL TIMES BELL — ring modulation of two bells: a bell (hum/prime/tierce/quint/nominal + upper series,
    virtual fundamental 6) multiplied by a copy tuned rho times higher — every partial of one against
    every partial of the other, sum AND difference (spectral convolution). Frame axis: rho glides 1.06 ->
    5.0 (the tilt brightens) and the dry bell hands over to the ring product. A bell -> a bell with its ghost -> a cloud of
    alien sum/difference tones that cross as the second bell rises."""
    low = np.array([0.5, 1.0, 1.2, 1.5, 2.0, 2.5, 2.66, 3.0, 4.2, 5.4, 6.8])
    j = np.arange(12, 24)
    br = np.concatenate([low, 6.8 * (j / 11.0) ** 1.28])
    v = 6.0
    xa = v * br
    aa = br ** -0.9
    m = blank()
    for i, u in enumerate(U):
        rho = geo(1.06, 5.0, u)
        aa = br ** -(0.9 - 0.55 * u)
        xb = xa * rho
        wet = 0.1 + 0.9 * ramp(u, 0.05, 0.6)
        put(m, i, xa, aa * (1.0 - 0.85 * wet))
        S = (xa[:, None] + xb[None, :]).ravel()
        Dd = np.abs(xa[:, None] - xb[None, :]).ravel() + 0.5
        A = (aa[:, None] * aa[None, :]).ravel() * wet
        put(m, i, S, A)
        put(m, i, Dd, A)
    return done(m)


def clangorous_feedback():
    """CLANGOROUS FEEDBACK — TIME, feedback FM run as a real recursion (8 cycles, the last one kept, its tail
    blended into the cycle before it so the wrap is seamless): carrier ratio 3 phase-modulated by a
    ratio-7 operator that feeds back on itself. Frame axis: index 0.5 -> 7 and self-feedback 0 -> 1.9, so a
    clean 3:7 clang (a bell-ish odd ladder) boils into the harsh noisy chaos of runaway feedback."""
    N = SZ * 4
    CY = 8
    n = np.arange(N * CY)
    t = n / N
    I = geo(0.5, 7.0, U)[:, None]
    fb = (1.9 * U ** 1.4)[:, None]
    y = np.zeros((F, n.size))
    prev = np.zeros((F, 1))
    for q in range(n.size):
        mod = np.sin(2 * np.pi * 7 * t[q] + fb * prev)
        prev = mod
        y[:, q:q + 1] = np.sin(2 * np.pi * 3 * t[q] + I * mod)
    last = y[:, (CY - 1) * N:]
    before = y[:, (CY - 2) * N:(CY - 1) * N]
    L = N // 16
    w = sm(np.linspace(0, 1, L))[None, :]
    out = last.copy()
    out[:, -L:] = (1 - w) * last[:, -L:] + w * before[:, -L:]
    return tdone(out)


TABLES = [
    ("SHRAPNEL BELL",        shrapnel_bell),
    ("PIANO WIRE SNAP",      piano_wire_snap),
    ("RING MOD SAW LADDER",  ring_mod_saw_ladder),
    ("SHIFTED SAW CLANG",    shifted_saw_clang),
    ("COWBELL TO CYMBAL",    cowbell_to_cymbal),
    ("LOGIC GATE HI HAT",    logic_gate_hi_hat),
    ("TUNING FORK BUZZ",     tuning_fork_buzz),
    ("SINGING BOWL SCREAM",  singing_bowl_scream),
    ("STEELPAN CIRCLE",      steelpan_circle),
    ("BRAKE SQUEAL",         brake_squeal),
    ("SUB ANVIL",            sub_anvil),
    ("CANTILEVER TINE",      cantilever_tine),
    ("UNDERCUT BAR",         undercut_bar),
    ("CRACKED CARILLON",     cracked_carillon),
    ("GONG CASCADE",         gong_cascade),
    ("OPERA GONG BEND",      opera_gong_bend),
    ("FOUNDRY BELL FM",      foundry_bell_fm),
    ("THUNDER SHEET",        thunder_sheet),
    ("SPINNING SAW BLADE",   spinning_saw_blade),
    ("FOLDED RING CLANG",    folded_ring_clang),
    ("SPRING COIL CHIRP",    spring_coil_chirp),
    ("BALL BEARING BOUNCE",  ball_bearing_bounce),
    ("WIND CHIME MOIRE",     wind_chime_moire),
    ("CRYSTAL SPLINTER",     crystal_splinter),
    ("PELOG METALLOPHONE",   pelog_metallophone),
    ("GRINDER SPARKS",       grinder_sparks),
    ("SQUARE TO GOLDEN PLATE", golden_plate),
    ("BELL TIMES BELL",      bell_times_bell),
    ("CLANGOROUS FEEDBACK",  clangorous_feedback),
]
CATEGORY = {ident: "Metallic" for ident, _ in TABLES}
