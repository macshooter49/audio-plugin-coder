"""
gen2_digital.py — the Terrain 500, DIGITAL category: new candidates (fb638).

EXTREME digital, from first principles only: FM with sine / square / chip-shaped operators,
Casio-style phase distortion taken past its limits, float / mu-law / 1-bit / PWM-DAC crushing,
block-codec and wavelet starvation, wavefolding and two's-complement overflow, bitwise logic on
integer waveforms, bit-plane / bit-reversal / XOR index permutations, shift-register chip noise,
stepped scans and glitch jumps. Nothing is sampled, traced or derived from anyone's wavetables.

THE JOURNEY RULE: frame 0 is playable, the PROCESS evolves across the 128 frames, the last frame
is wild. A few are wild from frame 0; several are SUB tables whose fundamental survives.

Anti-alias policy: continuous-time constructions (FM, PD, folds, edges) are rendered on an
oversampled grid of one exact period and reduced to 2048 samples by discarding every harmonic
above 1023 (truncation, never decimation, so nothing folds back). Integer / bitwise tables whose
waveform IS a 2048-sample digital signal are built natively on the table's own grid.
Every table ends in seam_roll() (one rotation for all frames, spectrum untouched) + wtlib.finalize.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import wtlib

F   = wtlib.FRAMES
N   = wtlib.SIZE
NH  = wtlib.NH
TAU = 2.0 * np.pi
u   = np.linspace(0.0, 1.0, F)             # frame axis 0..1
U   = u[:, None]

OS  = 8
NOS = N * OS
to  = np.arange(NOS) / NOS                  # oversampled cycle, [0,1)
po  = TAU * to
tn  = np.arange(N) / N                      # native cycle
kn  = np.arange(N)                          # native sample index (11 bits)


# ── helpers ───────────────────────────────────────────────────────────────────────────────
def down(w, nmax=NH - 1):
    """(F, k*N) exact-period render -> (F, N), harmonics above nmax discarded."""
    w = np.atleast_2d(np.asarray(w, dtype=float))
    S = np.fft.rfft(w, axis=1)
    out = np.zeros((w.shape[0], N // 2 + 1), dtype=complex)
    m = min(nmax + 1, S.shape[1])
    out[:, :m] = S[:, :m]
    out[:, 0] = 0.0
    return np.fft.irfft(out, n=N, axis=1) * (N / w.shape[1])


def seam_roll(fr):
    """Rotate EVERY frame by the same amount so the worst wrap jump sits on a quiet point.
    A common rotation leaves every magnitude spectrum, metric and the fingerprint unchanged."""
    x = np.asarray(fr, dtype=float)
    x = x - x.mean(axis=1, keepdims=True)
    d = np.abs(np.diff(x, axis=1))
    k = 20
    typ = np.maximum(np.partition(d, d.shape[1] - k, axis=1)[:, -k:].mean(axis=1), 1e-12)
    cand = np.arange(0, N, 16)
    best, bs = 0, 1e18
    for s in cand:
        a = x[:, (s - 1) % N]; b = x[:, s]
        r = np.abs(a - b) / typ
        v = r.max()
        if v < bs:
            bs, best = v, s
    return np.roll(x, -best, axis=1)


def fin(fr):
    return wtlib.finalize(seam_roll(fr))


def ramp(x, a, b):
    return np.clip((x - a) / (b - a), 0.0, 1.0)


def sq(ph):
    """bipolar square of a phase in radians (sign of sine), exact +-1."""
    return np.where(np.mod(ph, TAU) < np.pi, 1.0, -1.0)


def tri(ph):
    return (2.0 / np.pi) * np.arcsin(np.sin(ph))


def saw(ph):
    return 1.0 - 2.0 * np.mod(ph / TAU, 1.0)


def tfold(x):
    return (2.0 / np.pi) * np.arcsin(np.sin(np.pi * x / 2.0))


def wrap(x):
    """two's-complement overflow into [-1, 1)"""
    return np.mod(x + 1.0, 2.0) - 1.0


def xint(vals, build, length):
    """crossfade an integer-only builder along a continuous per-frame parameter."""
    lo = np.floor(vals).astype(int)
    fr = vals - lo
    cache = {}
    out = np.zeros((F, length))
    for i in range(F):
        for k in (lo[i], lo[i] + 1):
            if k not in cache:
                cache[k] = np.asarray(build(k), dtype=float)
        out[i] = (1.0 - fr[i]) * cache[lo[i]] + fr[i] * cache[lo[i] + 1]
    return out


def fbop(phase, beta, extra=0.0, iters=32):
    ph = phase + extra
    y = np.zeros(np.broadcast(ph, beta).shape)
    for _ in range(iters):
        y = np.sin(ph + beta * y)
    return y


def lowpass_h(fr, cut, order=4):
    """per-frame harmonic low-pass (Butterworth magnitude), cut = per-frame cutoff in harmonics."""
    S = np.fft.rfft(fr, axis=1)
    k = np.arange(S.shape[1])[None, :]
    c = np.asarray(cut, dtype=float).reshape(-1, 1)
    S = S / np.sqrt(1.0 + (k / c) ** (2 * order))
    return np.fft.irfft(S, n=fr.shape[1], axis=1)


# ══════════════════════════════════════ FM ══════════════════════════════════════════════════
def brick_fm():
    """A PULSE carrier phase-modulated by a sine at ratio 2 (index 0 -> 40) while its comparator
    threshold sweeps -0.5 -> 0.75: a 33% pulse passes through a square into a narrowing pulse
    whose edges swarm. Axis: pulse -> bent PWM blocks -> a dense barcode of slivers. Blocky 3D."""
    I = 40.0 * U ** 1.35
    th = -0.5 + 1.25 * U ** 1.1
    w = np.where(np.sin(po[None, :] + I * np.sin(2.0 * po[None, :] + 0.3)) > th, 1.0, -1.0)
    return fin(down(w))


def square_on_square():
    """Square carrier, SQUARE modulator at ratio 3 (index 0 -> 14) plus a triangle modulator at
    ratio 5 (0 -> 30): the square modulator throws hard phase jumps, the triangle bends the edges
    between them. Axis: square -> stuttering shifted blocks -> shredded pulse glyphs."""
    I = 14.0 * U ** 1.2
    J = 30.0 * U ** 1.8
    w = sq(po[None, :] + I * sq(3.0 * po[None, :]) + J * tri(5.0 * po[None, :] + 0.7))
    return fin(down(w))


def ratio_staircase():
    """2-op FM with the modulator ratio QUANTISED to 1, 2, 3 ... 24 (each held ~5 frames, then a
    one-frame snap), index 9 fixed, plus a ratio-1 modulator whose index creeps 2 -> 7 to fill the
    gaps between sidebands. Axis: a stepped scan, bell -> buzz -> tight high comb; the 3D view
    is a flight of stairs."""
    steps = 24
    pos_f = u * (steps - 1)
    hold = np.floor(pos_f)
    fr = np.clip((pos_f - hold - 0.8) / 0.2, 0.0, 1.0)
    vals = hold + fr
    def build(r):
        return np.sin(po + 9.0 * np.sin((r + 1) * po))
    base = xint(vals, build, NOS)
    I2 = 2.0 + 5.0 * U
    fill = np.sin(po[None, :] * 2.0 + I2 * np.sin(po[None, :]))
    return fin(down(base + 0.45 * fill * base))


