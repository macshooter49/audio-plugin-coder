"""
gen2_processed_b.py — PROCESSED, second pass (fb638, the Terrain 500).

The Virtual Riot idea built our own way: ONE process per table, its AMOUNT is the frame axis, and the name says
the process. gen2_processed.py covered the classic SAW / SQUARE twins (bandpass, comb, flanger, phaser, formant,
drive, fold, crush, S&H, FM-by-saw, ring mod, Terrain's filters on saw/square/pulse). This module goes where that
one did not:

  SOURCES   sine, triangle, narrow pulse, a supersaw frozen into one cycle, a chord in one cycle, a vowel body,
            a noise body (seeded random spectrum), an FM body, a 1-bit random bitstream.
  PROCESSES Chebyshev shaping, feedback FM, spectral bend / squeeze / contrast / inversion / gate / freeze trail,
            slew limiting, octave folding, resonator banks, vocoder bands, in-cycle granular pitch and reversed
            grains, centre clipping, FM by a square, exponential FM, and chained pairs (fold -> comb,
            crush -> bandpass).

Construction routes (every frame loops by construction, as in gen2_processed.py):
  SPEC   the process acts on the harmonic amplitudes (exact, periodic).
  TIME   the process runs on one cycle at 8x (16384 samples) and is band-limited to 1000 harmonics.

LOUDNESS GUARD (limit_crest, same law as gen2_processed.py): a frame whose peak/RMS exceeds 3.5 goes through the
gentlest tanh that brings it to 3.5, so peak normalisation cannot turn a spiky frame into a volume hole.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import wtlib

N, F, NH = wtlib.SIZE, wtlib.FRAMES, wtlib.NH
KMAX = 1000
OS = 8
M8 = N * OS
KK = np.arange(1, NH + 1, dtype=float)[None, :]   # (1, NH) harmonic numbers
KI = np.arange(1, NH + 1)                          # integer harmonic numbers
U = np.linspace(0.0, 1.0, F)[:, None]             # (F, 1) frame position 0..1
T8 = (np.arange(M8) / M8)[None, :]                # (1, M8) one cycle at 8x
TC8 = ((T8 + 0.5) % 1.0) - 0.5                    # the cycle as t in [-0.5, 0.5)
PH = wtlib.PHASE


# ══ building blocks ═════════════════════════════════════════════════════════════════════════════
def lin(a, b, c=1.0):
    return a + (b - a) * U ** c


def geo(a, b, c=1.0):
    return a * (b / a) ** (U ** c)


def src(kind, kmax=KMAX):
    """Coherent-phase band-limited source, complex harmonic amplitudes (1, NH): x(t) = sum Re(X_k e^{i 2 pi k t}).
    Saw riser at t = 0.5, square edges at t = 0.25 / 0.75, pulses centred on t = 0.5."""
    odd = KI % 2 == 1
    alt = np.where(((KI - 1) // 2) % 2 == 0, 1.0, -1.0)
    if kind == 'saw':
        X = -1j * np.where(odd, 1.0, -1.0) / KI
    elif kind == 'sqr':
        X = np.where(odd, alt / KI, 0.0).astype(complex)
    elif kind == 'tri':
        X = -1j * np.where(odd, alt / KI ** 2, 0.0)
    elif kind == 'sine':
        X = np.zeros(NH, complex); X[0] = -1j
    elif kind.startswith('pul'):
        w = int(kind[3:]) / 100.0
        X = (np.where(KI % 2 == 0, 1.0, -1.0) * np.sin(np.pi * KI * w) / KI).astype(complex)
    else:
        raise ValueError(kind)
    X = X.astype(complex)
    X[kmax:] = 0
    return X[None, :]


def synth(X, M=N):
    X = np.atleast_2d(X)
    S = np.zeros((X.shape[0], M // 2 + 1), complex)
    nk = min(X.shape[1], M // 2 - 1)
    S[:, 1:nk + 1] = X[:, :nk]
    return np.fft.irfft(S, n=M, axis=1) * (M / 2)


def analyse(x, kmax=KMAX):
    M = x.shape[1]
    S = np.fft.rfft(x, axis=1) / (M / 2)
    X = np.zeros((x.shape[0], NH), complex)
    X[:, :kmax] = S[:, 1:kmax + 1]
    return X


CREST_MAX = 3.5


def limit_crest(fr, cmax=CREST_MAX):
    """LOUDNESS GUARD (gen2_processed.py's law): frames over crest cmax get the gentlest tanh that brings them to it
    (run at 4x, band-limited back); frames under it are untouched."""
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


def guard_mix(y, x, cmax=CREST_MAX):
    """LOUDNESS GUARD for sparse processes the tanh guard cannot help (a dead-zone clip leaves a few narrow tips, and
    tanh only squares them into narrow pulses): per frame, blend back the smallest amount a of the process INPUT x
    (both band-limited and peak-normalised) that brings the crest down to cmax. Frames already under it get a = 0."""
    yb = synth(analyse(np.atleast_2d(y))); xb = synth(analyse(np.broadcast_to(x, np.shape(y))))
    yb /= np.maximum(np.abs(yb).max(axis=1, keepdims=True), 1e-12)
    xb /= np.maximum(np.abs(xb).max(axis=1, keepdims=True), 1e-12)

    def crest(z):
        return np.abs(z).max(axis=1) / np.maximum(np.sqrt((z ** 2).mean(axis=1)), 1e-12)
    lo = np.zeros(len(yb)); hi = np.ones(len(yb))
    need = crest(yb) > cmax
    for _ in range(24):
        mid = 0.5 * (lo + hi)
        ok = crest(yb + mid[:, None] * xb) <= cmax * 0.98
        hi = np.where(ok, mid, hi); lo = np.where(ok, lo, mid)
    a = np.where(need, hi, 0.0)
    return yb + a[:, None] * xb


def out_spec(X, cmax=CREST_MAX):
    return wtlib.finalize(limit_crest(synth(np.broadcast_to(X, (F, NH))), cmax))


def out_time(y, cmax=CREST_MAX):
    return wtlib.finalize(limit_crest(synth(analyse(y)), cmax))


def src8(kind, kmax=KMAX):
    return synth(src(kind, kmax), M8)


def src8n(kind, kmax=KMAX):
    x = src8(kind, kmax); return x / np.abs(x).max()


def _s(c):
    return 1j * KK / c


def bp2(c, Q):
    s = _s(c); return (s / Q) / (s * s + s / Q + 1)


def comb_fb(p, g):
    e = np.exp(-2j * np.pi * KK / p)
    return (1 - np.abs(g)) / (1 - g * e)


def fold_tri(v):
    return 4 * np.abs(((v + 1) / 4) % 1.0 - 0.5) - 1


def lookup(base, ph):
    """Read a periodic (M8,) cycle at fractional phase ph (any shape), linear interpolation."""
    p = (ph % 1.0) * M8
    i0 = np.floor(p).astype(int) % M8
    fr = p - np.floor(p)
    return base[i0] * (1 - fr) + base[(i0 + 1) % M8] * fr


# ── new sources ────────────────────────────────────────────────────────────────────────────────
_VOW = {   # public-domain bass formant data (Csound manual): Hz, dB, bandwidth Hz
    'a': ([600, 1040, 2250, 2450, 2750], [0, -7, -9, -9, -20], [60, 70, 110, 120, 130]),
    'e': ([400, 1620, 2400, 2800, 3100], [0, -12, -9, -12, -18], [40, 80, 100, 120, 120]),
    'i': ([250, 1750, 2600, 3050, 3340], [0, -30, -16, -22, -28], [60, 90, 100, 120, 120]),
    'o': ([400, 750, 2400, 2600, 2900], [0, -11, -21, -20, -40], [40, 80, 100, 120, 120]),
    'u': ([350, 600, 2400, 2675, 2950], [0, -20, -32, -28, -36], [40, 80, 100, 120, 120]),
}


def vowel_H(v, f0ref, shift=1.0, qmul=1.0):
    """Five-formant filter response on the harmonic axis (complex, (1, NH) or (F, NH) with array shift)."""
    fr, db, bw = [np.array(x, float) for x in _VOW[v]]
    H = 0
    for j in range(5):
        c = fr[j] / f0ref * shift
        Q = fr[j] / bw[j] * qmul
        H = H + 10 ** (db[j] / 20) * bp2(c, Q)
    return H


def noise_X(seed, tilt=0.0):
    """A NOISE BODY: seeded complex Gaussian spectrum, magnitude tilt k^-tilt (1, NH)."""
    rng = np.random.default_rng(seed)
    X = (rng.standard_normal(NH) + 1j * rng.standard_normal(NH)) * KI ** -tilt
    X[KMAX:] = 0
    return X[None, :]


def supersaw_X(seed=0x5A5A, voices=7):
    """A SUPERSAW FROZEN INTO ONE CYCLE: seven saws at the same pitch, time-shifted by seeded offsets (risers kept
    away from the loop seam) — the snapshot a detuned stack passes through, with its irregular comb of cancellations."""
    rng = np.random.default_rng(seed)
    off = rng.uniform(-0.38, 0.38, voices); off[0] = 0.0
    amp = np.linspace(1.0, 0.65, voices)
    X = src('saw')
    return sum(a * X * np.exp(-2j * np.pi * KK * o) for a, o in zip(amp, off))


def chord_X(roots=(4, 5, 6), shifts=(0.0, 0.03, 0.05), kind='saw'):
    """A CHORD IN ONE CYCLE: saws (or squares) on harmonics r, 2r, 3r... for each chord tone r. The cycle's own
    fundamental is the chord's virtual root."""
    X = np.zeros(NH, complex)
    for r, sh in zip(roots, shifts):
        m = np.arange(1, KMAX // r + 1)
        if kind == 'saw':
            c = -1j * np.where(m % 2 == 1, 1.0, -1.0) / m
        else:
            c = np.where(m % 2 == 1, np.where(((m - 1) // 2) % 2 == 0, 1.0, -1.0) / m, 0.0).astype(complex)
        X[r * m - 1] += c * np.exp(-2j * np.pi * r * m * sh)
    return X[None, :]


# ══ TIME — shapers ═══════════════════════════════════════════════════════════════════════════════
def chebyshev_sine():
    """CHEBYSHEV SHAPING, order swept continuously: y = cos(n acos(x)) on a sine, n 1 -> 64. At integer n it is the
    pure n-th harmonic; between integers the shaper folds the phase into a mirrored chirp whose skirts spread
    around harmonic n. Frames: n 1 -> 160 — a sine whose single bright peak climbs the whole series, flaring and
    pinching."""
    n = geo(1.0, 160.0, 0.9)
    return out_time(np.cos(n * np.arccos(0.9995 * np.sin(2 * np.pi * T8))))


def feedback_fm_sine():
    """FEEDBACK FM: a sine operator modulating its own phase through a short loop (1/48 cycle), solved as the
    periodic steady state (the loop iterated around the cycle 40 times). Feedback 0 -> 7: sine, a leaning ramp,
    then the loop stops settling and the cycle frays into a pitched shred."""
    beta = lin(0.0, 7.0, 1.4)
    th = 2 * np.pi * T8
    d = M8 // 48
    y = np.broadcast_to(np.sin(th), (F, M8)).copy()
    for _ in range(40):
        y = np.sin(th + beta * np.roll(y, d, axis=1))
    return out_time(y)


def octave_folded_sine():
    """OCTAVE FOLDING: four rectifier stages in series, each |x + b| re-centred. With the bias b at 1 they pass the
    sine untouched; as b falls to 0 every stage becomes a full-wave rectifier (an octave up), so the cycle creases
    and doubles four times over. Frames: b 1 -> -0.3 — sine, creased hump, stacked octave ripples, a lopsided buzz."""
    b = lin(1.0, -0.3, 0.8)
    x = np.broadcast_to(np.sin(2 * np.pi * T8), (F, M8)).copy()
    for _ in range(4):
        y = np.abs(x + b)
        lo = y.min(axis=1, keepdims=True); hi = y.max(axis=1, keepdims=True)
        x = 2 * (y - lo) / np.maximum(hi - lo, 1e-12) - 1
    return out_time(x)


def slewed_bitstream():
    """SLEW LIMITING, released: a frozen 1-bit random stream (40 bits per cycle) through a slew limiter whose full
    swing time falls from 30% of the cycle to 0.06%. Frames: a slow wandering hum -> triangle ridges -> trapezoid
    blocks -> the raw bitstream, bright and square-edged."""
    M = 4 * N
    rng = np.random.default_rng(0x51E3)
    nb = 40
    bits = rng.integers(0, 2, nb) * 2 - 1.0
    tt = np.arange(M) / M
    x = bits[np.floor(((tt + 0.5 / nb) % 1.0) * nb).astype(int) % nb]
    up = (2.0 / (geo(0.30, 0.0006, 0.8) * M))[:, 0]
    y = np.zeros(F); out = np.zeros((F, M))
    for rep in range(3):
        for n in range(M):
            y = y + np.clip(x[n] - y, -up, up)
            if rep == 2:
                out[:, n] = y
    return out_time(out)


def sine_fm_by_square():
    """FM BY A SQUARE: a sine whose frequency jumps between 1 + D and 1 - D every half cycle (the phase follows the
    square's integral, a triangle, so the cycle still closes). Deviation D 0.3 -> 110 harmonics: a sine that
    splits into two sweeping tones — one climbing, one diving through zero and back — each with bright skirts."""
    D = geo(0.3, 110.0, 0.9)
    sq = np.tanh(40.0 * np.cos(2 * np.pi * T8))
    tri = np.cumsum(sq, axis=1) / M8; tri -= tri.mean()
    return out_time(np.sin(2 * np.pi * T8 + 2 * np.pi * D * tri))


def granular_pitch_saw():
    """IN-CYCLE GRANULAR PITCH SHIFT: eight Hann grains per cycle (50% overlap), each replaying the saw around its
    own position at speed s. At s = 1 the grains rebuild the saw exactly; as s climbs 1 -> 14 each grain holds a
    faster saw, so the spectrum piles into a comb at multiples of s with grain-rate sidebands — a granular
    formant that climbs out of the saw."""
    n = 8
    s = geo(1.0, 14.0, 0.9)
    base = src8n('saw')[0]
    t = T8[0]
    g0 = np.floor(t * n); u = t * n - g0
    c0 = g0 / n; c1 = (g0 + 1) / n
    w0 = np.cos(np.pi * u / 2) ** 2; w1 = 1 - w0
    y = np.zeros((F, M8))
    for i in range(F):
        si = s[i, 0]
        y[i] = w0 * lookup(base, c0 + si * (t - c0)) + w1 * lookup(base, c1 + si * (t - c1))
    return out_time(y)


def _reversed_grains(base, r):
    y = np.zeros((F, M8))
    for i in range(F):
        ri = r[i, 0]
        c = np.round(TC8[0] * ri) / ri
        y[i] = lookup(base, 2 * c - TC8[0])
    return y


def reversed_grains_chord():
    """IN-CYCLE REVERSE GRAINS on a chord in one cycle (saws on h4, h5, h6): the cycle is cut into r grains (r 1 ->
    48, non-integer, grid centred on the seam) and every grain is played BACKWARDS about its own centre. One grain
    is the chord reversed; many grains chop it into a stutter of backwards shards at the grain rate."""
    base = synth(chord_X((4, 5, 6)), M8)[0]; base /= np.abs(base).max()
    return out_time(_reversed_grains(base, geo(1.0, 48.0, 0.9)))


def reversed_grains_sine():
    """IN-CYCLE REVERSE GRAINS on a sine: r grains per cycle (r 1 -> 64), each played backwards about its centre, so
    the sine is cut into mirrored arcs that jump at every grain edge. Frames: a sine -> a chattering, faceted
    buzz whose brightest band rides the grain rate."""
    base = np.sin(2 * np.pi * T8[0])
    return out_time(_reversed_grains(base, geo(1.0, 64.0, 0.9)))


# ══ SPEC — spectral processes ════════════════════════════════════════════════════════════════════
def spectral_bend_square():
    """SPECTRAL BEND: every harmonic k of a square is moved to harmonic round(k^p), p 1 -> 1.5 (phases carried).
    The odd series bends upward: gaps open in the bass, partials crowd and collide into irregular clusters up top.
    Frames: square -> a bent, bell-edged buzz -> a sparse, glassy scatter."""
    X = src('sqr')[0]
    p = lin(1.0, 1.5, 0.8)
    out = np.zeros((F, NH), complex)
    nz = np.abs(X) > 0
    for i in range(F):
        kp = np.round(KI ** p[i, 0]).astype(int)
        ok = nz & (kp <= KMAX)
        np.add.at(out[i], kp[ok] - 1, X[ok])
    return out_spec(out)


def spectral_squeeze_pulse():
    """SPECTRAL SQUEEZE: the whole spectrum of an 8% pulse is compressed toward harmonic 24, k -> 24 + (k - 24) s,
    s 1 -> 0.035 (energy summed where partials land together). Frames: a bright narrow pulse whose sinc lobes
    squeeze tighter and tighter until the entire spectrum is crammed into one nasal, buzzing knot."""
    X = src('pul8')[0]; mag = np.abs(X)
    s = geo(1.0, 0.035, 0.9)
    en = np.zeros((F, NH)); co = np.zeros((F, NH), complex)
    for i in range(F):
        kp = np.round(24.0 + (KI - 24.0) * s[i, 0]).astype(int)
        ok = (kp >= 1) & (kp <= KMAX) & (mag > 0)
        np.add.at(en[i], kp[ok] - 1, mag[ok] ** 2)
        np.add.at(co[i], kp[ok] - 1, X[ok])
    # magnitude = the energy that lands in each bin; phase = the phase of the coherent sum (the pulse's own phase
    # while nothing collides, so frame 0 IS the pulse), the fixed phase set where the coherent sum cancels.
    ph = np.where(np.abs(co) > 1e-3 * np.sqrt(en), np.angle(co), PH[None, :])
    return out_spec(np.sqrt(en) * np.exp(1j * ph))


def spectral_contrast_vowel():
    """SPECTRAL CONTRAST: a vowel body ('ah', formants on a 55 Hz grid) with every harmonic magnitude raised to the
    power p, p 0.2 -> 7. Frames: a flat, hissing buzz -> the vowel -> only the formant peaks survive, ringing as a
    few pure whistles."""
    H = np.abs(vowel_H('a', 55.0))[0]; H /= H.max()
    p = geo(0.2, 7.0)
    mag = H[None, :] ** p
    mag[:, KMAX:] = 0
    return out_spec(mag * np.exp(1j * PH))


def spectral_inversion_vowel():
    """SPECTRAL INVERSION: an 'ee' vowel body's dB spectrum (clamped to 60 dB of range) scaled by 1 - 2a, a 0 -> 1:
    the vowel flattens into a buzz at the midpoint and then turns INSIDE OUT — formants become holes, the quiet
    top becomes the loudest part. Frames: vowel -> buzz -> a hissing anti-vowel."""
    H = np.abs(vowel_H('i', 70.0))[0] * KI ** -0.3
    db = np.maximum(20 * np.log10(np.maximum(H / H.max(), 1e-9)), -60.0)
    a = lin(0.0, 1.0)
    mag = 10 ** ((1 - 2 * a) * db[None, :] / 20)
    mag[:, KMAX:] = 0
    return out_spec(mag * np.exp(1j * PH))


def spectral_gate_noise():
    """SPECTRAL GATE on a noise body: only the loudest q of the harmonics are let through, q 100% -> 0.5%. Frames:
    a pitched hiss carpet thins into a sparse field of random partials and ends as four or five glassy tones."""
    mag = np.abs(noise_X(0x6A7E, 0.45))[0]
    order = np.argsort(-mag[:KMAX])
    q = geo(1.0, 0.005)
    out = np.zeros((F, NH))
    for i in range(F):
        m = max(4, int(round(q[i, 0] * KMAX)))
        keep = order[:m]
        out[i, keep] = mag[keep]
    return out_spec(out * np.exp(1j * PH))


def freeze_trail_saw():
    """SPECTRAL FREEZE-HOLD: a sharp resonant band (Q 30) sweeps up a saw, h2 -> h700, and every harmonic it
    lights is FROZEN, decaying only 0.6 dB per frame. Frames: a thin whistle that leaves a glowing trail behind it,
    until the spectrum is a comet — a screaming head over a long frozen tail."""
    X = src('saw')
    c = geo(2.0, 700.0)
    B = np.abs(X * bp2(c, 30.0))
    B /= B.max(axis=1, keepdims=True)
    M = np.zeros((F, NH)); held = np.zeros(NH)
    for i in range(F):
        held = np.maximum(held * 0.93, B[i]); M[i] = held
    return out_spec(M * np.exp(1j * np.angle(X)))


def resonator_bank_noise():
    """RESONATOR BANK on a noise body: five resonators tuned like a free bar's modes (1 : 2.76 : 5.40 : 8.93 :
    13.34), Q 45, the whole bank climbing h1.3 -> h70. Frames: a pitched noise that condenses into a ringing,
    inharmonic metal bar sliding up through the spectrum."""
    c = geo(1.3, 70.0)
    H = sum(g * bp2(c * r, 45.0) for r, g in zip((1.0, 2.756, 5.404, 8.933, 13.34), (1.0, 0.8, 0.65, 0.5, 0.4)))
    return out_spec(noise_X(0x8E50, 0.25) * (0.04 + H))


def vocoded_noise_chord():
    """VOCODER BANDS: a noise carrier shaped band by band by a chord in one cycle (saws on h4, h5, h6). The band
    count climbs 3 -> 220 (log-spaced): frames go from tilted hiss through a whispering choir of band steps to the
    chord itself, sung by noise."""
    mod = np.abs(chord_X((4, 5, 6)))[0]
    car = noise_X(0x70C0, 0.0)[0]
    nb = np.round(geo(3.0, 220.0)).astype(int)[:, 0]
    out = np.zeros((F, NH), complex)
    for i in range(F):
        e = np.unique(np.round(np.logspace(0, np.log10(KMAX + 1), nb[i] + 1)).astype(int))
        for a, b in zip(e[:-1], e[1:]):
            em = (mod[a - 1:b - 1] ** 2).sum(); ec = (np.abs(car[a - 1:b - 1]) ** 2).sum()
            out[i, a - 1:b - 1] = car[a - 1:b - 1] * np.sqrt(em / max(ec, 1e-30))
    return out_spec(out)


def spectral_gate_supersaw():
    """SPECTRAL GATE on a supersaw frozen into one cycle: harmonics are ranked by how strongly the seven voices
    reinforce (magnitude x k^0.8, so the gate listens to the stack's comb, not its slope) and only the top q pass,
    q 100% -> 0.6%. Frames: a thick supersaw thins into a sparkling scatter of isolated partials high and low."""
    X = supersaw_X()[0]
    rank = np.argsort(-(np.abs(X[:KMAX]) * KI[:KMAX] ** 0.8))
    q = geo(1.0, 0.006)
    out = np.zeros((F, NH), complex)
    for i in range(F):
        keep = rank[:max(5, int(round(q[i, 0] * KMAX)))]
        out[i, keep] = X[keep]
    return out_spec(out)


def spectral_rotate_saw():
    """SPECTRAL ROTATION: the saw's harmonic series is rotated s bins up the spectrum and the top wraps round to the
    bottom (s 0 -> 700, accelerating). The loud fundamental climbs away as a bright formant with the saw's slope
    trailing above it, while the whisper-quiet top of the series wraps in underneath. Frames: saw -> a nasal,
    upside-down buzz -> an ice-bright hiss over a hollow bass."""
    X = src('saw')[0][:KMAX]
    sh = np.round(lin(0.0, 700.0, 1.6)).astype(int)[:, 0]
    out = np.zeros((F, NH), complex)
    for i in range(F):
        out[i, :KMAX] = np.roll(X, sh[i])
    return out_spec(out)


def granular_pitch_square():
    """IN-CYCLE GRANULAR PITCH SHIFT on a square: six Hann grains per cycle, each replaying the square around its
    own position at speed s, 1 -> 18. Frames: a square whose grains squeeze faster and faster into a buzzing,
    comb-toothed granular formant."""
    n = 6
    s = geo(1.0, 18.0, 0.9)
    base = src8n('sqr')[0]
    t = T8[0]
    g0 = np.floor(t * n); u = t * n - g0
    c0 = g0 / n; c1 = (g0 + 1) / n
    w0 = np.cos(np.pi * u / 2) ** 2; w1 = 1 - w0
    y = np.zeros((F, M8))
    for i in range(F):
        si = s[i, 0]
        y[i] = w0 * lookup(base, c0 + si * (t - c0)) + w1 * lookup(base, c1 + si * (t - c1))
    return out_time(y)


def chebyshev_triangle():
    """CHEBYSHEV SHAPING on a triangle: y = T_n(x) = cos(n acos x), n 1 -> 90. A Chebyshev polynomial of a straight
    ramp ripples fastest where the ramp nears its peaks, so the triangle fills with ripples that crowd toward its
    corners — a chirp that runs into every peak and back. Frames: triangle -> rippled pyramid -> a screaming,
    corner-chirping buzz."""
    n = geo(1.0, 90.0, 0.9)
    tri = 1 - 4 * np.abs(((T8 + 0.25) % 1.0) - 0.5)
    return out_time(np.cos(n * np.arccos(0.9995 * tri)))


def noise_fm_sine():
    """NOISE FM: a sine phase-modulated by a frozen noise body (a seeded random cycle, band-limited to 48 harmonics
    with a 1/k tilt), index 0 -> 16 radians. Frames: a sine that grows a grainy halo, then shatters into a pitched,
    rasping noise whose grain is the modulator's."""
    Xm = noise_X(0x4E01, 1.0); Xm[:, 48:] = 0
    m = synth(Xm, M8); m /= np.abs(m).max()
    I = lin(0.0, 16.0, 1.5)
    return out_time(np.sin(2 * np.pi * T8 + I * m))


# ══ chained pairs ════════════════════════════════════════════════════════════════════════════════
def comb_folded_sine():
    """FOLD -> COMB: a sine into a biased triangle wavefolder (gain 1 -> 26), then a fixed feedback comb whose teeth
    sit every 6.5 harmonics (g 0.97). Frames: a sine; as the folds multiply they feed the comb, which rings them
    into a metallic, whistling chord on the 13-series."""
    y = fold_tri(geo(1.0, 26.0) * 0.98 * np.sin(2 * np.pi * T8) + 0.25)
    return out_spec(analyse(y) * comb_fb(6.5, 0.97) * np.exp(1j * np.pi * KK / 6.5))


def crushed_bandpass_sine():
    """CRUSH -> BANDPASS: a sine through an offset bit crusher (bits 7 -> 0.6, continuous) into a fixed resonant
    bandpass at h48 (Q 18) over a trace of dry signal. Frames: a pure sine; as the stairs coarsen they pump energy
    into the band, which rings out as a crunchy, pitched squeal riding a stepped body."""
    L = 2.0 ** lin(7.0, 0.6, 0.8) / 2
    y = np.round(np.sin(2 * np.pi * T8) * L + 0.25) / L
    return out_spec(analyse(y) * (0.02 + bp2(48.0, 18.0)))


# ══ FX — Terrain's own filters, fed our new sources (fx_probe_b.cpp) ══════════════════════════════
# fx_probe_b.cpp is fx_probe.cpp (the shipping FilterSlot, driven as SynthVoice drives it, run to periodic steady
# state at f0 = 48000/2048 Hz) plus one source kind, "file:<path>": a 2048-sample float32 cycle written here, so
# Terrain's filters can be fed a supersaw, a chord, a noise body, an FM body or a vowel body. Dumps live in
# $TERRAIN_WT_FXDUMP_B, else <wt500>/procb/fxdump_b. Rebuild (seconds):
#     c++ -std=c++17 -O2 -I ../../Tests -I ../../Tests/shim -I ../../Source <wt500>/procb/fx_probe_b.cpp \
#         -framework Accelerate -o <wt500>/procb/fx_probe_b
#     python3 gen2_processed_b.py --jobs | <wt500>/procb/fx_probe_b <wt500>/procb/fxdump_b
_WT500 = os.environ.get("TERRAIN_WT500") or \
    "/private/tmp/claude-501/-Users-macshooter/521ae994-b058-4e64-9804-78f1254cea68/scratchpad/wt500"
FXDIR = os.environ.get("TERRAIN_WT_FXDUMP_B") or os.path.join(_WT500, "procb", "fxdump_b")


def _fm_cycle():
    t = np.arange(N) / N
    return np.sin(2 * np.pi * t + 2.2 * np.sin(4 * np.pi * t))


FX_SOURCES = {
    "ssaw":  lambda: synth(supersaw_X())[0],
    "chord": lambda: synth(chord_X((4, 5, 6)))[0],
    "sev":   lambda: synth(chord_X((4, 5, 6, 7), (0.0, 0.03, 0.05, 0.02)))[0],
    "noise": lambda: synth(noise_X(0x0B0D, 0.5))[0],
    "fm":    _fm_cycle,
    "vowel": lambda: synth(src('pul5') * vowel_H('o', 90.0))[0],
}

# (IDENT, "TYPEINDEX SOURCE key=a:b[:curve] ...", docstring). SOURCE "@name" = FX_SOURCES[name]. Cutoffs in Hz at
# f0 = 23.4375 Hz (harmonic = Hz / 23.44).
FX_JOBS = [
    ("NEGATIVE FLANGE CHORD", "112 @chord cut=20:20000 res=0.3:0.95",
     "A chord in one cycle (saws on h4, h5, h6) through Terrain's FLANGE - (inverted comb at flanger range). "
     "Frames: delay 10 ms -> 0.1 ms with feedback rising — the triad is carved into hollow, jet-like combs."),
    ("FLANGED SEVENTH CHORD", "111 @sev cut=20:20000 res=0.3:0.97",
     "A harmonic-seventh chord in one cycle (saws on h4, h5, h6, h7) through Terrain's FLANGE +. Frames: delay "
     "10 ms -> 0.1 ms, feedback rising to a ringing jet."),
]


def _fxkey(ident):
    return ident.replace(" ", "_")


def fx_job_lines():
    os.makedirs(FXDIR, exist_ok=True)
    used = {j.split()[1][1:] for _, j, _ in FX_JOBS if j.split()[1].startswith("@")}
    for nm in sorted(used):
        x = np.asarray(FX_SOURCES[nm](), dtype=float)
        (x / np.abs(x).max()).astype(np.float32).tofile(os.path.join(FXDIR, "src_%s.f32" % nm))
    out = []
    for ident, job, _ in FX_JOBS:
        w = job.split()
        if w[1].startswith("@"):
            w[1] = "file:" + os.path.join(FXDIR, "src_%s.f32" % w[1][1:])
        out.append(_fxkey(ident) + " " + " ".join(w))
    return out


def _fx_table(ident, doc):
    def fn():
        p = os.path.join(FXDIR, _fxkey(ident) + ".f32")
        if not os.path.isfile(p):
            raise FileNotFoundError("fx dump '%s' not found in %s — build and run fx_probe_b (see the FX section of "
                                    "gen2_processed_b.py)" % (_fxkey(ident), FXDIR))
        a = np.fromfile(p, dtype=np.float32).astype(float)
        if a.size != F * N:
            raise ValueError("fx dump %s has %d samples, expected %d" % (p, a.size, F * N))
        return out_time(a.reshape(F, N))
    fn.__doc__ = doc
    fn.__name__ = "fx_" + _fxkey(ident).lower()
    return fn


TABLES = [
    ("CHEBYSHEV SINE", chebyshev_sine),
    ("CHEBYSHEV TRIANGLE", chebyshev_triangle),
    ("FEEDBACK FM SINE", feedback_fm_sine),
    ("NOISE FM SINE", noise_fm_sine),
    ("OCTAVE FOLDED SINE", octave_folded_sine),
    ("SLEWED BITSTREAM", slewed_bitstream),
    ("SINE FM BY SQUARE", sine_fm_by_square),
    ("GRANULAR PITCH SAW", granular_pitch_saw),
    ("GRANULAR PITCH SQUARE", granular_pitch_square),
    ("REVERSED GRAINS CHORD", reversed_grains_chord),
    ("REVERSED GRAINS SINE", reversed_grains_sine),
    ("SPECTRAL BEND SQUARE", spectral_bend_square),
    ("SPECTRAL ROTATE SAW", spectral_rotate_saw),
    ("SPECTRAL SQUEEZE PULSE", spectral_squeeze_pulse),
    ("VOWEL CONTRAST", spectral_contrast_vowel),
    ("VOWEL INVERSION", spectral_inversion_vowel),
    ("SPECTRAL GATE NOISE", spectral_gate_noise),
    ("SPECTRAL GATE SUPERSAW", spectral_gate_supersaw),
    ("FREEZE TRAIL SAW", freeze_trail_saw),
    ("RESONATOR BANK NOISE", resonator_bank_noise),
    ("VOCODED NOISE CHORD", vocoded_noise_chord),
    ("COMB FOLDED SINE", comb_folded_sine),
    ("CRUSHED BANDPASS SINE", crushed_bandpass_sine),
] + [(ident, _fx_table(ident, doc)) for ident, _job, doc in FX_JOBS]

CATEGORY = {ident: "Processed" for ident, _ in TABLES}


if __name__ == "__main__":
    if "--jobs" in sys.argv:
        print("\n".join(fx_job_lines()))
        sys.exit(0)
    import time
    for ident, fn in TABLES:
        t0 = time.time()
        fr = fn()
        ok, line = wtlib.selfcheck(ident, fr)
        print(line, " %.2fs" % (time.time() - t0))
