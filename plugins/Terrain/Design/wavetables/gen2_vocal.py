"""
gen2_vocal.py -- TERRAIN factory bank, the 500 build: VOCAL (new tables).

Formant journeys that END somewhere a throat should not go: screams, robots, whispers, throat
singing, vocoder bands, growls, talking basses, and big vocal chords.

HOW THE VOICES ARE MADE (clean-room, first principles, nothing sampled)
-----------------------------------------------------------------------
* Source: a Rosenberg-style glottal FLOW pulse (raised-cosine opening, quarter-cosine closing,
  open quotient `oq`), placed one or more times per cycle, FFT'd at 8x and differentiated
  (lip radiation). Several pulses per cycle with unequal size/timing = period doubling,
  tripling, fry, false-cord growl: the SUBHARMONICS a real scream or growl has.
* Filter: parallel analog 2-pole band-pass formants (alternating sign, Klatt-style), evaluated
  as complex responses at the ABSOLUTE frequency of every harmonic for an analysis pitch `hz`,
  so the phase is a real voice's phase: each cycle looks like a glottal kick and ringing.
* Formant numbers: the public Csound "formant values" appendix (bass / tenor / countertenor /
  alto / soprano, 5 formants each) and Peterson & Barney (1952) male / female / child averages.
* Anything nonlinear (crush, S&H, drive, fold, sync, self-warp) is done at 8x (16384 samples
  per cycle) on a periodic signal and band-limited back to <= 1000 harmonics: alias-free, and
  the wrap is seamless by construction.
All randomness is seeded. Every table: 128 frames x 2048, finished by wtlib.finalize.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, wtlib

FR, SIZE, NH = wtlib.FRAMES, wtlib.SIZE, wtlib.NH
OS = 8
BIG = SIZE * OS                      # 8x oversampled cycle for nonlinear work
K = np.arange(1, NH + 1, dtype=float)
X = np.linspace(0.0, 1.0, FR)        # frame axis 0..1
TB = np.arange(BIG) / BIG            # one cycle at 8x, [0, 1)
KMAX = 1000                          # band limit for every time-domain build


# ── paths along the frame axis ────────────────────────────────────────────────────────────
def ss(u):
    u = np.clip(u, 0.0, 1.0)
    return u * u * (3.0 - 2.0 * u)


def ramp(a, b, x=X):
    """0 before a, 1 after b, smoothstep between."""
    return ss((x - a) / (b - a))


def keys(vals, x=X, log=False, hold=0.0):
    """smooth path through equally spaced keyframes; hold>0 parks on each key (articulation)."""
    v = np.asarray(vals, dtype=float)
    if log:
        v = np.log(v)
    m = len(v)
    pos = np.clip(x, 0.0, 1.0) * (m - 1)
    i0 = np.clip(np.floor(pos).astype(int), 0, m - 2)
    f = pos - i0
    if hold > 0:
        f = np.clip((f - hold / 2) / (1 - hold), 0.0, 1.0)
    f = ss(f)
    out = v[i0] * (1 - f) + v[i0 + 1] * f
    return np.exp(out) if log else out


def db(x):
    return 10.0 ** (np.asarray(x, dtype=float) / 20.0)


# ── spectrum <-> cycle ───────────────────────────────────────────────────────────────────
def spec_frame(S):
    """complex harmonic spectrum (NH,) -> one 2048-sample cycle (Nyquist bin dropped)."""
    sp = np.zeros(SIZE // 2 + 1, dtype=complex)
    sp[1:NH] = S[:NH - 1]
    return np.fft.irfft(sp, SIZE)


def spec_big(S, kmax=NH - 1):
    """harmonic spectrum -> one cycle at 8x (same amplitude scale as spec_frame)."""
    sp = np.zeros(BIG // 2 + 1, dtype=complex)
    sp[1:kmax + 1] = S[:kmax]
    return np.fft.irfft(sp, BIG) * OS


def big_spec(y):
    """one 8x cycle -> its harmonic spectrum (NH,), spec_frame scale."""
    return np.fft.rfft(y)[1:NH + 1] / OS


def big_frame(y, kmax=KMAX):
    """8x cycle -> band-limited 2048-sample cycle (alias-free)."""
    Y = np.fft.rfft(y) / OS
    sp = np.zeros(SIZE // 2 + 1, dtype=complex)
    sp[1:kmax + 1] = Y[1:kmax + 1]
    return np.fft.irfft(sp, SIZE)


def pk1(y):
    return y / max(np.abs(y).max(), 1e-12)


def unit(S):
    return S / max(np.abs(S).max(), 1e-12)


def rmsn(S):
    return S / max(np.sqrt(np.mean(np.abs(S) ** 2)), 1e-12)


def rspec(seed):
    """seeded complex gaussian spectrum: one fixed 'noise' realisation on the harmonic grid."""
    r = np.random.default_rng(seed)
    return (r.standard_normal(NH) + 1j * r.standard_normal(NH)) / np.sqrt(2.0)


def done(frames):
    return wtlib.finalize(np.asarray(frames, dtype=float))


# ── the glottis ──────────────────────────────────────────────────────────────────────────
def gflow(ph, oq, sq=2.5):
    """glottal FLOW over one local period, ph in [0,1): raised-cosine opening, quarter-cosine
    closing (the sharp closure is what makes a voice bright), closed for ph >= oq."""
    tp = oq * sq / (1.0 + sq)
    tn = oq / (1.0 + sq)
    g = np.zeros_like(ph)
    a = (ph >= 0) & (ph < tp)
    g[a] = 0.5 * (1.0 - np.cos(np.pi * ph[a] / tp))
    b = (ph >= tp) & (ph < tp + tn)
    g[b] = np.cos(0.5 * np.pi * (ph[b] - tp) / tn)
    return g


def train(pulses, oq, sq=2.5):
    """sum of glottal pulses in one table cycle at 8x. pulses = [(tau, amp, L)], L = the pulse's
    own period in cycles (a pulse only lasts oq*L)."""
    g = np.zeros(BIG)
    for tau, amp, L in pulses:
        if amp == 0:
            continue
        ph = ((TB - tau) % 1.0) / L
        g += amp * gflow(ph, oq, sq)
    return g


def excite(g):
    """glottal flow (8x) -> flow-DERIVATIVE harmonic spectrum (radiation included)."""
    return big_spec(g) * (1j * K)


# ── the tract ────────────────────────────────────────────────────────────────────────────
def bp(f, fc, bw):
    """analog 2-pole band-pass, unit peak at fc, -6 dB/oct skirts."""
    return (1j * bw * f) / (fc * fc - f * f + 1j * bw * f)


def tract(f, F, A, B, alt=True):
    """parallel formant bank (alternating signs so neighbours add instead of cancelling)."""
    H = np.zeros(len(f), dtype=complex)
    for j in range(len(F)):
        s = (-1.0) ** j if alt else 1.0
        H += s * db(A[j]) * bp(f, F[j], B[j])
    return H


# Csound manual, "Formant values" appendix: (F1..F5 Hz, amp dB, bandwidth Hz)
CS = {
    'bass': {'a': ([600, 1040, 2250, 2450, 2750], [0, -7, -9, -9, -20], [60, 70, 110, 120, 130]),
             'e': ([400, 1620, 2400, 2800, 3100], [0, -12, -9, -12, -18], [40, 80, 100, 120, 120]),
             'i': ([250, 1750, 2600, 3050, 3340], [0, -30, -16, -22, -28], [60, 90, 100, 120, 120]),
             'o': ([400, 750, 2400, 2600, 2900], [0, -11, -21, -20, -40], [40, 80, 100, 120, 120]),
             'u': ([350, 600, 2400, 2675, 2950], [0, -20, -32, -28, -36], [40, 80, 100, 120, 120])},
    'tenor': {'a': ([650, 1080, 2650, 2900, 3250], [0, -6, -7, -8, -22], [80, 90, 120, 130, 140]),
              'e': ([400, 1700, 2600, 3200, 3580], [0, -14, -12, -14, -20], [70, 80, 100, 120, 120]),
              'i': ([290, 1870, 2800, 3250, 3540], [0, -15, -18, -20, -30], [40, 90, 100, 120, 120]),
              'o': ([400, 800, 2600, 2800, 3000], [0, -10, -12, -12, -26], [40, 80, 100, 120, 120]),
              'u': ([350, 600, 2700, 2900, 3300], [0, -20, -17, -14, -26], [40, 60, 100, 120, 120])},
    'countertenor': {'a': ([660, 1120, 2750, 3000, 3350], [0, -6, -23, -24, -38], [80, 90, 120, 130, 140]),
                     'e': ([440, 1800, 2700, 3000, 3300], [0, -14, -18, -20, -20], [70, 80, 100, 120, 120]),
                     'i': ([270, 1850, 2900, 3350, 3590], [0, -24, -24, -36, -36], [40, 90, 100, 120, 120]),
                     'o': ([430, 820, 2700, 3000, 3300], [0, -10, -26, -22, -34], [40, 80, 100, 120, 120]),
                     'u': ([370, 630, 2750, 3000, 3400], [0, -20, -23, -30, -34], [40, 60, 100, 120, 120])},
    'alto': {'a': ([800, 1150, 2800, 3500, 4950], [0, -4, -20, -36, -60], [80, 90, 120, 130, 140]),
             'e': ([400, 1600, 2700, 3300, 4950], [0, -24, -30, -35, -60], [60, 80, 120, 150, 200]),
             'i': ([350, 1700, 2700, 3700, 4950], [0, -20, -30, -36, -60], [50, 100, 120, 150, 200]),
             'o': ([450, 800, 2830, 3500, 4950], [0, -9, -16, -28, -55], [70, 80, 100, 130, 135]),
             'u': ([325, 700, 2530, 3500, 4950], [0, -12, -30, -40, -64], [50, 60, 170, 180, 200])},
    'soprano': {'a': ([800, 1150, 2900, 3900, 4950], [0, -6, -32, -20, -50], [80, 90, 120, 130, 140]),
                'e': ([350, 2000, 2800, 3600, 4950], [0, -20, -15, -40, -56], [60, 100, 120, 150, 200]),
                'i': ([270, 2140, 2950, 3900, 4950], [0, -12, -26, -26, -44], [60, 90, 100, 120, 120]),
                'o': ([450, 800, 2830, 3800, 4950], [0, -11, -22, -22, -50], [70, 80, 100, 130, 135]),
                'u': ([325, 700, 2700, 3800, 4950], [0, -16, -35, -40, -60], [50, 60, 170, 180, 200])},
}

# Peterson & Barney (1952) averages, F1-F3 Hz
PB = {
    'male': {'i': (270, 2290, 3010), 'I': (390, 1990, 2550), 'e': (530, 1840, 2480), 'ae': (660, 1720, 2410),
             'a': (730, 1090, 2440), 'aw': (570, 840, 2410), 'U': (440, 1020, 2240), 'u': (300, 870, 2240),
             'uh': (640, 1190, 2390), 'er': (490, 1350, 1690)},
    'female': {'i': (310, 2790, 3310), 'I': (430, 2480, 3070), 'e': (610, 2330, 2990), 'ae': (860, 2050, 2850),
               'a': (850, 1220, 2810), 'aw': (590, 920, 2710), 'U': (470, 1160, 2680), 'u': (370, 950, 2670),
               'uh': (760, 1400, 2780), 'er': (500, 1640, 1960)},
    'child': {'i': (370, 3200, 3730), 'I': (530, 2730, 3600), 'e': (690, 2610, 3570), 'ae': (1010, 2320, 3320),
              'a': (1030, 1370, 3170), 'aw': (680, 1060, 3180), 'U': (560, 1410, 3310), 'u': (430, 1170, 3260),
              'uh': (850, 1590, 3360), 'er': (560, 1640, 2160)},
}


def pb(v, who='male'):
    """Peterson-Barney vowel as a 5-formant row (F4/F5 and amps/bandwidths filled in)."""
    f1, f2, f3 = PB[who][v]
    s = {'male': 1.0, 'female': 1.17, 'child': 1.32}[who]
    return ([f1, f2, f3, 3500 * s, 4500 * s], [0, -5, -11, -18, -24], [60, 85, 120, 160, 210])


def vpath(rows, x=X, hold=0.0):
    """formant PARAMETERS glide between vowel rows (F, bw geometric; amp dB linear)."""
    n = len(rows[0][0])
    F = np.stack([keys([r[0][j] for r in rows], x, log=True, hold=hold) for j in range(n)], 1)
    A = np.stack([keys([r[1][j] for r in rows], x, hold=hold) for j in range(n)], 1)
    B = np.stack([keys([r[2][j] for r in rows], x, log=True, hold=hold) for j in range(n)], 1)
    return F, A, B


def shelf_air(f, corner=3000.0, slope=0.5):
    """breath / air: a gentle rising-then-flat spectrum above `corner`."""
    return (f / corner) ** slope / (1.0 + (f / corner) ** slope)


# ══ SCREAMS ════════════════════════════════════════════════════════════════════════════════
def screaming_tenor():
    """Tenor 'o' -> 'a' -> a SCREAM. Csound tenor rows glide into a scream row (F1 up to 1 kHz,
    broad bandwidths, a hot 3.7 kHz ring). Frame axis: the glottis presses (open quotient 0.72 ->
    0.24, source corner 700 Hz -> 16 kHz), secondary closure kicks appear inside the cycle
    (the ragged multi-closure of a torn voice), pitch-synchronous rasp climbs from -52 to +6 dB and a flat torn-throat bypass opens.
    Ends as a white-hot, ragged shriek. f0 = 110 Hz per harmonic."""
    hz = 110.0; f = K * hz
    SCREAM = ([1000, 1650, 2900, 3700, 4700], [0, 0, 2, 4, -2], [220, 260, 300, 340, 460])
    F, A, B = vpath([CS['tenor']['o'], CS['tenor']['a'], CS['tenor']['a'], SCREAM, SCREAM])
    oq = keys([0.72, 0.62, 0.46, 0.32, 0.24])
    corner = keys([600, 1000, 2600, 9000, 20000], log=True)
    kick = ramp(0.45, 1.0)
    nmix = keys([-52, -42, -24, -6, 6])
    raw = keys([-80, -80, -60, -34, -20])          # the torn throat's flat 'bypass' rasp
    nz = np.random.default_rng(11).standard_normal(BIG)
    out = []
    for i in range(FR):
        p = [(0.03, 1.0, 1.0), (0.37, 0.55 * kick[i], 0.30), (0.61, 0.8 * kick[i], 0.21),
             (0.83, 0.35 * kick[i] ** 2, 0.12)]
        g = train(p, oq[i])
        E = unit(excite(g) / (1 + 1j * f / corner[i]))
        Nn = rmsn(big_spec(nz * (0.15 + g)))
        src = E + db(nmix[i]) * Nn
        S = src * (tract(f, F[i], A[i], B[i]) + db(raw[i]))
        out.append(spec_frame(S))
    return done(out)


def soprano_to_banshee():
    """Soprano u-o-a-e-i (Csound soprano rows, formants pinned to 330 Hz harmonics, so F1 sits
    on h1-h2 like a real soprano's formant tuning), then the WHISTLE REGISTER takes over: the
    vowel sinks, a 12 Hz-wide whistle resonance climbs h4 -> h9 and splits into a shrieking
    three-peak cluster, while breath rises to meet it. Ends as a banshee."""
    hz = 330.0; f = K * hz
    seq = ['u', 'o', 'a', 'e', 'i']
    x1 = np.clip(X / 0.55, 0, 1)
    F, A, B = vpath([CS['soprano'][v] for v in seq], x=x1)
    sink = keys([0, 0, 0, 0, -8, -22, -34], log=False)
    wh = ramp(0.45, 0.8)
    wpos = keys([4.0, 4.0, 4.0, 4.0, 5.0, 7.0, 9.0], log=True)
    split = ramp(0.8, 1.0)
    air = keys([-52, -48, -44, -40, -30, -18, -10])
    corner = keys([1400, 1600, 2200, 3500, 6000], log=True)
    nzs = rspec(22)
    out = []
    for i in range(FR):
        g = train([(0.05, 1.0, 1.0)], 0.6)
        E = unit(excite(g) / (1 + 1j * f / corner[i]))
        V = E * tract(f, F[i], A[i], B[i]) * db(sink[i])
        fw = wpos[i] * hz
        W = db(10) * wh[i] * (bp(f, fw, 12.0) - 0.8 * split[i] * bp(f, fw * 1.52, 14.0)
                               + 0.7 * split[i] * bp(f, fw * 2.07, 16.0))
        W = W * np.abs(E)
        Air = db(air[i]) * nzs * shelf_air(f, 5000.0, 0.9) * (1 + 3 * wh[i] * np.abs(bp(f, fw, 800.0)))
        out.append(spec_frame(unit(V + W) + Air))
    return done(out)


def self_warped_vowel():
    """A tenor 'a' -> 'e' -> 'i' that reads ITSELF: y(t) = v(t + b*v(t)), the waveform used as its
    own phase offset (self phase-modulation, done at 8x and band-limited). b = 0 -> 0.42 cycles,
    and past 70 % the warp is iterated a second time. Frame 0 is the plain vowel; the end is a
    screaming, self-folding knot."""
    hz = 110.0; f = K * hz
    F, A, B = vpath([CS['tenor']['a'], CS['tenor']['e'], CS['tenor']['i']])
    beta = keys([0.0, 0.05, 0.14, 0.3, 0.55, 0.9])
    it2 = ramp(0.55, 0.9)
    out = []
    for i in range(FR):
        g = train([(0.04, 1.0, 1.0)], 0.55)
        v = pk1(spec_big(unit(excite(g) / (1 + 1j * f / 2500.0)) * tract(f, F[i], A[i], B[i])))
        y = np.interp((TB + beta[i] * v) % 1.0, TB, v, period=1.0)
        if it2[i] > 0:
            y2 = np.interp((TB + 1.3 * beta[i] * y) % 1.0, TB, v, period=1.0)
            y3 = np.interp((TB + 1.3 * beta[i] * y2) % 1.0, TB, v, period=1.0)
            y = (1 - it2[i]) * y + it2[i] * y3
        out.append(big_frame(y))
    return done(out)


def hard_sync_vowels():
    """Formant synthesis by SYNC: five windowed sines at the tenor formant ratios F1..F5/f0,
    restarted every cycle (the old formant-oscillator trick). Frame axis: the window goes from a
    soft decaying bell (a sung 'oo' -> 'ah' -> 'eh' -> 'ee') to a hard rectangle while the sync ratios
    are dragged up 7x (upper formants faster) -- a sync SCREAM. Rolled half a cycle so the reset sits mid-frame."""
    hz = 110.0
    F, A, B = vpath([CS['tenor']['u'], CS['tenor']['a'], CS['tenor']['e'], CS['tenor']['i']])
    p = keys([1.6, 1.3, 0.9, 0.4, 0.08])
    s = keys([1.0, 1.0, 1.2, 2.6, 7.0], log=True)
    onset = keys([0.02, 0.015, 0.01, 0.004, 0.001], log=True)
    out = []
    for i in range(FR):
        w = (1.0 - TB) ** p[i] * (1.0 - np.exp(-TB / onset[i]))
        y = np.zeros(BIG)
        for j in range(5):
            r = F[i, j] / hz * s[i] ** (1.0 + 0.12 * j)
            y += db(A[i, j] * 0.6) * np.sin(2 * np.pi * r * TB)
        y = np.roll(w * y, BIG // 2)
        out.append(big_frame(y))
    return done(out)


def pig_squeal():
    """Wild from frame 0: an inhaled metal 'pig squeal'. A pinched tenor 'i' carries a razor
    resonance (60 -> 16 Hz wide, +12 -> +26 dB) that climbs 2.2 -> 5.4 kHz; the voiced pulse gives
    way to inhaled, pitch-synchronous hiss, and a second squeal peak splits off a fifth above
    (biphonic). f0 = 110 Hz per harmonic."""
    hz = 110.0; f = K * hz
    F, A, B = vpath([CS['tenor']['i'], CS['tenor']['e'], CS['tenor']['i']])
    fc = keys([2200, 2600, 3400, 4400, 5400], log=True)
    bw = keys([60, 45, 32, 22, 16], log=True)
    gain = keys([12, 16, 20, 23, 26])
    voiced = keys([1.0, 0.8, 0.55, 0.35, 0.25])
    nmix = keys([-30, -22, -14, -8, -3])
    bi = ramp(0.55, 0.95)
    nz = np.random.default_rng(33).standard_normal(BIG)
    out = []
    for i in range(FR):
        g = train([(0.04, 1.0, 1.0)], 0.38)
        E = unit(excite(g) / (1 + 1j * f / 5000.0))
        Nn = rmsn(big_spec(nz * (0.3 + g)))
        src = voiced[i] * E + db(nmix[i]) * Nn
        H = (tract(f, F[i], A[i], B[i]) + db(gain[i]) * bp(f, fc[i], bw[i])
             + bi[i] * db(gain[i] - 4) * bp(f, fc[i] * 1.5, bw[i] * 1.3))
        out.append(spec_frame(src * H))
    return done(out)


# ══ ROBOTS / VOCODERS / SPEECH MACHINES ════════════════════════════════════════════════════
def phoneme_chip():
    """A speech chip spelling a word: LPC-style phoneme frames parked one after another -- voiced
    vowels on a buzz (PB male u, a, i, o) and UNVOICED consonants on seeded noise (sh, s, t, f, k:
    shaped noise bands and bursts) -- through an 8 kHz DAC (72 samples per cycle at 110 Hz) with
    no reconstruction filter. Frame axis: the DAC decays -- bit depth falls 12 -> 1 bit (mid-rise
    quantiser at 8x, then band-limited) while its hold narrows from a full step to a 10 %
    return-to-zero spike, so the images climb into a glittering comb. Frame 0 is a clean chip
    'oo'; the end is one-bit, spiking robot babble. Pulses are centred on the sample instants, so
    the wrap always sits inside a held sample."""
    hz = 110.0; f = K * hz
    # phoneme: (formant row, voicing 0..1, noise band centre Hz, band width oct)
    SH = ([2600, 3400, 4300, 5500, 6500], [0, -4, -6, -10, -14], [500, 700, 900, 1100, 1400])
    SS = ([5200, 6300, 7400, 8600, 9800], [0, -2, -4, -6, -8], [900, 1100, 1300, 1500, 1800])
    TT = ([1800, 3600, 4800, 6200, 7800], [0, 0, 0, -2, -4], [1500, 1800, 2000, 2400, 2800])
    FF = ([1400, 2800, 4200, 6000, 8000], [0, -1, -2, -3, -4], [2000, 2400, 2800, 3200, 3600])
    KK = ([1500, 2200, 3000, 4000, 5200], [0, -2, -8, -12, -16], [400, 600, 900, 1200, 1500])
    seq = [(pb('u'), 1), (pb('u'), 1), (SH, 0), (pb('a'), 1), (SS, 0), (pb('i'), 1), (TT, 0),
           (pb('aw'), 1), (FF, 0), (pb('a'), 1), (KK, 0), (pb('i'), 1), (SS, 0)]
    F, A, B = vpath([r for r, _ in seq], hold=0.6)
    voi = keys([v for _, v in seq], hold=0.6)
    bits = keys([12.0, 7.0, 4.5, 3.0, 2.2, 1.6, 1.0])
    duty = keys([1.0, 1.0, 1.0, 0.7, 0.4, 0.2, 0.1])
    M = 72
    idx = ((np.floor(TB * M + 0.5) / M) % 1.0 * BIG).astype(int) % BIG
    uu = np.abs((TB * M + 0.5) % 1.0 - 0.5)            # distance from the sample instant, in steps
    nzs = rspec(606)
    buzz = np.ones(NH, dtype=complex) / (1 + 1j * f / 3800.0) ** 2
    out = []
    for i in range(FR):
        E = voi[i] * buzz + (1 - voi[i]) * 0.35 * nzs
        v = pk1(spec_big(E * tract(f, F[i], A[i], B[i]), kmax=72))
        v = v[idx]
        L = 2.0 ** bits[i]
        q = (np.floor(v * L / 2.0) + 0.5) / (L / 2.0)
        q = np.clip(q, -1.0, 1.0) * (uu <= duty[i] / 2 + 1e-9)
        out.append(big_frame(q))
    return done(out)


def vocoder_collapse():
    """A channel vocoder speaking a-e-i-o-u-a (Peterson-Barney male) onto a saw carrier, whose
    filterbank COLLAPSES: 36 bands -> 3, each band sharpening from overlapping to needle-thin, the
    carrier brightening (1/k -> 1/k^0.45) and the bank slipping off its own grid and sliding up
    (150 Hz-12 kHz -> 2.2-19 kHz). Frame 0 is a clean robot voice; the end is three screaming
    band-needles over carrier bleed."""
    hz = 110.0; f = K * hz
    lf = np.log2(f)
    F, A, B = vpath([pb(v) for v in ['a', 'e', 'i', 'aw', 'u', 'a']], hold=0.3)
    nb = keys([36, 24, 14, 8, 5, 3], log=True)
    ov = keys([0.7, 0.62, 0.5, 0.38, 0.28, 0.2])
    ct = keys([1.0, 0.95, 0.85, 0.7, 0.55, 0.45])
    slip = keys([0, 0, 0, 0.05, 0.15, 0.3])
    bleed = keys([-50, -46, -40, -34, -28, -22])
    lok = keys([150, 150, 150, 300, 900, 2200], log=True)       # the bank slides up off the voice
    hik = keys([12000, 12000, 12000, 13000, 15500, 19000], log=True)

    def bank(i, N):
        lo, hi = np.log2(lok[i]), np.log2(hik[i])
        c = np.linspace(lo, hi, N)
        sp = (hi - lo) / max(N - 1, 1)
        c = c + slip[i] * sp * np.sin(np.arange(N) * 2.1 + 3 * X[i])
        env = np.abs(tract(f, F[i], A[i], B[i]))
        edb = 20 * np.log10(env / env.max() + 1e-6)
        w = sp * ov[i] * 0.5
        mag = np.zeros(NH)
        for cb in c:
            sh = np.exp(-0.5 * ((lf - cb) / w) ** 2)
            gbd = np.sum(sh * edb) / np.sum(sh)
            mag += db(gbd) * sh
        return mag

    out = []
    phase = -np.pi / 2 + np.pi * K          # saw carrier, rolled half a cycle: riser mid-frame
    for i in range(FR):
        n0 = int(np.floor(nb[i])); fr = nb[i] - n0
        mag = (1 - fr) * bank(i, n0) + fr * bank(i, n0 + 1)
        car = K ** (-ct[i])
        m = mag * car
        m = m / m.max() + db(bleed[i]) * car / car.max()
        out.append(spec_frame(m * np.exp(1j * phase)))
    return done(out)


def tin_robot():
    """A tenor voice saying u-a-i-o, RING-MODULATED inside the cycle by an integer-harmonic carrier
    (so it stays periodic) that climbs 0 -> 41 harmonics, then a second carrier (7 -> 17) rings
    the result again; both carriers square up (tanh) from sine to hard square. Frame 0 is the
    dry voice; the end is a double-ringed tin announcer."""
    hz = 110.0; f = K * hz
    F, A, B = vpath([CS['tenor'][v] for v in ['u', 'a', 'i', 'o']])
    wet = ramp(0.0, 0.22)
    m1 = 1.0 + 40.0 * ramp(0.05, 1.0) ** 1.7
    w2 = ramp(0.62, 1.0)
    m2 = keys([7, 7, 7, 9, 13, 17])
    hard = keys([0.5, 0.8, 1.5, 4.0, 12.0, 40.0], log=True)   # carrier squares up: tin, not glass
    out = []
    for i in range(FR):
        g = train([(0.04, 1.0, 1.0)], 0.42)
        v = pk1(spec_big(unit(excite(g) / (1 + 1j * f / 6000.0)) * (tract(f, F[i], A[i], B[i]) + db(-34))))
        a = int(np.floor(m1[i])); fr = m1[i] - a

        def sq(c):
            return np.tanh(hard[i] * c) / np.tanh(hard[i])
        car = (1 - fr) * sq(np.cos(2 * np.pi * a * TB)) + fr * sq(np.cos(2 * np.pi * (a + 1) * TB))
        y = (1 - wet[i]) * v + wet[i] * v * car
        b = int(np.floor(m2[i])); fb = m2[i] - b
        car2 = (1 - fb) * sq(np.cos(2 * np.pi * b * TB)) + fb * sq(np.cos(2 * np.pi * (b + 1) * TB))
        y = y * (1 - w2[i] + w2[i] * car2)
        out.append(big_frame(y))
    return done(out)


def talkbox_square():
    """A SQUARE wave pushed up a talkbox tube into a mouth saying 'wah-yeah-ow' (PB male
    u-a-i-e-a-aw). The tube adds a comb (feedback 0.25 -> 0.8) that starts to squeal (a peak climbing 2.5 -> 5.2
    kHz), and the mouth's output is overdriven (tanh, drive 1 -> 60, with a growing bias so even harmonics tear into the odd-only
    square). Ends as a fuzzed, squealing talkbox. f0 = 110 Hz per harmonic."""
    hz = 110.0; f = K * hz
    F, A, B = vpath([pb(v) for v in ['u', 'a', 'i', 'e', 'a', 'aw']], hold=0.25)
    # square, rotated a quarter cycle so its edges sit mid-frame, never on the wrap
    sq = np.where(K % 2 == 1, 1.0 / K, 0.0) * np.exp(-1j * np.pi / 2) * np.exp(-2j * np.pi * K * 0.25)
    g = keys([0.25, 0.35, 0.45, 0.55, 0.68, 0.8])
    tau = 1.0 / 1.55
    drive = keys([1.0, 1.3, 2.5, 7.0, 20.0, 60.0], log=True)
    bias = keys([0, 0, 0.05, 0.15, 0.3, 0.45])
    leak = keys([-30, -28, -24, -20, -16, -12])     # the lips never seal: flat mouth leak
    sql = keys([-80, -80, -50, -26, -8, 6])          # the tube starts to feed back: a squeal
    sqf = keys([2500, 2500, 2800, 3500, 4400, 5200], log=True)
    out = []
    for i in range(FR):
        tube = 1.0 / (1.0 - g[i] * np.exp(-2j * np.pi * K * tau))
        H = tract(f, F[i], A[i], B[i]) + db(leak[i]) + db(sql[i]) * bp(f, sqf[i], 60.0)
        y = pk1(spec_big(sq * H * tube))
        y = np.tanh(drive[i] * (y + bias[i])) - np.tanh(drive[i] * bias[i])
        out.append(big_frame(y))
    return done(out)


def sample_rate_speech():
    """A female voice (Peterson-Barney, 220 Hz per harmonic) saying i-a-u-ae-er through a sample-
    and-hold whose rate falls from 200 to 5 steps per cycle: images of the formants fold back
    over the vowel, the voice turns to aliased robot, then to a five-step staircase. Hold grid
    centred on the wrap so no riser lands on it."""
    hz = 220.0; f = K * hz
    F, A, B = vpath([pb(v, 'female') for v in ['i', 'a', 'u', 'ae', 'er']])
    M = keys([200, 90, 40, 18, 9, 5], log=True)
    out = []
    for i in range(FR):
        g = train([(0.04, 1.0, 1.0)], 0.5)
        v = pk1(spec_big(unit(excite(g) / (1 + 1j * f / 4000.0)) * tract(f, F[i], A[i], B[i])))
        m0 = int(np.floor(M[i])); fr = M[i] - m0
        ys = []
        for m in (m0, m0 + 1):
            idx = ((np.floor(TB * m + 0.5) / m) % 1.0 * BIG).astype(int) % BIG
            ys.append(v[idx])
        y = (1 - fr) * ys[0] + fr * ys[1]
        out.append(big_frame(y))
    return done(out)


def voice_scrambler():
    """An analogue speech SCRAMBLER eating a tenor voice (a-e-i-o-u-a). Stage 1 splits the band
    into five sub-bands and shuffles them; stage 2 INVERTS the spectrum around 5.3 kHz (the
    'underwater duck' of inverted speech); stage 3 hops the inversion carrier on a rolling code
    and mirrors the upper band too. Frame 0 is clear speech; the end is encrypted babble."""
    hz = 110.0; f = K * hz
    F, A, B = vpath([CS['tenor'][v] for v in ['a', 'e', 'i', 'o', 'u', 'a']], hold=0.3)
    perm = [3, 0, 4, 1, 2]
    s1 = ramp(0.05, 0.35)
    s2 = ramp(0.35, 0.65)
    kc = keys([48, 48, 48, 48, 48, 37, 61, 29, 53], hold=0.5)
    s3 = ramp(0.68, 0.95)
    hiss = keys([-72, -70, -64, -56, -46, -38])     # line hiss creeps up as the code rolls
    out = []
    for i in range(FR):
        g = train([(0.04, 1.0, 1.0)], 0.45)
        V = unit(excite(g) / (1 + 1j * f / 5000.0)) * tract(f, F[i], A[i], B[i])
        V = unit(V) + db(hiss[i]) * rspec(44) * shelf_air(f, 4000.0, 1.0)
        W = V.copy()
        # stage 1: band shuffle of harmonics 1..40
        Sh = np.zeros(NH, dtype=complex)
        for b in range(5):
            src = np.arange(b * 8, b * 8 + 8)
            dst = np.arange(perm[b] * 8, perm[b] * 8 + 8)
            Sh[dst] += V[src]
        W[:40] = (1 - s1[i]) * V[:40] + s1[i] * Sh[:40]
        # stage 2/3: inversion around kc (rolling), then mirror the band above too
        c = int(round(kc[i]))
        Inv = np.zeros(NH, dtype=complex)
        Inv[:c - 1] = np.conj(W[:c - 1][::-1])
        Inv[c - 1:] = W[c - 1:]
        if s3[i] > 0:
            blk = W[c - 1:c - 1 + 4 * c]
            Up = W[c - 1:].copy()
            Up[:len(blk)] = (1 - s3[i]) * blk + s3[i] * np.conj(blk[::-1])
            Inv[c - 1:] = Up
        W = (1 - s2[i]) * W + s2[i] * Inv
        out.append(spec_frame(W))
    return done(out)


# ══ GROWLS / TALKING BASSES / FRY ══════════════════════════════════════════════════════════
def death_growl_descent():
    """Four glottal pulses per cycle (a bass 'u' sung two octaves above the key, 27.5 Hz per
    harmonic) whose false cords take over: the four pulses go unequal in size and timing, so
    subharmonics at 1/2 and 1/4 of the voice appear and the pitch FALLS two octaves into a
    guttural 'o'-'a' roar, soaked in pitch-synchronous noise and a flat false-cord rasp,
    finally clipped (tanh x30)."""
    hz = 27.5; f = K * hz
    F, A, B = vpath([CS['bass']['u'], CS['bass']['o'], CS['bass']['o'], CS['bass']['a']])
    B = B * keys([1.0, 1.2, 1.6, 2.2])[:, None]
    d = ramp(0.15, 0.85)
    patt = np.array([0.0, 0.75, 0.35, 0.95])
    jt = np.array([0.0, 0.035, -0.02, 0.05])
    nmix = keys([-60, -48, -26, -4, 10])
    drive = keys([1, 1, 1.5, 6, 30], log=True)
    oq = keys([0.6, 0.55, 0.45, 0.4])
    corner = keys([600, 900, 2000, 6000, 18000], log=True)
    raw = keys([-90, -90, -64, -34, -14])            # the false cords' flat rasp
    nz = np.random.default_rng(55).standard_normal(BIG)
    out = []
    for i in range(FR):
        p = [(0.02 + q * 0.25 + d[i] * jt[q], 1.0 - 0.85 * d[i] * patt[q], 0.25) for q in range(4)]
        g = train(p, oq[i])
        E = unit(excite(g) / (1 + 1j * f / corner[i]))
        Nn = rmsn(big_spec(nz * (0.1 + g)))
        S = (E + db(nmix[i]) * Nn) * (tract(f, F[i], A[i], B[i]) + db(raw[i]))
        y = pk1(spec_big(S))
        y = np.tanh(drive[i] * y)
        out.append(big_frame(y))
    return done(out)


def yoi_talking_bass():
    """Subby talking bass. A clean sine sub on h1 never moves; above it a saw speaks 'yoi-yoi-
    wah' (PB male u-aw-a-i-aw-u-a at 55 Hz per harmonic) and is driven through tanh and then a
    sine WAVEFOLDER whose gain climbs 1.6 -> 42. The top is high-passed off h1 so the sub stays
    pure. Frame 0 is a fat 'oo' bass; the end is a shredded, folded yoi over the same sub."""
    hz = 55.0; f = K * hz
    F, A, B = vpath([pb(v) for v in ['u', 'aw', 'a', 'i', 'aw', 'i', 'a']])
    saw = (1.0 / K) * np.exp(-1j * np.pi / 2)
    gain = keys([1.6, 2.0, 3.0, 6.0, 16.0, 42.0], log=True)
    fold = ramp(0.35, 0.8)
    sub = np.sin(2 * np.pi * TB)
    out = []
    for i in range(FR):
        y = pk1(spec_big(saw * tract(f, F[i], A[i], B[i])))
        a = np.tanh(gain[i] * y)
        b = np.sin(0.5 * np.pi * gain[i] * y)
        z = (1 - fold[i]) * a + fold[i] * b
        Z = big_spec(z)
        Z[0] = 0.0
        top = spec_big(Z)
        top = top / max(np.abs(top).max(), 1e-9)
        out.append(big_frame(0.8 * top + 0.9 * sub))
    return done(out)


def vocal_fry_rattle():
    """Wild from frame 0: the creak at the bottom of a voice. The table's own period is the fry
    period (27.5 Hz per harmonic): one short glottal pulse rings a dark bass 'o' tract, then more
    pulses break in at irregular places (diplophonia), the open phase shortens to a click, and the
    tract squeezes 'o' -> 'er' -> a pinched 'i' with needle-thin formants until the cycle is a
    five-pulse, long-ringing, squeaking rattle."""
    hz = 27.5; f = K * hz
    PINCH = ([420, 3300, 4200, 6000, 7800], [0, 6, 4, 0, -4], [60, 80, 110, 150, 200])
    F, A, B = vpath([CS['bass']['u'], pb('er'), pb('i'), PINCH])
    B = B * keys([1.0, 0.6, 0.35, 0.2], log=True)[:, None]
    oq = keys([0.45, 0.25, 0.12, 0.06], log=True)
    corner = keys([500, 1500, 6000, 20000], log=True)
    squeak = keys([-80, -60, -20, 8])                # the pinched glottis squeaks at 7.2 kHz
    extra = [(0.43, 0.8, 0.1, 0.5), (0.61, 0.5, 0.3, 0.7), (0.83, 0.7, 0.5, 0.85), (0.27, 0.45, 0.65, 0.95)]
    out = []
    for i in range(FR):
        p = [(0.05, 1.0, 0.9)]
        for tau, amp, a, b in extra:
            e = ramp(a, b)[i]
            if e > 0:
                p.append((tau + 0.03 * np.sin(7 * tau + 5 * X[i]), amp * e, 0.5))
        g = train(p, oq[i])
        E = unit(excite(g) / (1 + 1j * f / corner[i]))
        H = tract(f, F[i], A[i], B[i]) + db(squeak[i]) * bp(f, 7200.0, 300.0)
        out.append(spec_frame(E * H))
    return done(out)


def kazoo_buzz():
    """A hum ('doo-doo-daa-dee', PB male u-aw-a-e) into a KAZOO: the paper membrane SLAPS -- it
    follows the voice until it hits its stop, then flattens (one-sided hard clamp, the stop
    falling from above the peak to 0.15 of it) and every slap rings the membrane's own 3.2 ->
    5 kHz resonance (+10 -> +44 dB); past 40 % the membrane tears and clamps the OTHER side too, so
    the buzz squares off into odd harmonics. Frame 0 is a buzzing hum; the end a blown-out paper buzz."""
    hz = 110.0; f = K * hz
    F, A, B = vpath([pb(v) for v in ['U', 'u', 'aw', 'a', 'e']], hold=0.35)
    stop = keys([0.85, 0.5, 0.32, 0.2, 0.1])
    gain = keys([1.0, 1.4, 2.0, 3.0, 4.5])
    mres = keys([10, 18, 27, 36, 44])
    mfc = keys([3200, 3600, 4100, 4600, 5000], log=True)
    rattle = keys([0.3, 0.55, 0.9, 1.4, 2.4])
    neg = keys([4.0, 4.0, 1.4, 0.45, 0.14], log=True)   # the torn membrane slaps back too
    out = []
    for i in range(FR):
        g = train([(0.04, 1.0, 1.0)], 0.62)
        v = pk1(spec_big(unit(excite(g) / (1 + 1j * f / 1500.0)) * tract(f, F[i], A[i], B[i])))
        y = np.clip(gain[i] * v, -neg[i], stop[i])
        slap = np.abs(np.diff(np.concatenate([y[-1:], y]))) * BIG / 400.0      # clamp edges
        y = y + rattle[i] * slap * np.sin(2 * np.pi * (mfc[i] / hz) * TB)
        Y = big_spec(y) * (1.0 + (db(mres[i]) - 1.0) * np.abs(bp(f, mfc[i], 900.0)))
        out.append(spec_frame(np.where(K <= KMAX, Y, 0)))
    return done(out)


# ══ THROAT SINGING ═════════════════════════════════════════════════════════════════════════
def sygyt_whistle():
    """Tuvan sygyt: a drone on a bass 'u' with ONE razor formant (18 Hz wide, +22 dB) that picks a
    pentatonic melody out of the harmonic series (h6-8-9-10-12-9-12, parked on each), then climbs
    past any singer (h16 -> 24 -> 32) while it splits into a biphonic then triphonic chord (x1.5,
    x2) and the pressed drone tears into reed rasp. f0 = 110 Hz per harmonic."""
    hz = 110.0; f = K * hz
    F, A, B = vpath([CS['bass']['u'], CS['bass']['u'], CS['bass']['o']])
    hw = keys([6, 8, 9, 10, 12, 9, 12, 16, 24, 32], hold=0.6)
    two = ramp(0.62, 0.8)
    three = ramp(0.8, 0.97)
    oq = keys([0.42, 0.4, 0.36, 0.3, 0.22])            # throat singing is a PRESSED voice
    corner = keys([2500, 3000, 4000, 9000, 20000], log=True)
    reed = keys([-64, -62, -58, -50, -36, -14])        # the pressed reed's rasp
    nz = np.random.default_rng(1616).standard_normal(BIG)
    out = []
    for i in range(FR):
        g = train([(0.04, 1.0, 1.0)], oq[i])
        E = unit(excite(g) / (1 + 1j * f / corner[i]))
        Nn = rmsn(big_spec(nz * g))
        fw = hw[i] * hz
        H = (tract(f, F[i], A[i], B[i]) + db(22) * bp(f, fw, 18.0)
             + two[i] * db(19) * bp(f, fw * 1.5, 20.0) + three[i] * db(17) * bp(f, fw * 2.0, 22.0)
             + db(-24))
        out.append(spec_frame((E + db(reed[i]) * Nn) * H))
    return done(out)


def kargyraa_undertone():
    """Kargyraa: the voice pulses twice per cycle (sings an octave above the key, 55 Hz per
    harmonic) while the false folds flap at HALF its rate: alternate pulses shrink and change
    shape, so the undertone -- the table's own h1 -- grows into a huge sub. The vowel walks bass
    'o' -> 'a' -> 'ae' with narrowing formants that whistle out undertone harmonics; ends in a roaring,
    rasping sub. Subby."""
    hz = 55.0; f = K * hz
    F, A, B = vpath([CS['bass']['o'], CS['bass']['a'], pb('ae'), pb('ae')])
    B = B * keys([1.0, 0.8, 0.5, 0.3], log=True)[:, None]
    d = ramp(0.1, 0.7)
    corner = keys([1500, 2200, 4500, 10000, 20000], log=True)
    roar = keys([-72, -62, -44, -24, -6])
    nz = np.random.default_rng(1717).standard_normal(BIG)
    out = []
    for i in range(FR):
        g1 = train([(0.02, 1.0, 0.5)], 0.5)
        g2 = train([(0.52 + 0.04 * d[i], 1.0 - 0.7 * d[i], 0.5)], 0.5 - 0.3 * d[i])
        E = unit(excite(g1 + g2) / (1 + 1j * f / corner[i]))
        Nn = rmsn(big_spec(nz * g1))                    # the flapping folds rasp once per undertone
        H = tract(f, F[i], A[i], B[i]) + 0.9 * d[i] * db(-6) * bp(f, 55.0, 30.0) + db(-30)
        out.append(spec_frame((E + db(roar[i]) * Nn) * H))
    return done(out)


def open_fifth_monks():
    """A big vocal chord: chant voices on an open fifth (bases h4, h6, h8, h12 at 27.5 Hz per
    harmonic: root two octaves above the key) singing 'o'-'u'-'o'-'a'. Like the Gyuto chant, an
    UNDERTONE voice (h2) and an under-fifth (h3) rise beneath them, then the whole harmonic series
    fills in as the chord tears into a roar. Voices summed in power, one fixed phase set."""
    hz = 27.5; f = K * hz
    F, A, B = vpath([CS['bass'][v] for v in ['o', 'u', 'o', 'a', 'a']])
    voices = [(4, 1.0, 0.0, 0.0), (6, 0.8, 0.0, 0.0), (8, 0.6, 0.0, 0.0), (12, 0.45, 0.0, 0.0),
              (2, 1.1, 0.25, 0.6), (3, 0.7, 0.4, 0.72), (1, 1.0, 0.72, 1.0)]
    tilt = keys([1.35, 1.3, 1.1, 0.8, 0.45])
    wide = keys([1.0, 1.0, 1.2, 1.8, 2.8])
    air = keys([-60, -56, -48, -36, -24])
    ph = wtlib.PHASE
    out = []
    for i in range(FR):
        P = np.zeros(NH)
        for b, amp, a0, a1 in voices:
            e = 1.0 if a1 == 0 else ramp(a0, a1)[i]
            if e <= 0:
                continue
            m = np.arange(1, NH // b + 1)
            k = m * b
            env = np.abs(tract(k * hz, F[i], A[i], B[i] * wide[i]))
            P[k - 1] += (amp * e * m ** (-tilt[i]) * env) ** 2
        mag = np.sqrt(P)
        mag = mag / mag.max() + db(air[i]) * shelf_air(f, 2500.0, 0.7)
        out.append(spec_frame(mag * np.exp(1j * ph)))
    return done(out)


def minor_nine_choir():
    """A big vocal CHORD in one cycle: a bass on h8 and a choir on h16-19-24-36 (C, Eb, G, D -- a
    minor add-nine, root four octaves above the key; 13.75 Hz per harmonic). Each voice has its
    own Csound row and body size and sings 'u' -> 'o' -> 'a'; then the choir loses it: tilt
    flattens, F1 climbs, cluster voices h17-18-20-21-22-23 push in and the breath turns to a
    scream. Voices summed in power, one fixed phase set."""
    hz = 13.75; f = K * hz
    SCR = ([1000, 1700, 2950, 3800, 4800], [0, -2, -4, -4, -10], [200, 240, 300, 340, 440])
    seq = ['u', 'o', 'a', 'a']
    rows = {'bass': vpath([CS['bass'][v] for v in seq] + [SCR]),
            'tenor': vpath([CS['tenor'][v] for v in seq] + [SCR]),
            'alto': vpath([CS['alto'][v] for v in seq] + [SCR]),
            'soprano': vpath([CS['soprano'][v] for v in seq] + [SCR])}
    voices = [(8, 'bass', 1.0, 0.8), (16, 'tenor', 1.03, 1.0), (19, 'tenor', 0.97, 0.9),
              (24, 'alto', 1.0, 0.85), (36, 'soprano', 1.02, 0.7)]
    cluster = [17, 18, 20, 21, 22, 23]
    cl = ramp(0.62, 0.95)
    tilt = keys([1.6, 1.5, 1.3, 0.9, 0.5])
    air = keys([-54, -50, -42, -28, -14])
    nzs = rspec(77)
    ph = wtlib.PHASE
    out = []
    for i in range(FR):
        P = np.zeros(NH)
        allv = [(b, r, s, a, 1.0) for b, r, s, a in voices] + [(b, 'alto', 1.0, 0.6, cl[i]) for b in cluster]
        for b, r, s, amp, e in allv:
            if e <= 0:
                continue
            F, A, B = rows[r]
            m = np.arange(1, NH // b + 1)
            k = m * b
            env = np.abs(tract(k * hz, F[i] * s, A[i], B[i]))
            P[k - 1] += (amp * e * m ** (-tilt[i]) * env) ** 2
        mag = np.sqrt(P)
        mag = mag / mag.max()
        S = mag * np.exp(1j * ph) + db(air[i]) * nzs * shelf_air(f, 2000.0, 0.8)
        out.append(spec_frame(S))
    return done(out)


# ══ WHISPERS ═══════════════════════════════════════════════════════════════════════════════
def whisper_gate():
    """A tenor 'a'-'e'-'i' loses its voice. The glottal pulse fades out while aspiration noise,
    first gated by the glottal flow itself (breathy, pitch-synchronous), takes over (whisper,
    F1 raised 15 %, bandwidths x1.6); past 55 % the noise is chopped by a 12 -> 40 slot gate
    inside the cycle -- a shredded, stuttering whisper. One fixed noise realisation."""
    hz = 110.0; f = K * hz
    F, A, B = vpath([CS['tenor']['a'], CS['tenor']['e'], CS['tenor']['i']])
    wf = ramp(0.2, 0.75)
    Fw = F * (1 + 0.15 * wf[:, None]); Bw = B * (1 + 0.6 * wf[:, None])
    voiced = 1.0 - ramp(0.05, 0.7)
    nmix = keys([-40, -24, -10, 0, 4])
    q = ramp(0.55, 0.95)
    slots = keys([12, 12, 12, 20, 40], log=True)
    nz = np.random.default_rng(66).standard_normal(BIG)
    out = []
    for i in range(FR):
        g = train([(0.04, 1.0, 1.0)], 0.6)
        n = int(round(slots[i]))
        gate = (np.sin(np.pi * TB * n) ** 2) ** 6
        m = (1 - q[i]) * (0.12 + g) + q[i] * gate
        E = unit(excite(g) / (1 + 1j * f / 2500.0))
        Nn = rmsn(big_spec(nz * m))
        S = (voiced[i] * E + db(nmix[i]) * Nn) * tract(f, Fw[i], A[i], Bw[i])
        S = S + db(-40) * Nn * shelf_air(f, 6000.0, 1.0) * wf[i]
        out.append(spec_frame(S))
    return done(out)


def ghost_hum():
    """Subby. A closed-mouth hum (nasal murmur at 250 Hz plus an anti-formant, 55 Hz per harmonic)
    over a sine sub that never leaves; the mouth opens to a bass 'u', then the voiced top drains
    away into hollow bottle-whistles (three 40 -> 14 Hz-wide resonances on seeded noise) and a cold
    air cloud. A ghost moaning over a sub."""
    hz = 55.0; f = K * hz
    F, A, B = vpath([([250, 900, 2200, 3000, 3800], [0, -30, -30, -34, -40], [100, 200, 200, 250, 300]),
                     CS['bass']['u'], CS['bass']['u'], CS['bass']['o']])
    zf = keys([1000, 1300, 1700, 2400], log=True)
    zd = keys([0.95, 0.6, 0.3, 0.1])
    voiced = 1.0 - ramp(0.35, 0.9)
    ghost = ramp(0.3, 1.0)
    wbw = keys([40, 30, 20, 14], log=True)
    nzs = rspec(88)
    ph = wtlib.PHASE
    out = []
    for i in range(FR):
        g = train([(0.04, 1.0, 1.0)], 0.75)
        E = unit(excite(g) / (1 + 1j * f / 600.0))
        Z = 1.0 - zd[i] * bp(f, zf[i], 150.0)
        V = E * tract(f, F[i], A[i], B[i]) * Z
        V = unit(V) * voiced[i]
        Wh = (bp(f, 2300.0, wbw[i]) + 0.7 * bp(f, 3150.0, wbw[i] * 1.2) + 0.5 * bp(f, 4450.0, wbw[i] * 1.4))
        G = ghost[i] * (db(-4) * np.abs(Wh) + db(-26) * shelf_air(f, 3000.0, 0.6)) * nzs
        sub = np.zeros(NH, dtype=complex); sub[0] = 1.1 * np.exp(1j * ph[0])
        out.append(spec_frame(sub + 0.8 * V + G))
    return done(out)


# ══ GRAIN VOICES ═══════════════════════════════════════════════════════════════════════════
def vosim_chatter():
    """Wild from frame 0: VOSIM that ACCELERATES. Each cycle is a train of |sin|^p pulses, each
    quieter than the last, then silence (Kaegi's VOSIM voice). Frame axis: every pulse is shorter
    than the one before by a ratio rho 1 -> 0.8 (the train chirps up inside the cycle -- a 'zip'),
    the first pulse narrows (r 7 -> 44), pulses sharpen from sin^2 to cusps (p 2 -> 0.4), each pulse
    carries an inner formant carrier (2 -> 6 half-waves), the polarity flips every other pulse
    (the lump under the train cancels) and a second, reversed train answers at mid-cycle."""
    r = keys([7.0, 9.0, 13.0, 22.0, 44.0], log=True)
    rho = keys([1.0, 0.96, 0.9, 0.82, 0.74])
    dec = keys([0.86, 0.85, 0.84, 0.84, 0.86])
    pw = keys([2.0, 1.6, 1.0, 0.6, 0.4])
    mm = keys([2.0, 2.0, 3.0, 4.0, 6.0])        # half-waves of the inner formant carrier per pulse
    alt = 0.75 + 0.25 * ramp(0.0, 0.4)
    sec = 0.4 + 0.6 * ramp(0.3, 0.8)

    def burst(y, start, stop, w0, amp0, sign=1.0):
        pos, k, amp, w = start, 0, amp0, w0
        while k < 400 and pos + w <= stop and w > 2.0 / BIG:
            s = (TB >= pos) & (TB < pos + w)
            pol = (1 - 2 * alt[i] * (k % 2)) * sign
            u = (TB[s] - pos) / w
            y[s] += pol * amp * np.abs(np.sin(np.pi * u)) ** pw[i] * np.cos(np.pi * mm[i] * u)
            pos += w; amp *= dec[i]; w *= rho[i]; k += 1

    out = []
    for i in range(FR):
        y = np.zeros(BIG)
        burst(y, 0.0, 0.62, 1.0 / r[i], 1.0)
        if sec[i] > 0:
            burst(y, 0.62, 0.999, 0.6 / r[i], 0.8 * sec[i], -1.0)
        out.append(big_frame(y))
    return done(out)


def grain_soprano():
    """FOF voice: every cycle holds one grain per formant (soprano rows u-o-a-i-e, 110 Hz per
    harmonic): a raised-cosine attack then an exponential decay set by the formant bandwidth,
    tails summed over 20 cycles so the steady state loops. Frame axis: bandwidths shrink x0.18
    (the grains ring into whistles), the upper grains come up to full level, attacks sharpen to
    clicks and each grain CHIRPS upward (+0 -> 6 per cycle) -- a sung vowel turned laser choir."""
    hz = 110.0
    F, A, B = vpath([CS['soprano'][v] for v in ['u', 'o', 'a', 'i', 'e']])
    A = A * (1.0 - keys([0.0, 0.0, 0.25, 0.7, 1.0]))[:, None]
    bwm = keys([1.0, 0.8, 0.5, 0.3, 0.18], log=True)
    tr = keys([0.33, 0.2, 0.08, 0.02, 0.004], log=True)
    ch = keys([0.0, 0.05, 0.5, 2.0, 6.0])
    NC = 20
    t1 = np.arange(SIZE) / SIZE
    tt = (t1[None, :] + np.arange(NC)[:, None])
    out = []
    for i in range(FR):
        y = np.zeros(SIZE)
        env_a = np.where(tt < tr[i], 0.5 * (1 - np.cos(np.pi * np.minimum(tt / tr[i], 1.0))), 1.0)
        for j in range(5):
            bw = max(B[i, j] * bwm[i], 20.0)
            env = env_a * np.exp(-np.pi * bw * tt / hz)
            phs = 2 * np.pi * F[i, j] / hz * (tt + 0.5 * ch[i] * tt * tt / (1 + 0.15 * tt))
            y += db(A[i, j]) * (env * np.sin(phs)).sum(0)
        S = np.fft.rfft(y)
        S[KMAX + 1:] = 0
        out.append(np.fft.irfft(S, SIZE))
    return done(out)


# ══ SPECTRAL IMPOSSIBILITIES ═══════════════════════════════════════════════════════════════
def inside_out_vowel():
    """A tenor 'a' -> 'i' turned INSIDE OUT: the spectral envelope is read through a log-frequency
    mirror that closes around 4 kHz (log f' = (1-s) log f + s (2 log 4k - log f), s 0 -> 1.15). At
    s = 0.5 every harmonic sees the same point (a flat buzz); past it the formants land in the top
    octaves and the lows go hollow. Phase: frame 0's own voice phase plus a quadratic (chirp)
    phase that grows with the mirror, so the flat middle is a sweep inside the cycle, not a click."""
    hz = 110.0; f = K * hz
    F, A, B = vpath([CS['tenor']['a'], CS['tenor']['a'], CS['tenor']['e'], CS['tenor']['i']])
    s = keys([0.0, 0.08, 0.3, 0.55, 0.85, 1.15])
    lp = np.log(4000.0)
    g = train([(0.04, 1.0, 1.0)], 0.5)
    E0 = excite(g)
    ph = np.angle(E0 * tract(f, F[0], A[0], B[0]))
    chirp = np.pi * K * K / NH
    out = []
    for i in range(FR):
        fw = np.exp((1 - s[i]) * np.log(f) + s[i] * (2 * lp - np.log(f)))
        env = np.abs(tract(fw, F[i], A[i], B[i])) * (1.0 / np.sqrt(1 + (fw / 300.0) ** 2))
        env = env / env.max() + db(-58) * shelf_air(f, 3000.0, 0.5)
        c = 0.9 * ss(s[i] / 0.5)
        out.append(spec_frame(env * np.exp(1j * (ph + c * chirp))))
    return done(out)


def gargle():
    """Wild from frame 0: a tenor 'o'/'a' GARGLING. F1 and F2 are thrown around by a seeded bubble
    signal whose rate and depth grow across the table, and a 'water' comb with hopping delay and
    rising feedback (0.2 -> 0.85) bubbles through the voice. Ends drowning."""
    hz = 110.0; f = K * hz
    F, A, B = vpath([CS['tenor']['o'], CS['tenor']['a'], CS['tenor']['o'], CS['tenor']['a']])
    rng = np.random.default_rng(91)
    ph1, ph2, ph3 = rng.uniform(0, 2 * np.pi, 3)
    depth = keys([0.15, 0.3, 0.5, 0.8, 1.1])
    xx = X ** 1.6
    b1 = np.sin(2 * np.pi * 9 * xx + ph1) + 0.6 * np.sin(2 * np.pi * 23 * xx + ph2)
    b2 = np.sin(2 * np.pi * 13 * xx + ph3) + 0.5 * np.sin(2 * np.pi * 31 * xx + ph1)
    gfb = keys([0.2, 0.35, 0.55, 0.72, 0.85])
    dl = 1.0 / keys([3.1, 2.4, 4.3, 1.7, 5.6, 2.2, 3.7], hold=0.3)
    out = []
    for i in range(FR):
        Fi = F[i].copy()
        Fi[0] *= 2 ** (0.45 * depth[i] * b1[i])
        Fi[1] *= 2 ** (0.55 * depth[i] * b2[i])
        g = train([(0.04, 1.0, 1.0)], 0.5)
        E = unit(excite(g) / (1 + 1j * f / 3500.0))
        comb = 1.0 / (1.0 - gfb[i] * np.exp(-2j * np.pi * K * dl[i]))
        S = unit(E * tract(f, Fi, A[i], B[i] * (1 + 0.5 * depth[i])) * comb)
        S = S + db(-72 + 34 * X[i] ** 1.5) * rspec(92) * shelf_air(f, 4000.0, 0.8) * np.abs(comb)
        out.append(spec_frame(S))
    return done(out)


def bass_to_chipmunk():
    """The same 'a' sung by every throat in the Csound table -- bass, tenor, countertenor, alto,
    soprano -- then past any human: the whole formant set scales x1 -> x5 with bandwidths
    narrowing, a helium chipmunk that ends as a buzzing insect. 110 Hz per harmonic."""
    hz = 110.0; f = K * hz
    x1 = np.clip(X / 0.55, 0, 1)
    F, A, B = vpath([CS[v]['a'] for v in ['bass', 'tenor', 'countertenor', 'alto', 'soprano']], x=x1)
    sc = keys([1.0, 1.0, 1.0, 1.5, 2.8, 5.0], log=True)
    bwn = keys([1.0, 1.0, 1.0, 0.8, 0.55, 0.4], log=True)
    oq = keys([0.7, 0.6, 0.5, 0.4, 0.3, 0.22])
    air = keys([-74, -72, -68, -60, -50, -42])
    out = []
    for i in range(FR):
        g = train([(0.04, 1.0, 1.0)], oq[i])
        E = unit(excite(g) / (1 + 1j * f / (900.0 * sc[i] ** 1.8)))
        S = unit(E * tract(f, F[i] * sc[i], A[i], B[i] * sc[i] * bwn[i]))
        S = S + db(air[i]) * rspec(99) * shelf_air(f, 3000.0 * sc[i], 0.8)
        out.append(spec_frame(S))
    return done(out)


TABLES = [
    ("SCREAMING TENOR",      screaming_tenor),
    ("SOPRANO TO BANSHEE",   soprano_to_banshee),
    ("SELF WARPED VOWEL",    self_warped_vowel),
    ("HARD SYNC VOWELS",     hard_sync_vowels),
    ("PIG SQUEAL",           pig_squeal),
    ("PHONEME CHIP",         phoneme_chip),
    ("VOCODER COLLAPSE",     vocoder_collapse),
    ("TIN ROBOT",            tin_robot),
    ("TALKBOX SQUARE",       talkbox_square),
    ("SAMPLE RATE SPEECH",   sample_rate_speech),
    ("VOICE SCRAMBLER",      voice_scrambler),
    ("DEATH GROWL DESCENT",  death_growl_descent),
    ("YOI TALKING BASS",     yoi_talking_bass),
    ("VOCAL FRY RATTLE",     vocal_fry_rattle),
    ("KAZOO BUZZ",           kazoo_buzz),
    ("SYGYT WHISTLE",        sygyt_whistle),
    ("KARGYRAA UNDERTONE",   kargyraa_undertone),
    ("OPEN FIFTH MONKS",     open_fifth_monks),
    ("MINOR NINE CHOIR",     minor_nine_choir),
    ("WHISPER GATE",         whisper_gate),
    ("GHOST HUM",            ghost_hum),
    ("VOSIM CHATTER",        vosim_chatter),
    ("GRAIN SOPRANO",        grain_soprano),
    ("INSIDE OUT VOWEL",     inside_out_vowel),
    ("GARGLE",               gargle),
    ("BASS TO CHIPMUNK",     bass_to_chipmunk),
]
CATEGORY = {ident: "Vocal" for ident, _ in TABLES}
