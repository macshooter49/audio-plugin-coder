"""
gen2_processed.py — PROCESSED: the Virtual Riot idea built our own way (fb638, the Terrain 500).

ONE process per table, its AMOUNT is the frame axis, shipped as SAW and SQUARE twins (sometimes sine or
triangle), and the name says the process. Frame 0 is the plain source (or the process barely engaged),
the table travels, the last frame is the process at "too much".

Three construction routes, all exactly periodic (every frame loops by construction):
  SPEC   linear processes (resonant filters, combs, flangers, phasers, formants, phase copies) applied in
         the FREQUENCY domain: harmonic k of the source times the filter's complex response H(k). That IS
         the periodic steady state of the filter (what running it over the repeated cycle converges to),
         with H's phase kept, so a bandpass rings as a decaying burst after each edge inside the cycle.
  TIME   nonlinear processes (drive, fold, crush, sample-rate reduction, FM, noise) at 8x (16384 samples
         per cycle) on a band-limited source, then band-limited to 1000 harmonics.
  FX     Terrain's OWN filters (Source/TerrainFilters.h) rendered by fx_probe.cpp at the period of one
         frame (f0 = 48000/2048 Hz) to periodic steady state. Only Terrain can ship these.

LOUDNESS GUARD (limit_crest): any frame whose peak/RMS exceeds 3.5 (a spike, a narrow ringing burst) goes
through the gentlest tanh that brings it to 3.5, so peak normalisation cannot turn it into a 15 dB volume hole;
frames under 3.5 are untouched.

Sources are COHERENT-phase band-limited shapes (not wtlib.PHASE): the process has to act on a real saw
edge for its ringing, chirps and folds to exist. The saw's riser sits at t = 0.5 and the square's edges at
t = 0.25 / 0.75, so the loop seam is never on an edge.

FX dumps: $TERRAIN_WT_FXDUMP, else <wt500>/fxdump. Rebuild them (about a minute):
    c++ -std=c++17 -O2 -I ../../Tests -I ../../Tests/shim -I ../../Source fx_probe.cpp \\
        -framework Accelerate -o /tmp/fx_probe
    python3 gen2_processed.py --jobs | /tmp/fx_probe <wt500>/fxdump
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import wtlib

N, F, NH = wtlib.SIZE, wtlib.FRAMES, wtlib.NH
KMAX = 1000                                   # every table is band-limited to 1000 harmonics
OS = 8                                        # oversampling for TIME builds
M8 = N * OS
KK = np.arange(1, NH + 1, dtype=float)[None, :]   # (1, NH) harmonic numbers
U = np.linspace(0.0, 1.0, F)[:, None]             # (F, 1) frame position 0..1
T8 = (np.arange(M8) / M8)[None, :]                # (1, M8) one cycle at 8x
_WT500 = os.environ.get("TERRAIN_WT500") or \
    "/private/tmp/claude-501/-Users-macshooter/521ae994-b058-4e64-9804-78f1254cea68/scratchpad/wt500"
FXDUMP = os.environ.get("TERRAIN_WT_FXDUMP") or os.path.join(_WT500, "fxdump")


# ══ building blocks ═════════════════════════════════════════════════════════════════════════════
def lin(a, b, c=1.0):
    return a + (b - a) * U ** c


def geo(a, b, c=1.0):
    return a * (b / a) ** (U ** c)


def src(kind, kmax=KMAX):
    """Complex harmonic amplitudes X[k-1]: x(t) = sum_k Re(X_k e^{i 2 pi k t})."""
    ki = np.arange(1, NH + 1)
    odd = ki % 2 == 1
    alt = np.where(((ki - 1) // 2) % 2 == 0, 1.0, -1.0)
    if kind == 'saw':
        X = -1j * np.where(odd, 1.0, -1.0) / ki
    elif kind == 'sqr':
        X = np.where(odd, alt / ki, 0.0).astype(complex)
    elif kind == 'tri':
        X = -1j * np.where(odd, alt / ki ** 2, 0.0)
    elif kind == 'sine':
        X = np.zeros(NH, complex); X[0] = -1j
    elif kind.startswith('pul'):   # 'pulNN': NN % pulse centred on t = 0.5 (edges at 0.5 +- NN/200)
        w = int(kind[3:]) / 100.0
        X = (np.where(ki % 2 == 0, 1.0, -1.0) * np.sin(np.pi * ki * w) / ki).astype(complex)
    else:
        raise ValueError(kind)
    X = X.astype(complex)
    X[kmax:] = 0
    return X[None, :]


def synth(X, M=N):
    """(F, NH) complex -> (F, M) real cycles, amplitude-true."""
    X = np.atleast_2d(X)
    S = np.zeros((X.shape[0], M // 2 + 1), complex)
    nk = min(X.shape[1], M // 2 - 1)
    S[:, 1:nk + 1] = X[:, :nk]
    return np.fft.irfft(S, n=M, axis=1) * (M / 2)


def analyse(x, kmax=KMAX):
    """(F, M) real -> (F, NH) complex, band-limited to kmax, DC dropped."""
    M = x.shape[1]
    S = np.fft.rfft(x, axis=1) / (M / 2)
    X = np.zeros((x.shape[0], NH), complex)
    X[:, :kmax] = S[:, 1:kmax + 1]
    return X


CREST_MAX = 3.5      # per-frame peak/RMS ceiling (the loudness guard, see limit_crest)


def limit_crest(fr, cmax=CREST_MAX):
    """LOUDNESS GUARD. Every frame is peak-normalised, so a frame that is one spike plus a ringing tail (a
    highpassed riser, a narrow bandpass burst, a folded edge) plays 10-20 dB quieter than its neighbours and
    the table 'holes' as it is scanned. Frames whose crest factor exceeds cmax go through the gentlest tanh
    that brings them down to it (per-frame gain found by bisection, run at 4x and band-limited back), so the
    spike thickens and the frame keeps its level. Frames already under cmax are untouched, bit for bit."""
    fr = np.array(fr, dtype=float)
    rms = np.sqrt((fr ** 2).mean(axis=1)); pk = np.abs(fr).max(axis=1)
    idx = np.where(pk > cmax * np.maximum(rms, 1e-12))[0]
    if len(idx) == 0:
        return fr
    x4 = synth(analyse(fr[idx]), 4 * N)
    x4 /= np.maximum(np.abs(x4).max(axis=1, keepdims=True), 1e-12)
    lo = np.zeros(len(idx)); hi = np.full(len(idx), np.log(1e5)); best = fr[idx].copy()
    for _ in range(20):
        mid = 0.5 * (lo + hi)
        yb = synth(analyse(np.tanh(np.exp(mid)[:, None] * x4)))
        c = np.abs(yb).max(axis=1) / np.maximum(np.sqrt((yb ** 2).mean(axis=1)), 1e-12)
        ok = c <= cmax * 0.98
        hi = np.where(ok, mid, hi); lo = np.where(ok, lo, mid)
        best[ok] = yb[ok]
    fr[idx] = best
    return fr


def out_spec(X, cmax=CREST_MAX):
    return wtlib.finalize(limit_crest(synth(np.broadcast_to(X, (F, NH))), cmax))


def out_time(y, cmax=CREST_MAX):
    return wtlib.finalize(limit_crest(synth(analyse(y)), cmax))


def src8(kind, kmax=KMAX):
    """The band-limited source at 8x, (1, M8)."""
    return synth(src(kind, kmax), M8)


def src8n(kind, kmax=KMAX):
    x = src8(kind, kmax); return x / np.abs(x).max()


TC8 = ((T8 + 0.5) % 1.0) - 0.5       # the cycle as t in [-0.5, 0.5): index 0 is t = 0


def hold_grid(r):
    """Sample-and-hold read positions (indices into an 8x cycle) for a NON-integer rate r holds per cycle.
    The grid is centred on t = 0 (a hold straddles the loop seam, so the seam never steps); the partial hold
    lands at t = 0.5, where the saw has its riser anyway."""
    return ((np.round(TC8 * r) / r) % 1.0 * M8).astype(int) % M8


def rms_norm(x, target=0.5):
    r = np.sqrt((x ** 2).mean(axis=1, keepdims=True))
    return x * (target / np.maximum(r, 1e-12))


# analog-prototype responses on the harmonic axis (s = j k / c, c = corner in harmonics)
def _s(c):
    return 1j * KK / c


def bp2(c, Q):
    s = _s(c); return (s / Q) / (s * s + s / Q + 1)


def lp2(c, Q):
    s = _s(c); return 1.0 / (s * s + s / Q + 1)


def comb_fb(p, g):
    """Feedback comb, loop delay 1/p cycles (resonances every p harmonics), DC gain normalised."""
    e = np.exp(-2j * np.pi * KK / p)
    return (1 - np.abs(g)) / (1 - g * e)


def fold_tri(v):
    return 4 * np.abs(((v + 1) / 4) % 1.0 - 0.5) - 1


def naive_saw(theta):
    """Ramp -1..1 with the riser at theta = 0.5 (mod 1), matching src('saw')."""
    return 2 * ((theta + 0.5) % 1.0) - 1


def soft_square(theta, edge=0.004):
    """Square with edges at theta = .25/.75 (matching src('sqr')); ramps `edge` cycles wide."""
    return np.clip(np.cos(2 * np.pi * theta) / (np.pi * edge), -1, 1)


# ══ SPEC — resonant filters ═════════════════════════════════════════════════════════════════════
def bandpass_saw_ringer():
    """Saw through a two-pole resonant bandpass, exact steady state with the filter's phase (each riser rings
    as a decaying burst). Frames: centre h3 -> h420 (exponential) while Q climbs 2 -> 50 — a nasal saw becomes
    a whistling burst that rings through the whole cycle."""
    return out_spec(src('saw') * bp2(geo(3, 420), lin(2, 50, 2)))


def twin_peak_bandpass_square():
    """Square through TWO resonant bandpasses a ratio 3.3 apart (inharmonic spacing, so the two bursts beat
    against each other inside the cycle). Frames: centres h2 -> h260, Q 3 -> 36 — hollow square to a
    two-tone ringing chirp."""
    c = geo(2, 260); Q = lin(3, 36, 1.5)
    return out_spec(src('sqr') * (bp2(c, Q) + 0.8 * bp2(3.3 * c, 1.3 * Q)))


def _hp_ladder(c, r):
    s = _s(c); G = (s / (1 + s)) ** 4
    return G / (1 + 4 * r * G)


def resonant_highpass_saw():
    """Saw through a four-pole ladder HIGHPASS with rising resonance. Frames: corner h0.8 -> h350, feedback
    0.3 -> 0.97 of self-oscillation — the saw loses its body, then a resonant peak climbs out of the top and
    screams over a hollow fizz."""
    return out_spec(src('saw') * _hp_ladder(geo(0.8, 350), lin(0.3, 0.97)))


def resonant_highpass_square():
    """Square through the four-pole ladder highpass, resonance near self-oscillation from the start. Frames:
    corner h1.2 -> h500 — a square that turns into a thin, ringing needle."""
    return out_spec(src('sqr') * _hp_ladder(geo(1.2, 500, 0.8), lin(0.6, 0.985)))


# ══ SPEC — combs, flangers, phasers, dispersion ═════════════════════════════════════════════════
def feedback_comb_saw():
    """Saw through a feedback comb whose delay shrinks from 1/2.5 cycle to 1/120 cycle while feedback rises
    0.9 -> 0.985. Frames: resonant teeth every 2.5 harmonics (a ringing, pitched-up saw) spread into a few
    screaming metallic peaks at h120, h240, h360... with everything between them gone."""
    return out_spec(src('saw') * comb_fb(geo(2.5, 120, 0.9), lin(0.9, 0.985)))


def detuned_combs_pulse():
    """Two feedback combs in parallel (g 0.985) at a 5.5-harmonic spacing on a 25% pulse, the second detuned
    from the first. Frames: detune 0 -> 80% — one sharp comb splits into two tooth sets that drift apart into
    beating, glassy clusters over the pulse's own gap pattern."""
    p = 5.5; d = lin(0.0, 0.8, 0.8)
    return out_spec(src('pul25') * (comb_fb(p, 0.985) + comb_fb(p * (1 + d), 0.985)))


