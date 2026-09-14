"""
gen2_basic.py — Terrain factory library, BASIC SHAPES (new tables for the 500, fb638).

The brief: "a LITTLE of the basics, done better than anyone". Every table here starts on a shape a
player can name — saw, square, triangle, pulse, sine sub — rendered band-limited to harmonic 1023,
and then a PROCESS takes it somewhere it has no business going: unison voices freeze and scatter,
octaves and fifths pile up until the top outweighs the root, a clean sub grows a crown, a mohawk, a
laser, a siren, gravel. Frame 0 is always playable; the last frame is the wild one.

Construction rules (wtlib contract):
  * SPEC builds write a complex harmonic spectrum (harmonics 1..1023) and inverse-FFT it — alias-free
    by construction. Basic shapes use COHERENT phases (a saw looks like a saw), not the random set.
  * TIME builds render at 8x (16384 samples) and keep harmonics <= 1023 before coming down to 2048.
  * Every table ends in fin(): one rotation for the whole table puts the wrap on a quiet sample
    (magnitudes and fingerprint untouched), then wtlib.finalize().
Deterministic: every random choice is a seeded numpy Generator.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, wtlib

F, N, SZ = wtlib.FRAMES, wtlib.NH, wtlib.SIZE
KH = N - 1                                   # harmonics 1..1023 (1024 is the Nyquist bin: never used)
k = np.arange(1, KH + 1, dtype=float)        # harmonic numbers
kr = k[None, :]
s = np.linspace(0.0, 1.0, F)                 # the frame axis, 0..1
sc = s[:, None]
ODD = (k % 2 == 1)
OS = 8
T8 = np.arange(SZ * OS) / (SZ * OS)          # one cycle at 8x
TWO_PI = 2.0 * np.pi


# ── helpers ─────────────────────────────────────────────────────────────────────────────────────
def col(v):
    return np.asarray(v, dtype=float).reshape(-1, 1)


def lin(a, b, c=1.0):
    return a + (b - a) * s ** c


def geo(a, b, c=1.0):
    return np.exp(np.log(a) + (np.log(b) - np.log(a)) * s ** c)


def ss(x, a, b):
    """smoothstep of x between a and b (broadcasts)."""
    u = np.clip((np.asarray(x, float) - a) / (b - a), 0.0, 1.0)
    return u * u * (3.0 - 2.0 * u)


def db(x):
    return 10.0 ** (np.asarray(x, float) / 20.0)


def sinph(tau=0.0):
    """Sine-series phase for every harmonic, delayed by tau cycles: (1/k)*sinph(tau) is a saw whose
    reset sits at t = tau."""
    return np.exp(1j * (-np.pi / 2 - TWO_PI * k * tau))


def cyc(C):
    """(F, KH) complex harmonic spectrum -> (F, SZ) cycles."""
    C = np.nan_to_num(np.asarray(C, dtype=complex))
    S = np.zeros((C.shape[0], SZ // 2 + 1), dtype=complex)
    S[:, 1:KH + 1] = C
    return np.fft.irfft(S, n=SZ, axis=1)


def spec8(y8):
    """(F, SZ*OS) 8x-rate cycles -> (F, KH) complex harmonic spectrum (harmonics 1..1023)."""
    return np.fft.rfft(y8, axis=1)[:, 1:KH + 1]


def down(y8):
    """Band-limit an 8x-rate cycle to harmonic 1023 and bring it to 2048 samples."""
    return cyc(spec8(y8))


def pulse_spec(D, t0):
    """Spectrum of a unit pulse, high on [t0, t0 + D) — any broadcastable D, t0."""
    return (1.0 - np.exp(-1j * TWO_PI * k * D)) / (1j * k) * np.exp(-1j * TWO_PI * k * t0)


def quiet_roll(x):
    """ONE rotation for every frame, chosen so the wrap lands on the quietest sample for the worst
    frame. A rotation leaves every magnitude, every metric and the fingerprint unchanged."""
    x = np.asarray(x, float)
    d = np.abs(x - np.roll(x, 1, axis=1))               # d[:, r] = |x[r] - x[r-1]|
    kk = max(1, int(round(0.01 * SZ)))
    typ = np.partition(d, SZ - kk, axis=1)[:, -kk:].mean(axis=1, keepdims=True)
    worst = (d / np.maximum(typ, 1e-12)).max(axis=0)
    r = int(np.argmin(worst))
    return np.roll(x, -r, axis=1)


def fin(x):
    return wtlib.finalize(quiet_roll(x))


def mix_phase(ph_top, ph1=-np.pi / 2):
    ph = np.array(ph_top, dtype=float).copy()
    ph[0] = ph1
    return ph


PH_RAND = wtlib.PHASE[:KH]


# ══════════════════════════════════════════════════════════════════════════════════════════════════
# SAW FAMILY — unison, stacks, shards
# ══════════════════════════════════════════════════════════════════════════════════════════════════
def _braid(T):
    """Nine band-limited saws with exactly linear detune d_j = (j-4)/4, frozen at time T: voice j
    sits at phase offset d_j*T. Returns the complex spectrum (F, KH)."""
    V = 9
    d = (np.arange(V) - V // 2) / (V // 2)
    g = 1.0 - 0.40 * np.abs(d)
    E = np.exp(-1j * TWO_PI * (d[None, :] * T[:, None])[:, :, None] * k[None, None, :])
    return np.einsum("v,fvk->fk", g, E) / kr * sinph(0.5), g.sum()


def unison_braid():
    """Supersaw in ONE cycle, frozen as it drifts. Nine band-limited saws (1..1023) with exactly
    linear detune: voice j sits at phase offset d_j*T. A linear unison keeps passing its own
    'phantom' moments — at T = 4/9, 8/9, 4/3 ... the nine resets fall evenly and the stack fuses
    into one saw NINE (or three) times higher — and between them it is a comb that never settles.
    It ends as nine scattered resets: a sawtooth shattered into a picket fence.
    Frame axis: time since the unison started, T 0 -> 2.3."""
    C, _ = _braid(2.3 * s ** 1.1)
    return fin(cyc(C))


def supersaw_sub():
    """The trance stack in one cycle: the same frozen nine-voice linear unison as UNISON BRAID, with a
    sine sub swelling underneath from a fifth of the way in until it is four times the whole stack.
    Frame 0 is one pristine saw; the last frame is a round sub wearing a scattered picket fence.
    Frame axis: unison time T 0 -> 2.3, sub level 0 -> +12 dB over the stack."""
    C, gs = _braid(2.3 * s ** 1.1)
    C[:, 0] += 4.0 * gs * ss(s, 0.25, 1.0) * np.exp(-1j * np.pi / 2)
    return fin(cyc(C))

def square_swarm():
    """Supersquare in ONE cycle, with drifting duty. Eleven pulse voices frozen at detune offsets
    d_j*T; each voice's duty also walks off 50 % by its own seeded amount, so the odd-only square
    grows even harmonics as the swarm spreads. Recipe: exact pulse spectra summed.
    Frame axis: spread T 0 -> 1.8 cycles and duty drift 0 -> +-0.34 — square, a staircase of edges,
    then a swarm of uneven slabs."""
    V = 11
    rng = np.random.default_rng(1111)
    d = np.linspace(-1.0, 1.0, V) + rng.uniform(-0.05, 0.05, V)
    d[V // 2] = 0.0
    w = rng.uniform(-0.34, 0.34, V); w[V // 2] = 0.0
    order = np.argsort(np.abs(d), kind="stable")
    enter = np.empty(V); enter[order] = np.linspace(0.0, 0.55, V)
    g = ss(sc, enter[None, :], enter[None, :] + 0.16)
    g[:, order[0]] = 1.0
    T = 1.8 * s ** 1.3
    t0 = d[None, :] * T[:, None] + 0.25
    D = 0.5 + w[None, :] * ss(sc, 0.25, 1.0)
    C = np.zeros((F, KH), dtype=complex)
    for j in range(V):
        C += g[:, j:j + 1] * pulse_spec(D[:, j:j + 1], t0[:, j:j + 1])
    return fin(cyc(C))



def octave_tower():
    """Sub under an octave tower built from the top down. A clean sine; then band-limited saws an
    octave apart land on it from the top — 512x, 256x, ... down to 2x — each with its reset at its
    own golden-ratio place, so the sine grows interleaved rows of teeth instead of one spike. The root
    is a sine, so odd harmonics never exist: the last frame is a sub carrying an octave-up sawtooth
    fractal. Frame axis: octaves stacked, highest first (0 -> 9)."""
    hs = [2 ** o for o in range(9, 0, -1)]
    starts = list(np.linspace(0.0, 0.84, len(hs)))
    C = np.zeros((F, KH), dtype=complex)
    for i, h in enumerate(hs):
        mask = (k % h == 0)
        gi = ss(s, starts[i], starts[i] + 0.14)
        C[:, mask] += 0.45 * gi[:, None] * (h / k[mask])[None, :] * sinph(0.5 + 0.61803 * i / h)[mask][None, :]
    C[:, 0] += np.exp(-1j * np.pi / 2)
    return fin(cyc(C))

def fifth_tower():
    """Sub under a tower of twelfths (octave + fifth), built from the top down out of SQUARES: squares
    at 729x, 243x, 81x, 27x, 9x, 3x land on a clean sine in the first third, edges aligned, then the
    whole tower keeps swelling until it stands level with the root. Only odd multiples of three ever
    sound above the sub — a hollow, bell-bright power-chord spine of stacked blocks.
    Frame axis: twelfths stacked (highest first), then tower level 0.5 -> 1.0."""
    hs = [3 ** o for o in range(6, 0, -1)]
    starts = list(np.linspace(0.0, 0.30, len(hs)))
    lvl = lin(0.5, 1.0)
    A = np.zeros((F, KH))
    for i, h in enumerate(hs):
        m = k / h
        mask = (np.abs(m - np.round(m)) < 1e-9) & (np.round(m) % 2 == 1)
        A[:, mask] += (lvl * ss(s, starts[i], starts[i] + 0.12))[:, None] * (h / k[mask])[None, :]
    C = A * sinph(0.37)
    C[:, 0] += np.exp(-1j * np.pi / 2)
    return fin(cyc(C))

def square_bit_counter():
    """A square that counts. Squares at 1x, 2x, 4x ... 512x are summed with weights r^o — each one a
    bit of a binary counter. r 0 -> 0.5 turns the square into a staircase that converges on a perfect
    saw (the counter's ramp); r -> 1 makes every bit equal (blocky fractal steps); r -> 1.7 lets the
    top bits outshout the root. Bit o owns exactly the harmonics 2^o * odd, so the bits never share
    a harmonic: each is skewed by 0.011*o cycle to keep the carry from stacking into one spike,
    at no cost to the spectrum. Exact square spectra, 1..1023. Frame axis: bit weight r."""
    r = np.interp(s, [0.0, 0.34, 0.66, 1.0], [0.0, 0.5, 1.0, 1.7])
    C = np.zeros((F, KH), dtype=complex)
    for o in range(10):
        h = 2 ** o
        m = k / h
        mask = (np.abs(m - np.round(m)) < 1e-9) & (np.round(m) % 2 == 1)
        C[:, mask] += (r ** o)[:, None] * (h / k[mask])[None, :] * sinph(0.5 + 0.011 * o)[mask][None, :]
    return fin(cyc(C))

def ramp_origami():
    """A saw folded like paper. The ramp is multiplied by Rademacher sign patterns of rising order —
    halves, quarters, eighths ... 1/256 — each fold phased in smoothly. The first fold turns the saw
    into a tent, the next into a zigzag of ramp pieces, the last into a ramp-shaped barcode whose
    energy has climbed the series. TIME build at 8x.
    Frame axis: folds made (0 -> 8)."""
    tt = T8[None, :]
    r = 1.0 - 2.0 * ((T8 + 0.5) % 1.0)[None, :]
    y = np.repeat(r, F, axis=0)
    for o in range(1, 9):
        st = 0.03 + (o - 1) * 0.105
        a = ss(sc, st, st + 0.14)
        bit = np.floor((2 ** o) * tt + 1e-9) % 2.0
        y = y * (1.0 - 2.0 * a * bit)
    return fin(down(y))

# ══════════════════════════════════════════════════════════════════════════════════════════════════
# SQUARE / PULSE FAMILY
# ══════════════════════════════════════════════════════════════════════════════════════════════════
def pulse_doublet():
    """A square that splits into a bipolar pulse pair. The +half and the -half both narrow (duty
    0.5 -> 0.09, odd-only slivers half a cycle apart), then the negative sliver slides up until it
    touches the positive one — a doublet, the derivative of a pulse, spitting even harmonics.
    SPEC build, exact. Frame axis: width first, then gap."""
    u1 = ss(s, 0.0, 0.52)
    u2 = ss(s, 0.42, 1.0)
    w = np.exp(np.log(0.5) + (np.log(0.09) - np.log(0.5)) * u1) * (1 - 0.3 * u2)
    gap = np.maximum(0.5 - (0.5 - 0.03) * u2, w + 0.005)
    C = pulse_spec(col(w), 0.1) - pulse_spec(col(w), 0.1 + col(gap))
    return fin(cyc(C))


def square_stutter():
    """A square whose plateaus stutter. Inside each half-cycle a gate hard-synced to the half starts
    chopping: M gate cycles per half, M 1 -> 90, and the chop depth rises until the plateaus are
    bursts of needles; the low half joins later, so the table passes through lopsided even-harmonic
    shapes before ending half-wave symmetric again. TIME build at 8x.
    Frame axis: gate rate (and chop depth)."""
    tt = T8[None, :]
    M = geo(1.0, 90.0, 1.3)[:, None]
    A = ss(sc, 0.0, 0.3)
    B = ss(sc, 0.35, 0.85)
    u = (tt % 0.5) / 0.5
    gate = np.floor(M * u) % 2.0
    y = np.where(tt < 0.5, 1.0 - 2.0 * A * gate, -1.0 + 2.0 * B * gate)
    return fin(down(y))


def pulse_barcode():
    """A square that turns into a barcode. A 96-slot grid is laid over the cycle and 96 seeded
    bars open one after another, each flipping the level inside its own slot, until the square is a
    dense two-level code on the grid. It never stops being a pulse wave — always +-1 — so it stays
    LOUD while the spectrum goes from odd-harmonic square to a gridded code (sinc-shaped, with holes
    at every 96th harmonic). TIME build at 8x with fractional (anti-aliased) edges.
    Frame axis: bars opened (and how wide each has grown)."""
    G = 96
    rng = np.random.default_rng(240)
    slots = rng.choice(G, size=G, replace=False)
    act = np.sort(rng.uniform(0.02, 0.88, G))
    L = SZ * OS
    base = np.where(T8 < 0.5, 1.0, -1.0)
    out = np.empty((F, L))
    for f in range(F):
        w = (1.0 / G) * ss(s[f], act, act + 0.1)
        on = w > 0
        c = np.zeros(L + 1)
        for a0, wj in zip(slots[on] / G, w[on]):
            for lo, hi in _wrap_iv(a0 * L, (a0 + wj) * L, L):
                _cover(c, lo, hi)
        out[f] = base * np.cos(np.pi * np.cumsum(c)[:L])
    return fin(down(out))

def _wrap_iv(lo, hi, L):
    if hi <= L:
        return [(lo, hi)]
    return [(lo, float(L)), (0.0, hi - L)]


def _cover(c, lo, hi):
    """Add fractional coverage of [lo, hi) (in samples) to the difference array c."""
    i0, i1 = int(np.floor(lo)), int(np.floor(hi))
    f0, f1 = lo - i0, hi - i1
    c[i0] += 1.0 - f0
    if i0 + 1 < len(c):
        c[i0 + 1] += f0
    c[i1] -= 1.0 - f1
    if i1 + 1 < len(c):
        c[i1 + 1] -= f1


def square_accelerando():
    """A square that accelerates inside its own cycle. Instantaneous rate f(t) = 1 + (R-1) t within
    each period (a linear chirp), R 1 -> 300 (R-1 grows as s^2.6): the first plateau stays wide while the rest of the
    cycle crowds into a buzz — a chirp you can see, a bouncing-ball rhythm on a square, energy spread
    flat up to harmonic R. TIME build at 8x. Frame axis: the end-of-cycle rate R."""
    R = (1.0 + 299.0 * s ** 2.6)[:, None]
    tt = T8[None, :]
    psi = tt + (R - 1.0) * tt ** 2 / 2.0
    y = np.tanh(40.0 * np.sin(TWO_PI * psi))
    return fin(down(y))

# ══════════════════════════════════════════════════════════════════════════════════════════════════
# TRIANGLE FAMILY
# ══════════════════════════════════════════════════════════════════════════════════════════════════
def _tri8():
    return 4.0 * np.abs(((T8 - 0.25) % 1.0) - 0.5) - 1.0


def class_d_triangle():
    """A triangle played through a class-D stage: a comparator against a triangle carrier turns it
    into a train of pulses whose widths ARE the triangle. The carrier falls from 300x to 5.5x the
    pitch and the pulse-coded copy takes over, so the smooth triangle turns into a comb of fat and
    thin slabs with the triangle still audible underneath. TIME build at 8x (smooth comparator).
    Frame axis: carrier rate down, pulse-coded share up."""
    tt = T8[None, :]
    v = _tri8()[None, :]
    Nc = geo(300.0, 5.5, 0.9)[:, None]
    car = 2.0 * np.abs(2.0 * ((Nc * tt) % 1.0) - 1.0) - 1.0
    pwm = np.tanh(60.0 * (0.92 * v - car))
    a = ss(sc, 0.0, 0.35)
    return fin(down((1.0 - a) * v + a * pwm))

def breadknife_triangle():
    """A serrated triangle. Each slope carries a row of saw teeth hard-synced to the corners (so
    they restart at every peak), R teeth per slope, R 1.5 -> 160, tooth depth rising to 1.0 — the
    edge of a bread knife, then a chainsaw. Both slopes carry teeth of the SAME sign, so the
    serration breaks the triangle's symmetry and even harmonics grow with it. TIME build at 8x.
    Frame axis: teeth per slope (and their depth)."""
    tt = T8[None, :]
    v = _tri8()[None, :]
    u = ((tt - 0.25) % 0.5) / 0.5
    R = geo(1.5, 160.0, 0.8)[:, None]
    teeth = 1.0 - 2.0 * ((R * u) % 1.0)
    A = 1.0 * ss(sc, 0.0, 0.6) ** 0.8
    return fin(down(v + A * teeth))

# ══════════════════════════════════════════════════════════════════════════════════════════════════
# SUBS — a clean sine body that survives while the top goes monstrous
# ══════════════════════════════════════════════════════════════════════════════════════════════════


def sub_mohawk():
    """Sine sub with a mohawk: a band of random-phase harmonics (hair, not tone) sits above an edge
    that falls from harmonic 420 to 7 while its level rises from -78 to -33 dB per harmonic and its
    tilt flattens to white. The fundamental never moves; the fringe grows until it is a roar.
    SPEC build. Frame axis: edge down, level up."""
    H = geo(420.0, 7.0, 0.9)[:, None]
    L = db(-78.0 + 45.0 * s ** 0.8)[:, None]
    q = lin(1.3, 0.0)[:, None]
    m = L * (kr / H) ** (-q) / (1.0 + (H / kr) ** 8)
    m[:, 0] = 1.0
    m[:, 1:3] *= 0.0
    ph = mix_phase(PH_RAND)
    return fin(cyc(m * np.exp(1j * ph)[None, :]))


def sub_sync_scream():
    """Sine sub under a hard-sync scream. A slave saw hard-synced to the cycle sweeps 1.3 -> 30x;
    its harmonics below 5 are cut so the sine owns the bottom, and the sync layer rises from -60 dB
    to -4 dB of the sub. Frame axis: sync ratio (the formant climbs) and layer level."""
    R = geo(1.3, 29.37, 1.1)[:, None]
    saw = 1.0 - 2.0 * ((T8[None, :] * R) % 1.0)
    C = spec8(saw)
    C[:, :4] = 0.0
    C = C / np.maximum(np.abs(C).max(axis=1, keepdims=True), 1e-12)
    C = C * db(-60.0 + 56.0 * s ** 0.7)[:, None]
    C[:, 0] += np.exp(-1j * np.pi / 2)
    return fin(cyc(C))


def sub_crown():
    """Sine sub wearing a folded crown. The same sine is folded (sin(G*x + 0.6), G 0.5 -> 420: the
    wavefolder's PM plateau widens up the series), its lowest harmonics are removed and it is laid
    over the untouched sub, rising from -46 to -6 dB. Frame axis: fold gain (crown height) and level."""
    x = np.sin(TWO_PI * T8)[None, :]
    G = geo(0.5, 420.0, 0.6)[:, None]
    z = np.sin(G * x + 0.6)
    C = spec8(z)
    C[:, :2] = 0.0
    C = C / np.maximum(np.abs(C).max(axis=1, keepdims=True), 1e-12)
    C = C * db(-46.0 + 40.0 * s ** 0.5)[:, None]
    C[:, 0] += np.exp(-1j * np.pi / 2)
    return fin(cyc(C))


def sub_whistle():
    """Sine sub with a whistle riding it: one narrow resonant packet (log-Gaussian around harmonic c)
    with a gentle chirp phase, so it reads as a short ringing sweep on the sine's falling edge.
    c climbs 4 -> 640 while the packet narrows and rises from -40 to -4 dB. Frame axis: whistle pitch."""
    c = geo(4.0, 640.0, 1.0)[:, None]
    w = lin(0.22, 0.08)[:, None]
    lvl = db(-40.0 + 36.0 * s ** 0.7)[:, None]
    pk = lvl * np.exp(-0.5 * ((np.log(kr) - np.log(c)) / w) ** 2)
    pk[:, 0] = 0.0
    C = pk * np.exp(1j * (-TWO_PI * kr * 0.5 - np.pi * 2.5 * (kr - c) ** 2 / c))
    C[:, 0] += np.exp(-1j * np.pi / 2)
    return fin(cyc(C))

def square_sub_snarl():
    """A round sub that snarls in odd harmonics. The body is h1 + h3/4 (a rounded square, heavy and
    clean), and above it an odd-harmonic top combed into formant teeth whose spacing shrinks (160 ->
    10 harmonics) while it rises from -60 to -13 dB. A light dispersive phase smears
    each comb tooth into a ringing chirp off the body's shoulders. Frame axis: comb spacing and snarl level."""
    body = np.zeros(KH); body[0] = 1.0; body[2] = 0.25
    P = geo(160.0, 10.0)[:, None]
    comb = (0.5 + 0.5 * np.cos(TWO_PI * kr / P)) ** 3
    top = db(-60.0 + 47.0 * s ** 0.6)[:, None] * comb * kr ** -0.15 * (kr >= 5) * ODD[None, :]
    C = body[None, :] * sinph(0.25) + top * sinph(0.25) * np.exp(-1j * np.pi * 0.5 * k ** 2 / KH)
    return fin(cyc(C))

def sub_rattle():
    """Sine sub that rattles: 64 band-limited clicks at seeded places around the cycle, seeded
    signs, joining one by one and growing, until the smooth sub is peppered with spikes.
    Harmonics below 4 are left to the sine. Frame axis: clicks in the cycle and their size."""
    rng = np.random.default_rng(6464)
    K = 64
    p = rng.uniform(0.0, 1.0, K)
    sg = rng.choice([-1.0, 1.0], K)
    act = np.sort(rng.uniform(0.0, 0.8, K))
    a = ss(sc, act[None, :], act[None, :] + 0.15) * sg[None, :]
    E = np.exp(-1j * TWO_PI * p[:, None] * k[None, :])
    C = (a @ E) * (kr >= 4) * (2.4 / KH) * db(-12.0 + 6.0 * s)[:, None]
    C[:, 0] += np.exp(-1j * np.pi / 2)
    return fin(cyc(C))


def sub_laser():
    """Sine sub with a laser across it: a flat band of harmonics from 6 up to an edge that climbs from
    40 to 1023, given a quadratic (dispersive) phase so it sweeps through the cycle as one rising
    chirp — a zap drawn on top of the sub. Level rises -60 -> -22 dB per harmonic.
    Frame axis: laser bandwidth and level."""
    U = geo(40.0, 1023.0, 0.8)[:, None]
    band = (kr >= 6) / (1.0 + (kr / U) ** 10)
    L = db(-60.0 + 38.0 * s ** 0.6)[:, None]
    ph = -np.pi * 0.9 * k ** 2 / KH
    C = L * band * np.exp(1j * ph)[None, :]
    C[:, 0] += np.exp(-1j * np.pi / 2)
    return fin(cyc(C))


def sub_siren():
    """Sine sub with a siren above it: a broad random-phase formant band that swings up and down three
    and a half times while its centre climbs from harmonic 8 to 500. The band is held at a fixed
    POWER (not a fixed level per harmonic), rising from -40 to -6 dB against the sub, so the sub keeps
    its weight while the siren widens up the series. Frame axis: siren height (with its swing), level."""
    lc = np.log(8.0) + (np.log(500.0) - np.log(8.0)) * np.clip(s + 0.10 * np.sin(TWO_PI * 3.5 * s) * s, 0, 1)
    band = np.exp(-0.5 * ((np.log(kr) - lc[:, None]) / 0.38) ** 2)
    band[:, :3] = 0.0
    band = band / np.sqrt((band ** 2).sum(axis=1, keepdims=True))
    m = db(-40.0 + 34.0 * s ** 0.7)[:, None] * band
    m[:, 0] = 1.0
    return fin(cyc(m * np.exp(1j * mix_phase(PH_RAND))[None, :]))

def sub_gravel():
    """Sine sub with gravel in it: a random set of harmonics (seeded, each with its own Rayleigh
    level) whose density grows from 0.5 % to the whole series between harmonic 10 and 1023, rising
    from -60 to -17 dB. A few glassy pings over the sub, then grit, then a full crunchy carpet.
    Frame axis: grit density and level."""
    rng = np.random.default_rng(1010)
    thr = rng.uniform(0.0, 1.0, KH)
    amp = rng.rayleigh(1.0, KH)
    rho = geo(0.005, 1.0, 0.9)[:, None]
    on = ss(rho - thr[None, :], -0.02, 0.02)
    m = db(-60.0 + 43.0 * s ** 0.7)[:, None] * on * amp[None, :] * (kr >= 10) * kr ** -0.25
    m[:, 0] = 1.0
    return fin(cyc(m * np.exp(1j * mix_phase(PH_RAND))[None, :]))

TABLES = [
    ("UNISON BRAID",        unison_braid),
    ("SUPERSAW SUB",        supersaw_sub),
    ("SQUARE SWARM",        square_swarm),
    ("OCTAVE TOWER",        octave_tower),
    ("FIFTH TOWER",         fifth_tower),
    ("SQUARE BIT COUNTER",  square_bit_counter),
    ("RAMP ORIGAMI",        ramp_origami),
    ("PULSE DOUBLET",       pulse_doublet),
    ("SQUARE STUTTER",      square_stutter),
    ("PULSE BARCODE",       pulse_barcode),
    ("SQUARE ACCELERANDO",  square_accelerando),
    ("CLASS D TRIANGLE",    class_d_triangle),
    ("BREADKNIFE TRIANGLE", breadknife_triangle),
    ("SUB MOHAWK",          sub_mohawk),
    ("SUB SYNC SCREAM",     sub_sync_scream),
    ("SUB CROWN",           sub_crown),
    ("SUB WHISTLE",         sub_whistle),
    ("SQUARE SUB SNARL",    square_sub_snarl),
    ("SUB RATTLE",          sub_rattle),
    ("SUB LASER",           sub_laser),
    ("SUB SIREN",           sub_siren),
    ("SUB GRAVEL",          sub_gravel),
]
CATEGORY = {ident: "Basic Shapes" for ident, _ in TABLES}