def chip_half_sine():
    """FM-chip operators: a HALF-SINE carrier (sine with the negative lobe muted) modulated by an
    ABS-SINE operator at ratio 2, index 0 -> 16, then the modulator feeds back on itself.
    Axis: the chip half-sine you know, to a nasal chip buzz, to feedback grit."""
    I = 16.0 * U ** 1.3
    b = 2.2 * ramp(U, 0.5, 1.0) ** 1.2
    m = np.abs(fbop(2.0 * po[None, :], b, iters=24))
    ph = po[None, :] + I * (m - 2.0 / np.pi)
    w = np.maximum(np.sin(ph), 0.0)
    return fin(down(w))


def chip_quarter_pulse():
    """FM-chip 'pulse-sine' operator (the first quarter of each half-sine, then silence) as the
    carrier, driven by a second pulse-sine at ratio 3 and an abs-sine at ratio 7.
    Axis: a ticking chip pulse to a clattering 4-op chip lead to scrambled chip dust."""
    def psine(ph):
        q = np.mod(ph, np.pi)
        return np.where(q < np.pi / 2, np.abs(np.sin(ph)), 0.0)
    I = 12.0 * U ** 1.2
    J = 20.0 * ramp(U, 0.3, 1.0) ** 1.5
    m = I * psine(3.0 * po[None, :]) + J * np.abs(np.sin(7.0 * po[None, :] + 0.4))
    return fin(down(psine(po[None, :] + m) - 0.5 * psine(po[None, :] + np.pi + m)))


def stepped_pitch_fm():
    """FM by a seeded 8-step sample-and-hold sequence (zero-mean, so the carrier phase closes
    exactly): the carrier frequency JUMPS between 8 plateaus, through zero at high depth
    (0 -> 140 cycles of excursion). Axis: sine -> a carrier hopping pitches in blocks ->
    glitchy chirp shards."""
    rng = np.random.default_rng(1608)
    st = rng.uniform(-1, 1, 8); st -= st.mean()
    m = st[(to * 8).astype(int)]
    D = 140.0 * U ** 1.7
    ph = TAU * (to[None, :] + D * np.cumsum(m)[None, :] / NOS)
    return fin(down(np.sin(ph) + 0.5 * sq(ph) * U))


def expo_fm_chirp():
    """Exponential FM on a TRIANGLE: phase t + 12 (c(t) - t), c the normalised integral of
    2^(D sin 2pi t), D 0 -> 10 octaves, over a fixed sine fundamental. The triangle's teeth
    bunch into a squealing burst and stretch into slow ramps — asymmetric sidebands a linear FM
    cannot make. Axis: sine+triangle shimmer -> squelch -> one-sided laser chirp per cycle."""
    D = 10.0 * U ** 1.3
    inst = 2.0 ** (D * np.sin(TAU * to[None, :] + 0.4))
    c = np.cumsum(inst, axis=1); c = c / c[:, -1:]
    ph = to[None, :] + 12.0 * (c - to[None, :])
    return fin(down(tri(TAU * ph) + 0.5 * np.sin(TAU * to)[None, :]))


def cross_feedback_pair():
    """Two operators modulating EACH OTHER (a = sin(t + B b), b = sin(3t + G a)), solved by a
    fixed number of iterations. Past the stable point the pair tears into a deterministic storm.
    Axis: sine -> reedy 3:1 pair -> torn feedback noise, pitched."""
    B = 2.6 * U ** 1.1
    G = 3.4 * U ** 1.3
    a = np.zeros((F, NOS)); b = np.zeros((F, NOS))
    p = po[None, :]
    for _ in range(20):
        a, b = np.sin(p + B * b), np.sin(3.0 * p + G * a)
    return fin(down(a + 0.35 * b))


def sideband_avalanche():
    """A modulator that is itself an FM tone (11:13, index 0 -> 4) driving a sine carrier at
    index 0 -> 14, sized so the widest sideband lands just under harmonic 1023: sidebands on
    sidebands until the band fills to Nyquist with a constant envelope.
    Axis: sine -> glassy clusters -> a flat roaring wall."""
    I = 14.0 * U ** 1.6
    J = 4.0 * U ** 1.2
    m = np.sin(11.0 * po[None, :] + J * np.sin(13.0 * po[None, :]))
    return fin(down(np.sin(po[None, :] + I * m)))


def sub_under_fm():
    """SUB: a pure sine fundamental that never moves, under an FM voice on harmonic 2 (mod ratio
    3, index 0 -> 30, self-feedback in the last third) whose level rises to meet it.
    Axis: clean sub -> sub with a hollow FM bark on top -> sub under a torn FM top."""
    I = 30.0 * U ** 1.4
    b = 1.8 * ramp(U, 0.6, 1.0)
    top = fbop(2.0 * po[None, :], b, extra=I * np.sin(3.0 * po[None, :]), iters=24)
    return fin(down(np.sin(po)[None, :] * 1.0 + (0.15 + 0.55 * U) * top))


def feedback_brick():
    """A feedback operator through a comparator whose feedback is SQUARED (y^2, so it only ever
    pushes one way): y = tanh(G sin(t + B y^2 + FM)), iterated, drive G 3 -> 9, feedback B 0 -> 9.
    Axis: rounded square -> ragged square with extra teeth -> a stuttering lopsided bit-pattern."""
    B = 9.0 * U ** 1.2
    G_ = 3.0 + 6.0 * U ** 0.7
    p = po[None, :]
    y = np.zeros((F, NOS))
    for _ in range(9):
        y = 0.4 * y + 0.6 * np.tanh(G_ * np.sin(p + B * y * y + 1.5 * U * np.sin(3 * p)))
    return fin(down(np.tanh(1.5 * y)))


def four_op_cascade():
    """4-op series stack 9 -> 4 -> 2 -> 1, each index blooming in turn (top operator last), with
    operator 4 feeding back. Axis: sine -> octave FM organ -> 9-comb metal -> saturated storm."""
    I1 = 7.0 * ramp(U, 0.0, 0.4) ** 1.1
    I2 = 6.0 * ramp(U, 0.2, 0.65)
    I3 = 9.0 * ramp(U, 0.45, 1.0) ** 1.3
    b4 = 1.6 * ramp(U, 0.7, 1.0)
    p = po[None, :]
    o4 = fbop(9.0 * p, b4, iters=20)
    o3 = np.sin(4.0 * p + I3 * o4)
    o2 = np.sin(2.0 * p + I2 * o3)
    return fin(down(np.sin(p + I1 * o2 + 0.6 * I3 * o2 * o3)))


def triangle_saw_rake():
    """FM with non-sine operators: a TRIANGLE carrier bent by a SAW modulator at ratio 3 (index
    0 -> 44) and a slower saw at ratio 1. The saw resets kick every edge sideways.
    Axis: triangle -> leaning zigzags -> dense rake of saw-kicked teeth."""
    I = 44.0 * U ** 1.4
    J = 6.0 * U
    return fin(down(tri(po[None, :] + I * saw(3.0 * po[None, :] + 1.0) + J * saw(po[None, :]))))


def square_multiplier():
    """Squares multiplied together (the integer twin of XOR): a square at 1 times squares at 3,
    7, 13, 29, 61 phased in one after another, each entering by morphing its duty from 0 to 50%.
    Axis: a plain square -> castles -> a maze of tiny blocks. Pure hard edges."""
    rats = [3, 7, 13, 29, 61]
    y = sq(po)[None, :] * np.ones((F, 1))
    for j, r in enumerate(rats):
        a = ramp(U, 0.15 * j, 0.15 * j + 0.3)
        duty_th = np.cos(np.pi * a)          # threshold 1 -> -1 : factor is +1 until it opens
        fac = np.where(np.sin(r * po[None, :] + 0.37 * j) > duty_th * 1.0001, -1.0, 1.0)
        y = y * fac
    return fin(down(y))