def double_combed_saw():
    """Two feedback combs in SERIES, delays a golden ratio apart and of OPPOSITE polarity (+0.93 / -0.93), both
    shrinking together. Frames: spacing h1.6 -> h90 — the two tooth sets multiply into an irregular, glassy
    formant field."""
    p = geo(1.6, 90)
    return out_spec(src('saw') * comb_fb(p, 0.93) * comb_fb(p * 1.618, -0.93))


def flanger_pulse():
    """Jet flanger on a 40% pulse: dry + delayed copy with feedback, H = 1 + e/(1 - g e), e = exp(-i 2 pi k
    tau), centred (the dry path is advanced tau/2). At tau = 1/2 the copy cancels every odd harmonic; as the
    delay shrinks the first notches climb through the low harmonics. Frames: tau 0.5 -> 0.04 cycle, feedback
    0.3 -> 0.7 — octave pulse, jet sweep, a hollow resonant comb."""
    tau = geo(0.5, 0.04); g = lin(0.3, 0.7)
    e = np.exp(-2j * np.pi * KK * tau)
    return out_spec(src('pul40') * (1 + e / (1 - g * e)) * np.exp(1j * np.pi * KK * tau))


def _phaser(n_st, cc, g, sign=1.0, step_oct=0.35):
    H = np.zeros((F, NH), complex)
    for i in range(F):
        n = int(n_st[i, 0]); c0 = float(cc[i, 0]); gi = float(g[i, 0])
        A = np.ones(NH, complex)
        for j in range(n):
            cj = c0 * 2.0 ** (step_oct * (j - (n - 1) / 2))
            A *= (1 - 1j * KK[0] / cj) / (1 + 1j * KK[0] / cj)
        H[i] = 0.5 * (1 + sign * A / (1 - gi * A))
    return H