# ═════════════════════════════════════ PHASE DISTORTION ═════════════════════════════════════
def cz_square_bite():
    """CZ square, LOPSIDED: each half-cycle the phase races through a half turn in a window,
    then holds. The first half's window shrinks 0.5 -> 0.004 while the second half starts sharp
    (0.12); then both races OVER-WIND by different amounts (W1 1 -> 13, W2 1 -> 21) as the
    windows re-open, so the two edges ring at different pitches.
    Axis: half-sine/half-square -> CZ square -> squares whose edges ring like struck wires."""
    a = ramp(U, 0.0, 0.45); b = ramp(U, 0.45, 1.0)
    d1 = np.where(U < 0.45, 0.5 * (0.008) ** a, 0.004 * 40.0 ** b)
    d2 = np.where(U < 0.45, 0.12 * (0.033) ** a, 0.004 * 25.0 ** b)
    W1 = 1.0 + 12.0 * b ** 1.1
    W2 = 1.0 + 20.0 * b ** 1.1
    T = to[None, :]
    h = np.mod(T, 0.5)
    first = T < 0.5
    d = np.where(first, d1, d2); W = np.where(first, W1, W2)
    base = np.where(first, 0.0, 0.5)
    fast = np.minimum(h / d, 1.0)
    w = np.cos(TAU * (base + 0.5 * fast * W))
    w = np.where(h >= d, np.where(first, -1.0, 1.0), w)
    return fin(down(w))


def cz_trapezoid_reso():
    """CZ resonance with a TRAPEZOID window: a cosine whose multiple R climbs 1 -> 110 inside a
    window that rises, holds and falls, the window edges sharpening from soft to a 4-sample
    razor. Axis: rounded pulse -> resonant 'filter' sweep -> whistling hard-gated burst."""
    R = 1.0 + 109.0 * u ** 1.4
    e = 0.45 * (0.002 / 0.45) ** (U ** 0.8)
    T = to[None, :]
    win = np.clip(np.minimum(T, 1.0 - T) / e, 0.0, 1.0)
    def build(r):
        return np.cos(TAU * r * to)
    c = xint(R, build, NOS)
    return fin(down(win * c + 0.35 * (win - 0.5)))


def cz_triangle_reso():
    """CZ resonance, TRIANGLE window, over-driven: the ramp-up and ramp-down halves each carry a
    cosine burst of multiple R (1 -> 44) and the second half runs at R*1.5 so the two halves
    disagree. Axis: soft triangle -> twin resonant chirps -> screaming split resonance."""
    R = 1.0 + 43.0 * u ** 1.25
    T = to
    win = 1.0 - np.abs(2.0 * T - 1.0)
    def build(r):
        a = np.cos(TAU * r * 2.0 * T)
        b = np.cos(TAU * np.round(r * 1.5) * 2.0 * T)
        return np.where(T < 0.5, a, b)
    c = xint(R, build, NOS)
    return fin(down(win[None, :] * c))


def leaning_humps():
    """LEANING HUMPS: CZ phase distortion applied H times per cycle (H 2 -> 16, crossfaded), each
    hump with its OWN kink, the kinks laid along a sine across the cycle and pushed to the
    edges (0.5 -> 0.002). Axis: two soft humps -> a row of humps leaning different ways ->
    a picket fence of saw-needles facing each other."""
    Hn = 2.0 + 14.0 * u ** 1.3
    amt = U ** 1.1
    out = np.zeros((F, NOS))
    lo = np.floor(Hn).astype(int); fr = Hn - lo
    for i in range(F):
        acc = 0.0
        for H, wgt in ((lo[i], 1 - fr[i]), (lo[i] + 1, fr[i])):
            seg = np.minimum((to * H).astype(int), H - 1)
            h = to * H - seg
            k = 0.5 + 0.498 * amt[i, 0] * np.sin(TAU * (seg + 0.5) / H + 0.7)
            ph = np.where(h < k, 0.5 * h / k, 0.5 + 0.5 * (h - k) / (1.0 - k))
            acc = acc + wgt * -np.cos(TAU * ph)
        out[i] = acc
    return fin(down(out))


def phase_winder():
    """Over-wound phase read through a TRIANGLE: phase W * t^g (W integer, crossfaded, 1 -> 30;
    g 1 -> 5), so the frequency CHIRPS inside every cycle and the chirp keeps steepening.
    Axis: triangle -> accelerating zigzag -> a rising laser chirp per cycle."""
    W = 1.0 + 29.0 * u ** 1.2
    g = 1.0 + 4.0 * U ** 1.1
    Tg = to[None, :] ** g
    lo = np.floor(W)[:, None]; fr = (W - np.floor(W))[:, None]
    w = (1 - fr) * tri(TAU * lo * Tg) + fr * tri(TAU * (lo + 1) * Tg)
    return fin(down(w))


def staircase_modulator():
    """FM (phase modulation) by a QUANTISED modulator: sin(t + I * round(Q sin 3t) / Q), the
    modulator's resolution falling 64 -> 2 levels while the index climbs 0 -> 12. The carrier
    jumps between pitch plateaus instead of gliding.
    Axis: sine -> gently stepped FM -> blocky pitch-jump chords -> a 3-state glitch carrier."""
    Q = 64.0 * (2.0 / 64.0) ** u
    I = 12.0 * U ** 1.2
    out = np.zeros((F, NOS))
    for i in range(F):
        m = np.round(Q[i] * np.sin(3 * po)) / Q[i]
        out[i] = np.sin(po + I[i, 0] * m)
    return fin(down(out))


def phase_staircase():
    """Phase quantised to Q steps with a SLANT inside each step: slant 1 = plain sine, 0 = a
    stair, negative = each step winds BACK (zigzag), -3 = over-wound chatter; Q 64 -> 5.
    Axis: sine -> stepped sine -> sawtooth-toothed stair -> chattering zigzag."""
    Q = np.round(64.0 * 2.0 ** (-3.6 * u)).astype(int)
    s = 1.0 - 4.0 * u ** 1.2
    out = np.zeros((F, NOS))
    for i in range(F):
        q = Q[i]
        x = to * q
        ph = (np.floor(x) + s[i] * np.mod(x, 1.0)) / q + (1.0 - s[i]) * 0.5 / q
        out[i] = np.sin(TAU * ph)
    return fin(down(out))


def bent_fm():
    """Phase distortion of an FM carrier: the carrier's phase is first bent by a CZ saw map (the
    kink tightening 0.5 -> 0.01), THEN frequency-modulated at ratio 5 — the PD squashes the
    sidebands into one half of the cycle. Axis: sine -> squashed FM bell -> one-sided shriek."""
    k = (0.5 * 10.0 ** (-1.7 * U))
    I = 14.0 * U ** 1.2
    T = to[None, :]
    ph = np.where(T < 1.0 - k, 0.5 * T / (1.0 - k), 0.5 + 0.5 * (T - 1.0 + k) / k)
    return fin(down(np.sin(TAU * ph + I * np.sin(5.0 * TAU * ph))))


# ═══════════════════════════════════════ CRUSH / RATE ═══════════════════════════════════════
def _fquant(x, mbits, emin):
    """float-style quantiser: mantissa mbits, exponent floor emin (values below 2^emin -> 0)."""
    ax = np.abs(x)
    e = np.floor(np.log2(np.maximum(ax, 1e-30)))
    e = np.maximum(e, emin)
    step = 2.0 ** (e - mbits)
    q = np.round(ax / step) * step
    q = np.where(ax < 2.0 ** emin * 0.5, 0.0, q)
    return np.sign(x) * q


def float_crush():
    """Floating-point crusher on an FM tone whose index rises 1.2 -> 4.5: mantissa bits 9 -> 0
    (fine steps survive near zero, the loud part goes coarse), then the exponent range collapses
    to 4 octaves: a pure power-of-two ladder. Axis: FM tone -> log-spaced steps -> an octave
    ladder of slabs."""
    mb = np.round(9.0 * (1.0 - ramp(u, 0.0, 0.6)) ** 1.2).astype(int)
    em = np.round(-16.0 + 12.0 * ramp(u, 0.55, 1.0) ** 0.9).astype(int)
    out = np.zeros((F, NOS))
    for i in range(F):
        I = 1.2 + 3.3 * u[i] ** 1.2
        src = np.sin(po + I * np.sin(3 * po + 0.4)) + 0.2 * np.sin(5 * po + 1.1)
        src = src / np.abs(src).max()
        out[i] = _fquant(src, mb[i], em[i])
    return fin(down(out))


def mu_law_grit():
    """Mu-law companding on a sine with a 3rd-harmonic FM kink, mu 0.5 -> 60, the codebook
    shrinking 5 -> 3 bits: the steps crowd around zero and turn to slabs at the peaks.
    Axis: kinked sine -> terraced sine -> hunched slabs."""
    src = np.sin(po + 0.8 * np.sin(3 * po))
    mu = 0.5 + 60.0 * U ** 1.5
    bits = 5.0 - 2.0 * U ** 1.2
    L = 2.0 ** bits / 2.0
    c = np.sign(src) * np.log1p(mu * np.abs(src)) / np.log1p(mu)
    cq = np.round(c * L) / L
    y = np.sign(cq) * (np.power(1.0 + mu, np.abs(cq)) - 1.0) / mu
    return fin(down(y))


def jitter_clock():
    """Sample-and-hold of a 2-op FM tone on a 48-tick clock whose tick times jitter more and more
    (seeded), so the blocks go uneven and the held values glitch. Axis: stepped FM -> stuttering
    blocks -> glitch skyline."""
    rng = np.random.default_rng(4801)
    base = np.arange(48) / 48.0
    jit = rng.uniform(-0.5, 0.5, 48) / 48.0
    src = np.sin(po + 3.0 * np.sin(2 * po))
    out = np.zeros((F, NOS))
    ticks_n = np.round(48 - 40 * u ** 1.3).astype(int)
    for i in range(F):
        n = max(ticks_n[i], 4)
        b = np.arange(n) / n
        j = np.random.default_rng(4801 + n).uniform(-0.5, 0.5, n) / n
        tk = np.sort(np.mod(b + 1.7 * u[i] ** 0.8 * j, 1.0))
        idx = np.searchsorted(tk, to, side='right') - 1
        held_t = tk[idx % n]
        s = np.sin(TAU * held_t + (3.0 + 9.0 * u[i]) * np.sin(2 * TAU * held_t))
        out[i] = s
    return fin(down(out))


def one_bit_stream():
    """1-bit delta-sigma (2nd order) of a sine + 2nd, computed at the table's own rate, heard
    through a low-pass that opens from harmonic 12 to 1023. Axis: a sine -> sine with a fizz of
    shaped bit-noise -> the raw barcode of a 1-bit converter."""
    x = 0.55 * np.sin(TAU * tn) + 0.2 * np.sin(2 * TAU * tn + 0.5)
    y = np.zeros(N)
    i1 = i2 = 0.0; fb = 0.0
    for _ in range(2):
        for n in range(N):
            i1 += x[n] - fb
            i2 += i1 - fb
            fb = 1.0 if i2 >= 0 else -1.0
            y[n] = fb
    cut = 12.0 * (1023.0 / 12.0) ** (u ** 0.9)
    fr = lowpass_h(np.tile(y, (F, 1)), cut, order=3)
    return fin(fr)


def pulse_dac():
    """Class-D PWM of a sine: a natural-sampled comparator against a triangle carrier whose count
    per cycle falls 96 -> 3, heard through an output filter that opens from harmonic 30 to the
    top in the first 40%. Axis: sine -> sine with its carrier whine rising -> a visible
    pulse-width ladder -> three fat mutant pulses."""
    M = 96.0 * (3.0 / 96.0) ** (ramp(u, 0.2, 1.0) ** 0.85)
    sig = 0.85 * np.sin(po) + 0.15 * np.sin(3 * po)
    def build(m):
        m = max(int(m), 1)
        return np.where(sig > tri(m * po - np.pi / 2), 1.0, -1.0)
    y = down(xint(M, build, NOS))
    cut = 30.0 * (1023.0 / 30.0) ** ramp(u, 0.0, 0.4)
    return fin(lowpass_h(y, cut, order=4))


def codec_blocks():
    """Block-transform codec starved of bits: the cycle is cut into B blocks (8, 16, 32, 64 — a
    designed step each quarter), each block DCT'd and quantised with a step growing 1e-3 -> 3;
    like a real codec it always sends each block's loudest term, so the end is one cosine per
    block. Source: an FM tone (5:1). Axis: clean FM -> ringing block seams -> blocky cosine rubble."""
    from scipy.fft import dct, idct
    src = np.sin(po + 2.2 * np.sin(5 * po + 0.3)) + 0.5 * np.sin(2 * po)
    src = down(src[None, :])[0]
    Bs = np.array([8, 16, 32, 64])
    B = Bs[np.minimum((u * 4).astype(int), 3)]
    step = 1e-3 * (3.0 / 1e-3) ** (u ** 0.8)
    out = np.zeros((F, N))
    for i in range(F):
        L = N // B[i]
        C = dct(src.reshape(B[i], L), type=2, norm='ortho', axis=1)
        sc = np.sqrt(L) * step[i]
        Cq = np.round(C / sc) * sc
        big = np.argmax(np.abs(C[:, 1:]), axis=1) + 1
        for j in range(B[i]):
            if Cq[j, big[j]] == 0:
                Cq[j, big[j]] = np.sign(C[j, big[j]]) * sc
        out[i] = idct(Cq, type=2, norm='ortho', axis=1).reshape(-1)
    return fin(out)


def wavelet_starve():
    """Haar wavelet compression: keep only the K biggest Haar coefficients of an FM tone, K
    2048 -> 3 on a fast log curve so the blocks arrive early. Axis: smooth tone -> tone rebuilt
    from ever bigger blocks -> a handful of Haar bricks."""
    src = np.sin(TAU * tn + 2.2 * np.sin(2 * TAU * tn)) + 0.3 * np.sin(5 * TAU * tn)
    def haar(x):
        c = x.copy(); n = len(c); out = []
        while n > 1:
            a = (c[0:n:2] + c[1:n:2]) / np.sqrt(2); d = (c[0:n:2] - c[1:n:2]) / np.sqrt(2)
            out.insert(0, d); c = a; n //= 2
        return np.concatenate([c] + out)
    def ihaar(h):
        c = h[:1].copy(); p = 1
        while p < len(h):
            d = h[p:2 * p]; a = c
            c = np.empty(2 * p); c[0::2] = (a + d) / np.sqrt(2); c[1::2] = (a - d) / np.sqrt(2)
            p *= 2
        return c
    H = haar(src)
    order = np.argsort(-np.abs(H))
    K = np.round(2048.0 * (3.0 / 2048.0) ** (u ** 0.45)).astype(int)
    out = np.zeros((F, N))
    for i in range(F):
        h = np.zeros_like(H); keep = order[:K[i]]; h[keep] = H[keep]
        out[i] = ihaar(h)
    return fin(out)