def phaser_saw():
    """Allpass-chain phaser with feedback (dry + chain), exact. Frames: stage count 2 -> 64 while the chain's
    centre climbs h3 -> h120 and feedback rises 0.3 -> 0.85 — one soft notch becomes a forest of resonant
    notches (feedback 0.6 -> 0.97 turns the gaps between them into resonant peaks)."""
    n = np.round(lin(2, 64)); return out_spec(src('saw') * _phaser(n, geo(3, 120), lin(0.6, 0.97)))


def phaser_pulse():
    """Inverted phaser (dry MINUS chain) on a 25% pulse, negative feedback. Frames: 4 -> 64 stages, centre
    h200 -> h4 (falling), feedback 0 -> -0.85 — a thin, phased pulse whose interleaved notch sets comb it into
    a buzzing band-comb as the chain sinks. (Wild from frame 0: the inverted chain cancels the pulse's body.)"""
    n = np.round(lin(4, 64)); return out_spec(src('pul25') * _phaser(n, geo(200, 4), -lin(0.0, 0.85), sign=-1.0))


def dispersive_spring_pulse():
    """A feedback comb whose loop also carries DISPERSION (group delay rising with frequency, like a spring), on
    a 25% pulse: loop phase = 2 pi (k/4.5 + D k^2 / 2000), feedback 0.95. Frames: D 0 -> 6 — a dense regular
    comb whose teeth chirp closer and closer together up the spectrum until they alias into a sparkling
    pseudo-random field."""
    D = lin(0.0, 6.0, 0.7)
    phi = 2 * np.pi * (KK / 4.5 + D * KK ** 2 / (2 * KMAX))
    return out_spec(src('pul25') * 0.05 / (1 - 0.95 * np.exp(-1j * phi)))


def smeared_square():
    """Spectral SMEAR: every harmonic of a square leaks energy UPWARD into the bins above it (exponential
    kernel), the leaked energy taking the fixed random phase set. Frames: smear 0 -> 60 bins — a crisp square
    fills its empty even bins, then diffuses into a hissing, bright buzz."""
    X = src('sqr')[0]; mag = np.abs(X)
    ph = np.where(mag > 0, np.angle(X), wtlib.PHASE)
    w = lin(0.0, 60.0, 0.7)
    d = np.arange(NH)
    out = np.zeros((F, NH), complex)
    for i in range(F):
        wi = w[i, 0]
        if wi < 1e-6:
            m = mag
        else:
            ker = np.exp(-d / wi); ker /= ker.sum()
            m = np.convolve(mag, ker)[:NH]
        out[i] = m * np.exp(1j * ph)
    out[:, KMAX:] = 0
    return out_spec(out)


def sub_anchor_comb_saw():
    """SUBBY. The saw's fundamental is held clean; everything above it runs through a feedback comb, feedback
    0.7 -> 0.995, spacing h2 -> h40. Frames: a saw whose top thins into a few ringing metal teeth while the sub
    stays put."""
    X = src('saw'); lo = np.zeros_like(X); lo[:, 0] = X[:, 0]
    p = geo(2.0, 40)      # the comb is centred (advanced half a loop) so no echo of the riser lands on the seam
    return out_spec(lo + (X - lo) * comb_fb(p, lin(0.7, 0.995)) * np.exp(1j * np.pi * KK / p))


def sub_anchor_formant_saw():
    """SUBBY. The saw's fundamental is held clean; everything above it runs through a five-formant 'ah' filter
    whose formants are shifted 0.6x -> 6x and sharpened. Frames: a sub with a mouth on top that opens, rises
    and ends in a shrill, whistling vowel."""
    X = src('saw'); lo = np.zeros_like(X); lo[:, 0] = X[:, 0]
    fr, db, bw = [np.repeat(np.array(v, float)[None, :], F, 0) for v in _VOW['a']]
    return out_spec(lo + 2.0 * (X - lo) * _formant_H(fr, db, bw, 110.0, geo(0.6, 6.0), lin(1.0, 3.0)))