def uneven_decimator():
    """A decimator whose clock is set by the signal's own SLOPE: where the wave moves fast the
    clock stalls (long holds), on the slow crowns it runs free — the opposite of a plain
    sample-and-hold. Stall depth 0 -> 600. Axis: FM tone -> stepped flanks under smooth crowns
    -> steep flanks become giant stairs."""
    src = np.sin(po + 1.2 * np.sin(2 * po + 0.7)) + 0.3 * np.sin(3 * po)
    src = src / np.abs(src).max()
    ds = np.abs(np.gradient(src)); ds = ds / ds.max()
    out = np.zeros((F, NOS))
    for i in range(F):
        H = 600.0 * u[i] ** 2.0
        rate = 1.0 / (1.0 + H * ds ** 2)
        clk = np.cumsum(rate) / OS
        k = np.floor(clk)
        first = np.r_[True, k[1:] != k[:-1]]
        idx = np.maximum.accumulate(np.where(first, np.arange(NOS), 0))
        out[i] = src[idx]
    return fin(down(out))


def sub_bitcrush():
    """SUB: a sine fundamental kept pristine while a second layer (octave + twelfth) is crushed
    8 bits -> 1 bit and sample-held from 2048 to 16 steps, rising to meet the sub.
    Axis: clean sub -> sub with a fizzy digital collar -> sub under a 1-bit square hash."""
    src = np.sin(2 * po + 0.4) + 0.6 * np.sin(3 * po + 1.3)
    src = src / np.abs(src).max()
    L = 2.0 ** (8.0 - 7.0 * u ** 0.9)
    steps = np.round(2048.0 * (16.0 / 2048.0) ** (u ** 0.9)).astype(int)
    out = np.zeros((F, NOS))
    for i in range(F):
        idx = (np.floor(to * steps[i]) / steps[i] * NOS).astype(int)
        c = np.round(src[idx] * L[i]) / L[i]
        out[i] = np.sin(po) + (0.15 + 0.8 * u[i]) * c
    return fin(down(out))


# ═════════════════════════════════════ FOLD / OVERFLOW ══════════════════════════════════════
def hundred_folds():
    """A sine into an ASYMMETRIC triangle folder (upper rail 1, lower rail 0.35) at gain 1 ->
    110. Past sanity the fold count goes into the hundreds while the rails keep a lopsided
    envelope. Axis: sine -> nested Ms -> a buzzing lopsided zigzag carpet."""
    g = 1.0 + 109.0 * U ** 2.0
    x = g * np.sin(po)[None, :]
    lo_r = 0.35
    # fold between -lo_r and +1: map to period 2*(1+lo_r)
    span = 1.0 + lo_r
    y = np.mod(x + lo_r, 2 * span)
    y = np.where(y > span, 2 * span - y, y) - lo_r
    return fin(down(y))


def trapezoid_folder():
    """A LOPSIDED, flat-topped trapezoid (fast rise, slow fall, clipped hard) folded at gain
    1 -> 40: its flats stay flat while its slopes turn into ever-faster zigzags.
    Axis: trapezoid -> mesas with serrated ramps -> plateaus split by fizzing zigzag cliffs."""
    T = to
    r_ = np.where(T < 0.25, T / 0.25, 1.0 - 2.0 * (T - 0.25) / 0.75)
    trap = np.clip(4.0 * r_, -1.0, 1.0)
    g = 1.0 + 39.0 * U ** 1.6
    return fin(down(tfold(g * trap[None, :])))


def overflow_tear():
    """Two's-complement overflow: an FM-kinked sine times gain 1.12 -> 12, wrapped into [-1, 1)
    like an integer register with no headroom. The crowns tear off from frame 0 and reappear
    at the opposite rail. Axis: sine with nicked crowns -> torn sine -> a hail of broken ramps."""
    g = 1.12 + 11.0 * U ** 1.5
    src = np.sin(po + 1.0 * np.sin(3 * po)); src = src / np.abs(src).max()
    return fin(down(wrap(g * src[None, :])))


def fold_to_wrap():
    """The same gain sweep (1 -> 9) through a folder that turns into a WRAPPER: the reflection
    at each rail becomes a jump by degrees (blend). Axis: folded sine zigzags -> half-torn ->
    razor overflow ramps."""
    g = 1.0 + 8.0 * U ** 1.2
    x = g * (np.sin(po) + 0.25 * np.sin(3 * po))[None, :]
    a = ramp(U, 0.25, 0.9)
    return fin(down((1.0 - a) * tfold(x) + a * wrap(x)))


def nested_sines():
    """x -> sin(g x + phi_k) applied five times to a sine (a composed digital shaper with a
    different bias at each layer, so even harmonics bloom), g 1 -> 2.6, sized to stay inside
    the band. Axis: sine -> lopsided sine -> fractal ripple -> frothing nested folds."""
    g = 1.0 + 1.6 * U ** 1.1
    y = np.sin(po)[None, :] * np.ones((F, 1))
    phis = [0.0, 0.9, -0.4, 1.7, 0.3]
    for k, phi in enumerate(phis):
        y = np.sin(g * np.pi * 0.5 * y * (1.0 + 0.45 * k * U) + phi * U)
    return fin(down(y))


def sub_fold():
    """SUB: a sine fundamental kept at full level under a folded copy of its octave; fold gain
    1 -> 40 and a DC bias walk make the top snarl while the bottom holds.
    Axis: sub+octave -> sub with a folded snarl -> sub under a fizzing fold storm."""
    g = 1.0 + 39.0 * U ** 1.7
    bias = 0.8 * U ** 1.3
    top = tfold(g * np.sin(2 * po + 0.3)[None, :] + bias)
    top = top - top.mean(axis=1, keepdims=True)
    return fin(down(np.sin(po)[None, :] + (0.25 + 0.45 * U) * top))


# ═════════════════════════════════════════ BITWISE ══════════════════════════════════════════
def _q8(x):
    return np.clip(np.round((x + 1.0) * 127.5), 0, 255).astype(np.int64)


def _from8(v):
    return v.astype(float) / 127.5 - 1.0


def logic_gate_tour():
    """Two 8-bit integer waves (sine at 1, sine at a ratio climbing 2 -> 7) combined by AND,
    then OR, then XOR, then NAND of AND/XOR — a new logic stage every quarter, blended.
    Axis: a blocky sine-ish -> AND glyphs -> OR castles -> XOR hash -> inverted rubble."""
    a = _q8(np.sin(TAU * tn))
    ops = [lambda x, y: x & y, lambda x, y: x | y, lambda x, y: x ^ y,
           lambda x, y: 255 - ((x & y) ^ (x | (y >> 1)))]
    R = 2.0 + 5.0 * u
    out = np.zeros((F, N))
    for i in range(F):
        r = int(np.floor(R[i])); fr = R[i] - r
        b1 = _q8(np.sin(TAU * r * tn + 0.5)); b2 = _q8(np.sin(TAU * (r + 1) * tn + 0.5))
        s = u[i] * 3.0
        j = min(int(s), 2); w = s - j
        o1 = (1 - fr) * _from8(ops[j](a, b1)) + fr * _from8(ops[j](a, b2))
        o2 = (1 - fr) * _from8(ops[j + 1](a, b1)) + fr * _from8(ops[j + 1](a, b2))
        m = (1 - w) * o1 + w * o2
        first = 1.0 - ramp(u[i], 0.0, 0.08)
        out[i] = (1 - first) * m + first * _from8(a)
    return fin(out)