# ══ SPEC — formants, phase copies ═══════════════════════════════════════════════════════════════
_VOW = {   # public-domain bass formant data (Csound manual): Hz, dB, bandwidth Hz
    'a': ([600, 1040, 2250, 2450, 2750], [0, -7, -9, -9, -20], [60, 70, 110, 120, 130]),
    'e': ([400, 1620, 2400, 2800, 3100], [0, -12, -9, -12, -18], [40, 80, 100, 120, 120]),
    'i': ([250, 1750, 2600, 3050, 3340], [0, -30, -16, -22, -28], [60, 90, 100, 120, 120]),
    'o': ([400, 750, 2400, 2600, 2900], [0, -11, -21, -20, -40], [40, 80, 100, 120, 120]),
    'u': ([350, 600, 2400, 2675, 2950], [0, -20, -32, -28, -36], [40, 80, 100, 120, 120]),
}


def _formant_H(fr, db, bw, f0ref, shift, qmul):
    H = 0
    for j in range(5):
        c = fr[:, j:j + 1] / f0ref * shift
        Q = fr[:, j:j + 1] / bw[:, j:j + 1] * qmul
        H = H + 10 ** (db[:, j:j + 1] / 20) * bp2(c, Q)
    return H


def _vowel_path(seq):
    tabs = [np.array(_VOW[v], float) for v in seq]
    pos = U[:, 0] * (len(seq) - 1)
    i0 = np.minimum(pos.astype(int), len(seq) - 2); w = pos - i0
    w = w * w * (3 - 2 * w)
    return [np.array([tabs[a][r] * (1 - ww) + tabs[a + 1][r] * ww for a, ww in zip(i0, w)]) for r in range(3)]


def formant_shifted_saw():
    """Saw through a five-formant 'ah' filter whose formants are SHIFTED together. Frames: shift 0.3x -> 7x
    (giant's chest to chipmunk to a shrieking insect) with the formants sharpening as they rise."""
    fr, db, bw = [np.repeat(np.array(v, float)[None, :], F, 0) for v in _VOW['a']]
    return out_spec(src('saw') * _formant_H(fr, db, bw, 110.0, geo(0.3, 7.0), lin(1.0, 2.5)))


def formant_shifted_pulse():
    """A 33% pulse through a five-formant 'oh' filter whose formants are SHIFTED together 0.5x -> 9x while
    sharpening 1.5x -> 5x. Frames: a dark, hooting pulse that rises into a whistling, piercing vowel."""
    fr, db, bw = [np.repeat(np.array(v, float)[None, :], F, 0) for v in _VOW['o']]
    return out_spec(src('pul33') * _formant_H(fr, db, bw, 110.0, geo(0.5, 9.0), lin(1.5, 5.0)))


def formant_square_vowels():
    """Square through a five-formant filter (2x shifted) walking a-e-i-o-u while the formant Q rises 2x -> 8x. Frames:
    a talking square that ends in robotic whistles."""
    fr, db, bw = _vowel_path("aeiou")
    return out_spec(src('sqr') * _formant_H(fr, db, bw, 90.0, 2.0, lin(2.0, 8.0, 1.2)))


def phase_stacked_saw():
    """Thirteen copies of the saw at phase offsets 0, d .. 12d with ALTERNATING polarity (+ - + ...). At d ~ 0
    they sum to one saw; as d opens the copies cancel the body and pile up (13x) on the harmonics near
    k = 1/(2d), so a resonant hump walks DOWN the spectrum. Frames: d 0.003 -> 0.4 cycle — saw -> multi-riser
    teeth -> a barcode of thin pulses."""
    d = geo(0.003, 0.4, 0.5)
    return out_spec(src('saw') * sum(((-1) ** j) * np.exp(2j * np.pi * KK * j * d) for j in range(13)))


# ══ TIME — dispersion into nonlinearity, fold, rectify, drive ═══════════════════════════════════
def chirp_drive_saw():
    """Allpass DISPERSION into a fixed hard tanh drive (x8): the saw's riser uncoils into a falling chirp
    (group delay linear in harmonic number) and the chirp is squashed into square-edged zaps. Frames:
    dispersion 0 -> 10 cycles of delay at the top — driven saw, laser zaps, a wrapped chirp storm."""
    D = lin(0.0, 10.0, 0.8)
    ph = np.exp(-1j * np.pi * D * (KK / KMAX) ** 2 * KMAX)
    x = rms_norm(synth(src('saw') * ph, M8), 0.45)
    return out_time(np.tanh(8.0 * x))


def dispersed_square_fold():
    """Square through power-law dispersion (phase ~ k^1.5), then a fixed BIASED sine wavefolder driven hard
    (even harmonics from the bias). Frames: dispersion 0 -> 2.5 — a lopsided folded square whose edges turn
    into chirps, and the chirps fold into shimmering bursts."""
    D = lin(0.0, 2.5, 0.8)
    ph = np.exp(-2j * np.pi * D * KK ** 1.5 / np.sqrt(KMAX))
    x = rms_norm(synth(src('sqr') * ph, M8), 0.6)
    return out_time(np.sin(4.5 * np.pi * x + 0.35 * np.pi))


def foldback_saw():
    """Saw through a triangle wavefolder, gain rising 1 -> 16. Frames: the ramp folds back on itself again
    and again — a saw becomes a multi-tooth zigzag (frequency multiplication by folding)."""
    return out_time(fold_tri(geo(1.0, 16.0) * src8('saw') * 0.95))


def edge_fold_square():
    """Band-limited square (Gibbs ripple and all) into a BIASED sine folder, gain 1 -> 9. The flat tops fold
    to fixed levels that breathe through zero at different rates, every edge becomes a burst of folds. Frames:
    square -> lopsided pulse -> ringing fold bursts."""
    return out_time(np.sin(0.5 * np.pi * lin(1.0, 9.0) * src8n('sqr') + 0.7))