def bit_reversed_read():
    """A sine+3rd read through a partially BIT-REVERSED index: the lowest k of the 11 address
    bits are reversed, k 0 -> 11 (crossfaded). Axis: smooth wave -> fine grain scramble ->
    blocky fractal shards (the FFT's own shuffle)."""
    src = np.sin(TAU * tn) + 0.4 * np.sin(3 * TAU * tn + 0.8)
    def rev(k):
        k = int(np.clip(k, 0, 11))
        if k == 0:
            return src
        low = kn & ((1 << k) - 1)
        r = np.zeros_like(kn)
        for b in range(k):
            r |= ((low >> b) & 1) << (k - 1 - b)
        return src[(kn & ~((1 << k) - 1)) | r]
    K = 11.0 * u ** 0.9
    return fin(xint(K, rev, N))


def block_reverser():
    """XOR on the address: y[n] = x[n XOR (2^k - 1)], which reverses the wave inside every block
    of 2^k samples; k walks 0 -> 10 while the source FM deepens. Axis: FM tone -> backwards
    grains -> saw-toothed block mirrors."""
    I = 1.0 + 4.0 * u
    def build(k):
        k = int(np.clip(k, 0, 10))
        return kn ^ ((1 << k) - 1)
    K = 10.0 * (1.0 - (1.0 - u) ** 1.3)
    out = np.zeros((F, N))
    for i in range(F):
        src = np.sin(TAU * tn + I[i] * np.sin(2 * TAU * tn + 0.4))
        k = int(np.floor(K[i])); fr = K[i] - k
        a = src[build(k)]; b = src[build(k + 1)]
        out[i] = (1 - fr) * a + fr * b
    return fin(out)


def counter_mask():
    """An 11-bit counter (one ramp per cycle) with address bits masked off in a scrambled order
    along a Gray path: masking a high bit doubles the ramp, masking a low bit makes stairs.
    Axis: ramp -> stair ramps -> octave-folded sawtooth rubble. Hard, bright, blocky."""
    order = [0, 10, 3, 9, 1, 8, 5, 7, 2, 6, 4]
    masks = [2047]
    m = 2047
    for b in order:
        m &= ~(1 << b); masks.append(m if m else 1)
    def build(j):
        j = int(np.clip(j, 0, len(masks) - 1))
        v = kn & masks[j]
        v = v.astype(float)
        return v - v.mean()
    J = (len(masks) - 2) * u ** 0.9
    fr = xint(J, build, N)
    return fin(fr / np.maximum(np.abs(fr).max(axis=1, keepdims=True), 1e-9))


def ring_overflow():
    """An OVERFLOWING ring modulator: a sine times (0.3 + g * 2.5 sin 5t), the product scaled
    1 -> 3 and wrapped into [-1, 1) like a fixed-point multiplier with no headroom. Clean sparse
    ring-mod partials until the product crosses full scale, then it tears.
    Axis: ring-mod bell -> torn ring-mod -> overflow shrapnel riding the ring pattern."""
    out = np.zeros((F, NOS))
    for i in range(F):
        g = 0.1 + 0.9 * u[i] ** 1.2
        p = np.sin(po) * (0.3 + g * 2.5 * np.sin(5 * po + 0.3)) * (1.0 + 2.0 * u[i])
        out[i] = wrap(p)
    return fin(down(out))


def shift_register_buzz():
    """A 15-bit linear-feedback shift register in its short 'metallic' mode (the chip noise
    channel's tonal setting), clocked into the cycle with a divider falling 256 -> 4 samples per
    bit. Axis: an 8-step pulse riff -> a ringing short-loop buzz -> a dense metallic barcode."""
    s = 0x5A3C; seq = []
    for _ in range(4096 + 37):
        b = (s ^ (s >> 6)) & 1
        s = (s >> 1) | (b << 14)
        seq.append(1.0 if s & 1 else -1.0)
    seq = np.array(seq[37:])
    D = 256.0 * (4.0 / 256.0) ** (u ** 0.85)
    out = np.zeros((F, N))
    for i in range(F):
        out[i] = seq[(kn / D[i]).astype(int) % 4096]
    return fin(out)


def bit_plane_dive():
    """One BIT PLANE of a 12-bit FM tone as a +-1 wave, descending from bit 1 (just under the
    sign) to the least significant bit, neighbours blended, over a fading 6-bit copy of the
    tone. Axis: stepped tone with a pulse rider -> octave pulse towers -> chattering bit hash."""
    src = np.sin(TAU * tn + 1.3 * np.sin(2 * TAU * tn)) + 0.3 * np.sin(3 * TAU * tn + 0.7)
    src = src / np.abs(src).max()
    v = np.round((src + 1.0) * 2047.5).astype(np.int64)
    def plane(k):
        k = int(np.clip(k, 1, 11))
        return (((v >> (11 - k)) & 1) * 2.0 - 1.0)
    K = 1.0 + 10.0 * u ** 1.15
    fr = xint(K, plane, N)
    q6 = np.round(src * 32) / 32
    return fin(0.5 * fr + 1.2 * q6[None, :] * (1 - U) ** 1.5)


def byte_rotate():
    """An 8-bit sine whose bits are ROTATED left by r (r 0 -> 7, 18 frames per rotation, blended):
    each rotation throws the value's high bits to the bottom. Axis: sine -> terraced folds ->
    unrecognisable bit glyphs."""
    a = _q8(0.98 * np.sin(TAU * tn) + 0.02)
    def rot(r):
        r = int(r) % 8
        v = ((a << r) | (a >> (8 - r))) & 255
        return _from8(v)
    R = 7.0 * u ** 1.0
    return fin(xint(R, rot, N))


def xor_sub():
    """SUB: a sine fundamental under the XOR of two 8-bit waves (octave sine and a ramp whose
    ratio climbs 3 -> 12, crossfaded), the XOR layer rising 0.05 -> 0.8. Axis: pure sub -> sub
    with a buzzing digital lattice -> sub buried in XOR shrapnel (the fundamental never leaves)."""
    a = _q8(np.sin(2 * TAU * tn + 0.2))
    def build(r):
        x = _from8(a ^ _q8(saw(int(r) * TAU * tn)))
        return x - x.mean()
    R = 3.0 + 9.0 * u ** 1.3
    X = xint(R, build, N)
    return fin(np.sin(TAU * tn)[None, :] + (0.05 + 0.75 * U ** 1.1) * X)


def broken_carry():
    """An adder with a BROKEN CARRY CHAIN: a 12-bit sine plus a 12-bit triangle at 5x, summed
    with carries allowed only up to bit c; c falls 12 -> 0, so the sum degrades into their XOR.
    Axis: a clean sine+triangle sum -> glitch cracks where carries die -> pure XOR lattice."""
    A = np.round((np.sin(TAU * tn + 0.3) + 1) * 1023.5).astype(np.int64)
    B = np.round((tri(5 * TAU * tn) + 1) * 1023.5).astype(np.int64)
    def add(c):
        c = int(np.clip(c, 0, 12))
        if c >= 12:
            v = A + B
        else:
            mask = (1 << c) - 1
            low = (A & mask) + (B & mask)
            high = (A & ~mask) ^ (B & ~mask)
            v = (low ^ high) & 4095
        v = v.astype(float)
        return v - v.mean()
    Cv = 12.0 * (1.0 - u) ** 0.9
    fr = xint(Cv, add, N)
    return fin(fr / np.maximum(np.abs(fr).max(axis=1, keepdims=True), 1e-9))


def hamming_weight():
    """HAMMING WEIGHT: the number of 1-bits in the b-bit integer of an FM tone (ratio 3, index
    2.5; b 2 -> 11, crossfaded) replaces the tone, which fades out: popcount turns smooth motion
    into a binary ruler. Rendered to harmonic 700 so the ruler keeps a soft top edge.
    Axis: stepped tone -> ruler shapes -> a fractal popcount skyline."""
    src = np.sin(TAU * tn + 2.5 * np.sin(3 * TAU * tn))
    src = src / np.abs(src).max()
    def pc(b):
        b = int(np.clip(b, 1, 11))
        v = np.round((src + 1) * 0.5 * ((1 << b) - 1)).astype(np.int64)
        c = np.zeros_like(v)
        for k in range(b):
            c += (v >> k) & 1
        c = c.astype(float)
        return (c - c.mean()) / max(b, 1)
    fr = xint(2.0 + 9.0 * u ** 0.9, pc, N)
    fr = fr / np.maximum(np.abs(fr).max(axis=1, keepdims=True), 1e-9)
    return fin(down(fr + 0.8 * src[None, :] * (1 - U) ** 2, nmax=700))


def sub_reso_snarl():
    """SUB: a sine fundamental held steady under a CZ resonant top (saw window x cosine at
    multiple R 2 -> 70) that swells from a whisper to a snarl as R climbs.
    Axis: sub with a soft buzz -> sub + vowel-ish reso sweep -> sub under a screaming reso."""
    R = 2.0 + 68.0 * u ** 1.3
    win = (1.0 - to)
    def build(r):
        return win * np.cos(TAU * r * to)
    top = xint(R, build, NOS)
    top = top - top.mean(axis=1, keepdims=True)
    return fin(down(np.sin(po)[None, :] + (0.2 + 0.6 * U) * top))


# ═════════════════════════════════════ STEPPED / GLITCH ═════════════════════════════════════
def block_shuffle():
    """The cycle of a 2-op FM tone cut into 2^k blocks and permuted by a seeded shuffle, k 0 -> 7
    (1 -> 128 blocks), neighbours crossfaded. Axis: intact FM tone -> swapped halves ->
    cut-up stutter -> granular glitch confetti."""
    src = np.sin(po + 2.5 * np.sin(2 * po + 0.3)) + 0.3 * np.sin(3 * po)
    def build(k):
        k = int(np.clip(k, 0, 7)); nb = 1 << k
        perm = np.random.default_rng(700 + k).permutation(nb)
        return src.reshape(nb, -1)[perm].reshape(-1)
    K = 7.0 * u ** 0.9
    return fin(down(xint(K, build, NOS)))


def buffer_stutter():
    """A buffer-repeat effect inside one cycle: the first 1/n of an FM tone (n 1 -> 20, index
    rising 2 -> 9) is looped n times, each repeat a hard retrigger with a short fade, so every
    loop point clicks. Axis: the tone -> retriggered halves -> a machine-gun of clicking grains."""
    Nn = 1.0 + 19.0 * u ** 1.2
    out = np.zeros((F, NOS))
    lo = np.floor(Nn).astype(int); fr = Nn - lo
    for i in range(F):
        I = 2.0 + 7.0 * u[i]
        acc = 0.0
        for n, w in ((lo[i], 1 - fr[i]), (lo[i] + 1, fr[i])):
            loc = np.mod(to * n, 1.0) / n
            env = 1.0 - 0.5 * np.mod(to * n, 1.0) ** 2 * min(1.0, (n - 1) / 3.0)
            acc = acc + w * np.sin(TAU * loc * 3 + I * np.sin(TAU * loc * 2) + 0.9) * env
        out[i] = acc
    return fin(down(out))


def staircase_chirp():
    """A pitch STAIRCASE inside each cycle: the wave plays harmonic 1, 2, 3 ... S in equal time
    slices (S 1 -> 24), each slice an exact number of cycles so the stair is seamless.
    Axis: sine -> an arpeggio squeezed into one cycle -> a staircase chirp of 24 rungs."""
    Sv = 1.0 + 23.0 * u ** 1.1
    def build(S):
        S = max(int(S), 1)
        seg = np.minimum((to * S).astype(int), S - 1)
        loc = to * S - seg
        h = seg + 1
        # h cycles in a slice of length 1/S -> h*S... use h cycles per slice
        return np.sin(TAU * h * loc)
    return fin(down(xint(Sv, build, NOS)))


def alias_moire():
    """ALIASING on purpose: a sawtooth chirp saw(2pi K t^2) written straight onto the 2048-sample
    grid with no band-limiting, K 1 -> 8192 (integer, crossfaded). Once the chirp outruns the
    grid it folds back into moire fans of phantom ramps — the sound of a sampler with no
    filter. Axis: one bent ramp -> an accelerating ramp chirp -> folding moire interference."""
    K = 2.0 ** (13.0 * u)
    lo = np.floor(K).astype(int); fr = K - lo
    t2 = tn * tn
    out = np.zeros((F, N))
    for i in range(F):
        out[i] = (1 - fr[i]) * saw(TAU * lo[i] * t2) + fr[i] * saw(TAU * (lo[i] + 1) * t2)
    return fin(out)


def wave_ram_rot():
    """Chip wave-RAM: 64 samples x 3 bits holding a sine. Bit rot accumulates (seeded flips, up
    to 150 of them), so the stepped sine is gradually rewritten by garbage.
    Axis: a stepped chip sine -> wonky chip lead -> a corrupted 3-bit skyline."""
    n = 64
    ram = np.clip(np.round((np.sin(TAU * (np.arange(n) + 0.5) / n) + 1) * 3.5), 0, 7).astype(int)
    rng = np.random.default_rng(6464)
    flips = [(rng.integers(n), rng.integers(3)) for _ in range(150)]
    out = np.zeros((F, NOS))
    for i in range(F):
        r = ram.copy()
        for (p, b) in flips[:int(150 * u[i] ** 1.3)]:
            r[p] ^= (1 << b)
        out[i] = r[(to * n).astype(int)] / 3.5 - 1.0
    return fin(down(out))


def glitch_selector():
    """Eight 16-frame zones, each a different digital machine at rising violence (PD saw, FM,
    crushed FM, folded FM, XOR'd FM, stuttered, bit-plane, overflowed FM), each zone moving inside
    and JUMPING at its edge. Axis: a designed glitch tour — the frame knob as a selector."""
    out = np.zeros((F, NOS))
    p = po
    for i in range(F):
        z = i // 16; w = (i % 16) / 15.0
        if z == 0:
            k = 0.5 - 0.45 * w
            ph = np.where(to < 1 - k, 0.5 * to / (1 - k), 0.5 + 0.5 * (to - 1 + k) / k)
            y = -np.cos(TAU * ph)
        elif z == 1:
            y = np.sin(p + (2 + 6 * w) * np.sin(3 * p))
        elif z == 2:
            L = 2 ** (5 - 3 * w)
            y = np.round(np.sin(p + 7 * np.sin(2 * p)) * L) / L
        elif z == 3:
            y = tfold((2 + 8 * w) * np.sin(p + 3 * np.sin(5 * p)))
        elif z == 4:
            a = _q8(np.sin(p)); b = _q8(np.sin((4 + int(6 * w)) * p + 0.3))
            y = _from8(a ^ b)
        elif z == 5:
            n = 2 + int(10 * w)
            loc = np.mod(to * n, 1.0) / n
            y = np.sin(TAU * loc * 7 + 4 * np.sin(TAU * loc * 3))
        elif z == 6:
            v = np.round((np.sin(p + 2 * np.sin(3 * p)) + 1) * 2047).astype(np.int64)
            y = ((v >> (6 - int(5 * w))) & 1) * 2.0 - 1.0
        else:
            y = wrap((2 + 6 * w) * np.sin(p + (3 + 10 * w) * np.sin(7 * p)))
        out[i] = y
    return fin(down(out))