def power_shaped_triangle():
    """Exponent waveshaper y = sign(x) |x|^p on a triangle, p 1 -> 40. Frames: the triangle's flanks bow in,
    then everything but the tips collapses — a triangle becomes two sharp spikes (up and down) on a flat line.
    (The spikes ARE the process, so the loudness guard only steps in above crest 6.)"""
    x = src8n('tri')
    return out_time(np.sign(x) * np.abs(x) ** geo(1.0, 40.0, 0.6), cmax=6.0)


def driven_ringing_square():
    """A square rung by a fixed resonant lowpass (h12, Q 8), then DRIVEN asymmetrically: tanh(g (x + 0.3)),
    gain 1 -> 40. Frames: the ringing square saturates into lopsided squared-off ripple pulses."""
    x = rms_norm(synth(src('sqr') * lp2(12, 8), M8), 0.5)
    g = geo(1.0, 40.0)
    return out_time(np.tanh(g * (x + 0.3)) - np.tanh(g * 0.3))


def overdriven_resonance_saw():
    """A saw rung by a fixed resonant bandpass (h20, Q 12) — a whistling burst after every riser — then driven
    into tanh, gain 1 -> 60. Frames: the burst clips into a rectangular buzz packet."""
    x = synth(src('saw') * bp2(20, 12), M8); x /= np.abs(x).max()
    return out_time(np.tanh(geo(1.0, 60.0) * x))


def sub_guard_fuzz_saw():
    """SUBBY. Multiband drive: the fundamental stays a clean sine; harmonics 2+ go through a triangle
    WAVEFOLDER, gain 1 -> 30, mixed back 0.25 -> 0.6. Frames: a saw keeps its sub body while the top folds into
    a buzzing, splintered fuzz."""
    X = src('saw'); lo = np.zeros_like(X); lo[:, 0] = X[:, 0]
    xl = synth(lo, M8); xl /= np.abs(xl).max()
    xh = synth(X - lo, M8); xh /= np.abs(xh).max()
    return out_time(xl + lin(0.25, 0.6) * fold_tri(geo(1.0, 30.0) * xh))


def sub_crush_square():
    """SUBBY. The square's fundamental stays clean; everything above it is bitcrushed (bits 8 -> 1) and
    sample-rate reduced (256 -> 12 holds per cycle). Frames: a solid sub under a collapsing digital hash."""
    X = src('sqr'); lo = np.zeros((1, NH), complex); lo[:, 0] = X[:, 0]
    xl = synth(lo, M8); xh = synth(X - lo, M8); xh = xh / np.abs(xh).max()
    bits = lin(8.0, 1.0, 0.8); L = 2.0 ** bits / 2
    holds = np.round(geo(256, 12))
    y = np.zeros((F, M8))
    for i in range(F):
        h = int(holds[i, 0])
        idx = (np.floor(T8[0] * h) / h * M8).astype(int)
        q = (np.floor(xh[0] * L[i, 0]) + 0.5) / L[i, 0]
        y[i] = q[idx]
    y = rms_norm(y, 0.3)
    return out_time(xl / np.abs(xl).max() + y)


# ══ TIME — crush, sample rate, slew, chop ════════════════════════════════════════════════════════
def supercrushed_saw():
    """Bit depth AND sample rate crushed together: holds per cycle 10 -> 2.5 (non-integer, sliding) while the
    mid-tread quantiser drops 2.5 -> 1.2 bits. Frames: a crunchy stair -> aliasing images -> a few jagged
    blocks."""
    x = src8n('saw')[0]
    r = geo(10.0, 2.5, 0.5); L = 2.0 ** lin(2.5, 1.2, 0.5) / 2
    return out_time(np.array([np.round(x[hold_grid(r[i, 0])[0]] * L[i, 0]) / L[i, 0] for i in range(F)]))


def lofi_resampled_saw():
    """Sample-rate reduction with LINEAR interpolation (a cheap resampler): the saw is read at only r points per
    cycle, r 40 -> 2.2 (non-integer, sliding, grid centred on the seam) and joined with straight lines, so the
    riser smears across a whole interval and the ramp breaks into crooked facets. Frames: saw -> faceted saw ->
    lopsided triangle shards."""
    x = src8n('saw')[0]
    r = geo(40.0, 2.2, 0.7)
    y = np.zeros((F, M8))
    for i in range(F):
        ri = r[i, 0]
        j0 = np.floor(TC8[0] * ri); fr = TC8[0] * ri - j0
        a = x[((j0 / ri) % 1.0 * M8).astype(int) % M8]; b = x[(((j0 + 1) / ri) % 1.0 * M8).astype(int) % M8]
        y[i] = a + (b - a) * fr
    return out_time(y)


def bitcrushed_triangle():
    """An offset quantiser (floor(x L + 0.3) / L) on a triangle, bits 6 -> 0.8 (continuous): the offset makes
    the stairs lopsided, so even harmonics grow as the bits fall. Frames: triangle -> stepped pyramid -> a
    three-level digital shard."""
    L = 2.0 ** lin(6.0, 0.8, 0.8) / 2
    return out_time(np.floor(src8n('tri') * L + 0.3) / L)


def decimated_square():
    """Integer sample-rate reduction of the square with the sample clock offset by 0.37 of a hold, holds per
    cycle 256 -> 3. Frames: square -> jittered pulse patterns (odd hold counts break the symmetry, even
    harmonics appear) -> a lopsided 3-step pulse."""
    x = src8('sqr')[0]
    holds = np.round(geo(256, 3))
    y = np.zeros((F, M8))
    for i in range(F):
        h = holds[i, 0]
        idx = ((np.floor(T8[0] * h) + 0.37) / h * M8).astype(int) % M8
        y[i] = x[idx]
    return out_time(y)


def slew_limited_pulse():
    """A 33% pulse through a SLEW LIMITER whose rise time grows 0.4% -> 25% of the cycle while the fall is made
    1 -> 12x slower. Frames: pulse -> trapezoid -> sharkfin -> a lopsided ramp that never reaches the bottom."""
    M = N * 4
    x = synth(src('pul33'), M)[0]; x /= np.abs(x).max()
    up = (2.0 / (lin(0.004, 0.25) * M))[:, 0]
    dn = up / lin(1.0, 12.0)[:, 0]
    y = np.zeros(F); out = np.zeros((F, M))
    for rep in range(3):
        for n in range(M):
            y = y + np.clip(x[n] - y, -dn, up)
            if rep == 2:
                out[:, n] = y
    return out_time(out)


def sliced_saw():
    """A 100% AM gate inside the cycle: the saw multiplied by a raised-cosine gate running n times per cycle,
    n 3 -> 40 (stepped). Frames: a saw sliced into three soft swells, then into a comb of short rounded saw
    splinters (sidebands mirrored around the gate rate)."""
    x = src8n('saw')[0]; n = np.round(geo(3.0, 40.0, 0.7))
    return out_time(x[None, :] * (0.5 - 0.5 * np.cos(2 * np.pi * n * T8)))


# ══ TIME — FM / PM ════════════════════════════════════════════════════════════════════════════════
def square_fmed_by_saw():
    """A square whose phase is modulated by a (band-limited) saw at the same frequency, index 0 -> 4 cycles
    of deviation. Frames: square -> edges crowding toward the saw's riser -> a burst of pulses."""
    m = lin(0.0, 4.0, 0.7)
    mod = synth(src('saw', 200), M8); mod /= np.abs(mod).max()
    return out_time(soft_square(T8 + m * mod))


def soft_fm_saw():
    """Saw carrier, sine modulator at ratio 2 (phase modulation), index 0 -> 25 radians. Frames: a saw whose
    ramp bends twice per cycle, then folds into a cluster of risers — soft FM growl."""
    beta = lin(0.0, 25.0, 0.5)
    return out_time(naive_saw(T8 + beta / (2 * np.pi) * np.sin(4 * np.pi * T8)))


def soft_fm_square():
    """Square carrier, sine modulator at ratio 1 (phase modulation), index 0 -> 14 radians. The modulation
    breaks the square's half-wave symmetry, so even harmonics grow out of an odd-only source. Frames: a square
    whose edges slide toward each other, then multiply into a cluster of uneven pulses."""
    beta = lin(0.0, 14.0, 0.5)
    return out_time(soft_square(T8 + beta / (2 * np.pi) * np.sin(2 * np.pi * T8)))


# ══ TIME — noise, ring mod ════════════════════════════════════════════════════════════════════════
def natural_noise_saw():
    """Saw plus NATURAL noise: frozen, slowly evolving noise (random complex spectrum crossfaded between six
    seeded snapshots) coloured by a broad band that rises h3 -> h220 like a gust. Frames: noise level 0 -> 5x
    the saw — clean saw, breathy saw, then a whistling wind with a pitched core."""
    rng = np.random.default_rng(0x5A11)
    snaps = (rng.standard_normal((6, NH)) + 1j * rng.standard_normal((6, NH))) * KK[0] ** -0.2
    snaps[:, KMAX:] = 0
    pos = U[:, 0] * 5; i0 = np.minimum(pos.astype(int), 4); w = (pos - i0)[:, None]
    nz = snaps[i0] * np.cos(w * np.pi / 2) + snaps[i0 + 1] * np.sin(w * np.pi / 2)
    s = src('saw')
    rs = np.sqrt((np.abs(s) ** 2).sum()); rn = np.sqrt((np.abs(nz) ** 2).sum(axis=1, keepdims=True))
    nz = nz * np.abs(bp2(geo(3.0, 220.0), 1.2))
    rn = np.sqrt((np.abs(nz) ** 2).sum(axis=1, keepdims=True))
    return out_spec(s + 5.0 * U ** 0.7 * nz * rs / rn)


def digital_noise_square():
    """Square plus DIGITAL noise: 1-bit random steps (a new bit pattern every frame) at 32 -> 700 steps per
    cycle, mixed in 0 -> 100%. Frames: square -> glitch-flecked square -> a pitched 1-bit hash."""
    rng = np.random.default_rng(0xB175)
    x = src8n('sqr')[0]
    steps = np.round(geo(32, 700))
    a = lin(0.0, 1.0, 1.4)
    y = np.zeros((F, M8))
    for i in range(F):
        h = int(steps[i, 0])
        bits = rng.integers(0, 2, h) * 2 - 1.0
        y[i] = (1 - a[i, 0]) * x + a[i, 0] * bits[(T8[0] * h).astype(int)]
    return out_time(y)


def ring_modulated_saw():
    """Saw times a sine at an integer ratio r, r 3 -> 80 (exponential, stepped): the saw's series splits into
    sum and difference sidebands mirrored around r. Frames: a buzzing, hollow clang whose formant hump rides up
    with the carrier."""
    r = np.round(geo(3.0, 80.0, 0.7))
    return out_time(src8n('saw')[0][None, :] * np.sin(2 * np.pi * r * T8))


def resonant_highpass_pulse():
    """A 33% pulse through the four-pole ladder HIGHPASS, resonance 0.4 -> 0.97. Frames: corner h0.8 -> h300 —
    the pulse's body drains away and a resonant whistle rides the top of what is left."""
    return out_spec(src('pul33') * _hp_ladder(geo(0.8, 300), lin(0.4, 0.97)))


def ring_modulated_square():
    """Square times a sine at an integer ratio r, r 1 -> 90 (exponential, stepped). Frames: the square's odd
    series splits into sum and difference sidebands — hollow, then bell-ish, then a high clanging buzz."""
    r = np.round(geo(1.0, 90.0))
    return out_time(src8('sqr')[0][None, :] * np.sin(2 * np.pi * r * T8))