# ════════════════════════════════════════ OTHERS ═══════════════════════════════════════════
def _walsh(k, n=N):
    """Walsh function of sequency-ordered index via Gray code / bit parity (natively 2048)."""
    g = k ^ (k >> 1)
    br = 0
    for b in range(11):
        if (g >> b) & 1:
            br |= 1 << (10 - b)
    return 1.0 - 2.0 * (np.array([bin(x & br).count("1") & 1 for x in range(n)], dtype=float))


def walsh_glyphs():
    """Additive synthesis with WALSH functions (square-wave 'harmonics') instead of sines: a
    seeded sequency spectrum whose narrow band slides from sequency 1-6 up to ~300 while it
    widens, squashed by a tanh so it stays blocky and loud.
    Axis: a blocky square-ish wave -> castle glyphs -> high-sequency block hash."""
    rng = np.random.default_rng(2222)
    ks = np.unique(np.round(np.geomspace(1, 1500, 160)).astype(int))
    basis = np.array([_walsh(int(k)) for k in ks])
    amp = rng.uniform(0.3, 1.0, len(ks)) * rng.choice([-1, 1], len(ks))
    lk = np.log(ks)
    c = np.log(1.5) + (np.log(300) - np.log(1.5)) * u ** 1.1
    wd = 0.35 + 0.5 * u
    wgt = np.exp(-((lk[None, :] - c[:, None]) / wd[:, None]) ** 2) * amp[None, :]
    wgt[:, 0] += 2.0 * (1 - u) ** 2
    y = wgt @ basis
    y = y / np.abs(y).max(axis=1, keepdims=True)
    return fin(np.tanh(3.0 * y))


def corrupt_rom():
    """A waveshaper LUT (a 16-entry 'ROM' interpolated linearly) that starts as the identity and
    is corrupted by seeded offsets growing 0 -> 2x, driven by a sine at gain 1 -> 3.
    Axis: sine -> kinked sine -> a sine read through a ROM full of garbage."""
    rng = np.random.default_rng(1616)
    garbage = rng.uniform(-1, 1, 33)
    xs = np.linspace(-1, 1, 33)
    src = np.sin(po)
    out = np.zeros((F, NOS))
    for i in range(F):
        lut = xs + 2.0 * u[i] ** 1.2 * garbage
        g = 1.0 + 2.0 * u[i] ** 1.5
        x = wrap(g * src * 0.999)
        out[i] = np.interp(x, xs, lut)
    return fin(down(out))


def polarity_chopper():
    """A sine multiplied by a SQUARE wave whose integer ratio climbs 1 -> 64 (crossfaded): digital
    polarity chopping. The spectrum splits into mirrored combs.
    Axis: rectified-ish sine -> chopped castellations -> a fine chopped comb shimmer."""
    R = 64.0 ** u
    def build(r):
        r = max(int(r), 1)
        return np.sin(po + 0.3) * sq(r * po + 0.2) + 0.2 * sq(po)
    return fin(down(xint(R, build, NOS)))


def modulo_garden():
    """y = (A * sine) mod (B), with the modulus B shrinking 2.2 -> 0.12 and a slow A growth: the
    wave is chopped into terraces of sawteeth whose count explodes. Axis: sine -> wrapped
    crowns -> dense saw-tooth gardens riding a sine outline."""
    A = 1.0 + 1.5 * U
    B = 2.2 * (0.12 / 2.2) ** (U ** 0.9)
    x = A * (np.sin(po) + 0.2 * np.sin(2 * po))[None, :]
    y = np.mod(x, B) - B / 2 + 0.6 * np.sin(po)[None, :] * (1 - U) ** 0.5
    return fin(down(y))


TABLES = [
    ("BRICK FM",             brick_fm),
    ("SQUARE ON SQUARE",     square_on_square),
    ("RATIO STAIRCASE",      ratio_staircase),
    ("CHIP HALF SINE",       chip_half_sine),
    ("CHIP QUARTER PULSE",   chip_quarter_pulse),
    ("STEPPED PITCH FM",     stepped_pitch_fm),
    ("EXPO TRIANGLE CHIRP",   expo_fm_chirp),
    ("CROSS FEEDBACK PAIR",  cross_feedback_pair),
    ("SIDEBAND AVALANCHE",   sideband_avalanche),
    ("SUB UNDER FM",         sub_under_fm),
    ("FEEDBACK BRICK",       feedback_brick),
    ("FOUR OP CASCADE",      four_op_cascade),
    ("TRIANGLE SAW RAKE",    triangle_saw_rake),
    ("SQUARE BITE",       cz_square_bite),
    ("RESONANT GATE",    cz_trapezoid_reso),
    ("CZ TRIANGLE RESO",     cz_triangle_reso),
    ("LEANING HUMPS",         leaning_humps),
    ("PHASE WINDER",         phase_winder),
    ("PLATEAU FM",   staircase_modulator),
    ("PHASE STAIRCASE",      phase_staircase),
    ("BENT FM",              bent_fm),
    ("FLOAT CRUSH",          float_crush),
    ("MU LAW GRIT",          mu_law_grit),
    ("JITTER CLOCK",         jitter_clock),
    ("ONE BIT STREAM",       one_bit_stream),
    ("PWM CONVERTER",            pulse_dac),
    ("CODEC BLOCKS",         codec_blocks),
    ("WAVELET STARVE",       wavelet_starve),
    ("UNEVEN DECIMATOR",     uneven_decimator),
    ("SUB BITCRUSH",         sub_bitcrush),
    ("HUNDRED FOLDS",        hundred_folds),
    ("TRAPEZOID FOLDER",     trapezoid_folder),
    ("OVERFLOW TEAR",        overflow_tear),
    ("FOLD TO WRAP",         fold_to_wrap),
    ("NESTED SINES",         nested_sines),
    ("SUB FOLD",             sub_fold),
    ("LOGIC GATE TOUR",      logic_gate_tour),
    ("BIT REVERSED READ",    bit_reversed_read),
    ("BLOCK REVERSER",       block_reverser),
    ("COUNTER MASK",         counter_mask),
    ("RING OVERFLOW",        ring_overflow),
    ("SHIFT REGISTER BUZZ",  shift_register_buzz),
    ("BIT PLANE DIVE",       bit_plane_dive),
    ("BYTE ROTATE",          byte_rotate),
    ("XOR SUB",              xor_sub),
    ("BLOCK SHUFFLE",        block_shuffle),
    ("BUFFER STUTTER",       buffer_stutter),
    ("STAIRCASE CHIRP",      staircase_chirp),
    ("WAVE RAM ROT",         wave_ram_rot),
    ("GLITCH SELECTOR",      glitch_selector),
    ("WALSH GLYPHS",         walsh_glyphs),
    ("CORRUPTED SHAPER",          corrupt_rom),
    ("POLARITY CHOPPER",     polarity_chopper),
    ("MODULO GARDEN",        modulo_garden),
    ("SQUARE MULTIPLIER",     square_multiplier),
    ("BROKEN CARRY",          broken_carry),
    ("HAMMING WEIGHT",        hamming_weight),
    ("SUB RESO SNARL",        sub_reso_snarl),
    ("ALIAS MOIRE",           alias_moire),
]

CATEGORY = {ident: "Digital" for ident, _ in TABLES}