# ══ FX — Terrain's own filters (fx_probe.cpp) ═════════════════════════════════════════════════════
# (IDENT, "TYPEINDEX SOURCE key=a:b[:curve] ...", docstring).  Cutoffs in Hz at f0 = 23.4375 Hz, so
# harmonic = Hz / 23.44 (1 kHz = h43, 10 kHz = h427).
FX_JOBS = [
    ("LADDER HOWL SAW", "0 saw cut=90:7000 res=0.35:1.0 drv=0.0:1.0",
     "Saw through Terrain's LADDER LP24 (Huovilainen ZDF, 2x). Frames: cutoff h4 -> h300 while resonance "
     "climbs to self-oscillation and drive rises — a warm saw ends in a howling, whistling ladder."),
    ("ACID SAW CLIMB", "4 saw cut=50:5500 res=1.0 drv=0.2:1.0",
     "Saw through Terrain's ACID 303 at full resonance. Frames: cutoff h2 -> h235 with drive rising — the "
     "squelch opens into a screaming acid peak."),
    ("ACID SCREAM SQUARE", "33 sqr cut=70:3500 res=0.5:1.0 drv=0.2:1.0",
     "Square through Terrain's ACID SCREAM. Frames: cutoff h3 -> h150, resonance and drive to max."),
    ("POLIVOKS PULSE", "89 pul25 cut=60:6000 res=0.6:1.0 drv=0.3:1.0",
     "25% pulse through Terrain's POLIVOKS voicing (the aggressive diode). Frames: cutoff h2.5 -> h256."),
    ("SCREAMING LOWPASS PULSE", "85 pul25 cut=60:5000 res=0.7:1.0 drv=0.3:1.0",
     "25% pulse through Terrain's SCREAM LP (feedback lowpass with a clipper in the loop). Frames: cutoff "
     "h2.6 -> h213, loop gain and clipping rising."),
    ("SCREAMING BANDPASS SAW", "86 saw cut=90:7000 res=0.5:1.0 drv=0.0:1.0",
     "Saw through Terrain's SCREAM BP (feedback bandpass). Frames: centre h4 -> h300 with the feedback "
     "loop and its clipper pushed to the top."),
    ("SCREAM BANDPASS PULSE", "86 pul33 cut=5000:60 res=0.6:1.0 drv=0.3:1.0",
     "33% pulse through Terrain's SCREAM BP, the band FALLING h213 -> h2.6 while the feedback loop and its "
     "clipper are pushed — a whistle that dives into a screaming, clipped honk."),
    ("GROWL FORMANT SAW", "71 saw cut=20:20000 res=0.3:1.0 drv=0.2:1.0",
     "Saw through Terrain's FORMANT GROWL. Frames: the vowel knob a -> e -> i -> o -> u while Q and the "
     "bank's drive rise — talking saw to a snarling growl."),
    ("SOPRANO FORMANT PULSE", "115 pul25 cut=20:20000 res=0.4:1.0 drv=0.2:1.0",
     "25% pulse through Terrain's FORMANT SOPRANO register. Frames: vowels a -> u, Q and drive rising."),
    ("FORTY EIGHT STAGE PHASER SAW", "103 saw cut=25:3000 res=0.9:0.95 cyc=96",
     "Saw through Terrain's 48-stage PHASER at high feedback. Frames: centre h1 -> h128 — a dense resonant "
     "notch comb sweeping up through the saw."),
    ("INVERTED PHASER PULSE", "102 pul25 cut=5000:30 res=0.9 cyc=96",
     "25% pulse through Terrain's 32-stage PHASER N (inverted mix), high feedback. Frames: centre h213 -> "
     "h1.3 (falling)."),
    ("KARPLUS SAW", "66 saw cut=60:4000 res=0.9 cyc=160",
     "Saw exciting Terrain's KARPLUS BRIGHT string loop. Frames: string pitch h2.6 -> h170."),
    ("AM RADIO SAW", "91 saw cut=20:20000 res=0.2:0.9 drv=0.3:1.0",
     "Saw through Terrain's RADIO (IF bandpass pair into a crusher). Frames: the band 150 Hz -> 8 kHz."),
    ("SAMPLE AND HOLD SQUARE", "83 sqr cuth=600:5 round=1 res=0.5 drv=0.3",
     "Square through Terrain's SAMPLE & HOLD with feedback. Frames: 600 -> 5 holds per cycle."),
    ("CRUSHED SAW DESCENT", "23 saw cut=20000 crushm=32:2 round=1 res=0.3:0.0 drv=0.5:1.0",
     "Saw through Terrain's BIT CRUSH, its clock locked to the cycle. Frames: 32 -> 2 holds per cycle, "
     "bits 7.6 -> 4, its input tanh driven harder."),
    ("DIODE RING SQUARE", "21 sqr cuth=1:61 round=1 res=0.0:1.0 drv=0.3",
     "Square through Terrain's RING MOD, carrier stepping h1 -> h61, sine -> diode ring blend."),
    ("BODE SHIFTED SAW", "22 saw shifth=1:42 round=1 res=0.5",
     "Saw through Terrain's BODE frequency shifter, shift = 1 -> 42 harmonics (integer, so it loops)."),
    ("BODE DOWN SQUARE", "76 sqr shifth=1:42 round=1 res=0.5",
     "Square through Terrain's BODE DOWN: partials pushed DOWN through zero and mirrored."),
    ("WAVESHAPED TRIANGLE", "24 tri cut=20000 res=0.3:1.0 drv=0.6:1.0 lvl=1.0:12.0 dc=0.0:0.6",
     "Triangle through Terrain's WAVESHAPER (ADAA). Frames: morph tanh -> sine fold, drive and level rising, "
     "and a growing DC bias so the folds go lopsided (even harmonics)."),
    ("ACID DRIVEN SINE", "4 sine cut=30:2500 res=1.0 drv=1.0 lvl=2.0:8.0 dc=0.0:0.8",
     "SUBBY. A sine overdriven into Terrain's ACID 303 at full resonance. Frames: cutoff h1.3 -> h107 with "
     "the input level rising 2 -> 8 and a growing bias (even harmonics) — a round sub that squelches into an "
     "acid shriek."),
    ("COMB FIFTHS PULSE", "64 pul33 cut=50:3000 res=0.95 cyc=96",
     "33% pulse through Terrain's COMB FIFTH (two combs a fifth apart). Frames: comb pitch h2 -> h128."),
    ("SAMPLE AND HOLD MINUS SAW", "84 saw cuth=250:3 round=1 res=0.0:0.9 drv=0.5",
     "Saw through Terrain's S&H MINUS (input minus its own held staircase): what is left is a saw of saws "
     "whose teeth run at the hold rate. Frames: 250 -> 3 holds per cycle, hold feedback 0 -> 0.9 — a resonant peak at the hold rate "
     "walks down the spectrum."),
    ("OBX MORPH SAW", "9 saw cut=600 res=0.3:0.97 morph=0.0:1.0",
     "Saw through Terrain's OB-X SVF at h26: frames morph LP -> notch -> HP with resonance rising."),
    ("XPD BANDPASS SQUARE", "38 sqr cut=50:3000 res=0.9 drv=0.2:1.0",
     "Square through Terrain's XPD BP24. Frames: centre h2 -> h128, drive rising."),
    ("SEM BANDPASS SAW", "51 saw cut=9000:60 res=0.9 drv=0.2:1.0",
     "Saw through Terrain's SEM BP, the band FALLING h384 -> h2.6 with drive rising — a fizzy whistle "
     "that sinks into a honking, driven hum."),
    ("DIODE RING SAW", "21 saw cuth=2:48 round=1 res=1.0:0.3 drv=0.3:1.0",
     "Saw through Terrain's RING MOD, carrier stepping h2 -> h48, the diode-ring blend falling to a cleaner "
     "sine ring while the input drive rises."),
    ("SAMPLE AND HOLD TRIANGLE", "83 tri cuth=300:3 round=1 res=0.0:0.9 drv=0.5",
     "Triangle through Terrain's SAMPLE & HOLD, feedback rising 0 -> 0.9 (a comb at the hold rate). Frames: "
     "300 -> 3 holds per cycle — a stepped pyramid that rings."),
]


def _fxkey(ident):
    return ident.replace(" ", "_")


def _fx_table(ident, doc):
    def fn():
        p = os.path.join(FXDUMP, _fxkey(ident) + ".f32")
        if not os.path.isfile(p):
            raise FileNotFoundError("fx dump '%s' not found in %s — build and run fx_probe.cpp (see the header "
                                    "of gen2_processed.py)" % (_fxkey(ident), FXDUMP))
        a = np.fromfile(p, dtype=np.float32).astype(float)
        if a.size != F * N:
            raise ValueError("fx dump %s has %d samples, expected %d" % (p, a.size, F * N))
        return out_time(a.reshape(F, N))
    fn.__doc__ = doc
    fn.__name__ = "fx_" + _fxkey(ident).lower()
    return fn


TABLES = [
    ("BANDPASS SAW RINGER", bandpass_saw_ringer),
    ("TWIN PEAK BANDPASS SQUARE", twin_peak_bandpass_square),
    ("RESONANT HIGHPASS SAW", resonant_highpass_saw),
    ("HIGHPASS NEEDLE SQUARE", resonant_highpass_square),
    ("FEEDBACK COMB SAW", feedback_comb_saw),
    ("DETUNED COMBS PULSE", detuned_combs_pulse),
    ("DOUBLE COMBED SAW", double_combed_saw),
    ("FLANGER PULSE", flanger_pulse),
    ("PHASER SAW", phaser_saw),
    ("PHASER PULSE", phaser_pulse),
    ("DISPERSIVE SPRING PULSE", dispersive_spring_pulse),
    ("SMEARED SQUARE", smeared_square),
    ("SUB ANCHOR COMB SAW", sub_anchor_comb_saw),
    ("SUB ANCHOR FORMANT SAW", sub_anchor_formant_saw),
    ("FORMANT SHIFTED SAW", formant_shifted_saw),
    ("FORMANT SHIFTED PULSE", formant_shifted_pulse),
    ("FORMANT SQUARE VOWELS", formant_square_vowels),
    ("PHASE STACKED SAW", phase_stacked_saw),
    ("CHIRP DRIVE SAW", chirp_drive_saw),
    ("DISPERSED SQUARE FOLD", dispersed_square_fold),
    ("FOLDBACK SAW", foldback_saw),
    ("EDGE FOLD SQUARE", edge_fold_square),
    ("POWER SHAPED TRIANGLE", power_shaped_triangle),
    ("DRIVEN RINGING SQUARE", driven_ringing_square),
    ("OVERDRIVEN RESONANCE SAW", overdriven_resonance_saw),
    ("SUB FUZZ SAW", sub_guard_fuzz_saw),
    ("SUB CRUSH SQUARE", sub_crush_square),
    ("SUPERCRUSHED SAW", supercrushed_saw),
    ("LO-FI RESAMPLED SAW", lofi_resampled_saw),
    ("BITCRUSHED TRIANGLE", bitcrushed_triangle),
    ("DECIMATED SQUARE", decimated_square),
    ("SLEW LIMITED PULSE", slew_limited_pulse),
    ("SLICED SAW", sliced_saw),
    ("SQUARE FM BY SAW", square_fmed_by_saw),
    ("SOFT FM SAW", soft_fm_saw),
    ("SOFT FM SQUARE", soft_fm_square),
    ("NATURAL NOISE SAW", natural_noise_saw),
    ("DIGITAL NOISE SQUARE", digital_noise_square),
    ("RING MODULATED SAW", ring_modulated_saw),
    ("RESONANT HIGHPASS PULSE", resonant_highpass_pulse),
    ("RING MODULATED SQUARE", ring_modulated_square),
] + [(ident, _fx_table(ident, doc)) for ident, _job, doc in FX_JOBS]

CATEGORY = {ident: "Processed" for ident, _ in TABLES}


if __name__ == "__main__":
    if "--jobs" in sys.argv:
        for ident, job, _doc in FX_JOBS:
            print(_fxkey(ident), job)
    else:
        import time
        for ident, fn in TABLES:
            t0 = time.time()
            try:
                fr = fn()
                ok, line = wtlib.selfcheck(ident, fr)
                print(line, " %.2fs" % (time.time() - t0))
            except FileNotFoundError as e:
                print("MISSING", ident, e)
