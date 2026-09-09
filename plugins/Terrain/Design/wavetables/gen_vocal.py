"""
gen_vocal.py -- TERRAIN factory bank: VOCAL / FORMANT (12) + GRANULAR / TEXTURE (12).

FORMANT PITCH PINNING
---------------------
A formant is a resonance of a physical tube: it sits at a FIXED ABSOLUTE frequency and does
not follow the played note. A single-cycle wavetable only stores harmonic RATIOS, so every
formant table here is built by evaluating the vocal-tract transfer function at the absolute
frequency of harmonic n for an analysis pitch of f0 = 110 Hz (A2) -- i.e. at n * 110 Hz.
Play these tables near A2 and the vowels are anatomically correct; play them far above and
they scale like a chipmunk, exactly like every formant wavetable in every synth.
Formant centre frequencies are textbook adult-male acoustics (Peterson-Barney style tables),
computed here from first principles into resonator responses -- nothing is sampled or copied.

GRANULAR NOTE
-------------
A wavetable frame is periodic, so "noise" here is a DENSE, IRREGULAR but FIXED harmonic
spectrum. All randomness is seeded and interpolated ACROSS the frame axis (rndpath), so the
morph glides instead of hashing.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # fb606
import numpy as np, wtlib

FR, NH = wtlib.FRAMES, wtlib.NH
n    = wtlib.n_ax.astype(float)          # harmonic index 1..1024
F0   = 110.0                             # ANALYSIS PITCH -- all formants pinned here
HZ   = n * F0                            # absolute Hz of harmonic n at the analysis pitch
L2   = np.log2(n)
X    = np.linspace(0.0, 1.0, FR)         # frame axis, 0..1


# -- helpers -----------------------------------------------------------------------------
def ss(u):
    return u * u * (3.0 - 2.0 * u)


def path(keys, log=False):
    """keyframes -> (FR,) smoothstep-interpolated trajectory (log=True: geometric)."""
    k = np.asarray(keys, dtype=float)
    if log:
        k = np.log(k)
    m = len(k)
    pos = X * (m - 1)
    i0 = np.floor(pos).astype(int).clip(0, m - 2)
    f = ss(pos - i0)
    v = k[i0] * (1.0 - f) + k[i0 + 1] * f
    return np.exp(v) if log else v


def path_step(keys, sharp=3.2, log=False):
    """like path(), but each keyframe is HELD and the move between them is quick -- real
    articulation parks on a vowel instead of sliding through it."""
    k = np.asarray(keys, dtype=float)
    if log:
        k = np.log(k)
    m = len(k)
    pos = X * (m - 1)
    i0 = np.floor(pos).astype(int).clip(0, m - 2)
    f = ss(np.clip((pos - i0 - 0.5) * sharp + 0.5, 0, 1))
    v = k[i0] * (1.0 - f) + k[i0 + 1] * f
    return np.exp(v) if log else v


def rndpath(seed, k, lo=0.0, hi=1.0):
    """(FR,NH) field: k seeded draws, smoothstep-interpolated along the FRAME axis so the
    spectrum drifts continuously instead of re-randomising every frame."""
    rng = np.random.default_rng(seed)
    keys = rng.uniform(lo, hi, (k, NH))
    pos = X * (k - 1)
    i0 = np.floor(pos).astype(int).clip(0, k - 2)
    f = ss(pos - i0)[:, None]
    return keys[i0] * (1.0 - f) + keys[i0 + 1] * f


def reson(fc, bw, g_db=0.0, hz=None):
    """2-pole resonance magnitude, unit peak * gain -- one formant."""
    hz = HZ if hz is None else hz
    q = max(fc / max(bw, 1e-6), 0.5)
    r = hz / fc
    return (10.0 ** (g_db / 20.0) / q) / np.sqrt((1.0 - r * r) ** 2 + (r / q) ** 2)


def notch(fz, bw, hz=None):
    """Anti-resonance (a spectral ZERO). Nasality = a pole-zero pair, not just extra poles."""
    hz = HZ if hz is None else hz
    q = max(fz / max(bw, 1e-6), 0.5)
    r = hz / fz
    return np.sqrt((1.0 - r * r) ** 2 + (r / q) ** 2) / (1.0 + r * r)


def norm(m):
    return m / np.maximum(np.abs(m).max(axis=1, keepdims=True), 1e-12)


def shelf(n0, slope):
    """flat then power-law rolloff -- the aspiration / air floor."""
    return 1.0 / (1.0 + (n / n0) ** slope)


def gauss_l(centre, width_oct):
    """gaussian in log-frequency, centred on harmonic `centre`, width in octaves."""
    return np.exp(-0.5 * (np.log2(n / centre) / width_oct) ** 2)


def boxsmooth(A, w):
    """box filter ALONG the harmonic axis (widens spectral features into shards/clusters)."""
    w = int(w) | 1
    if w < 3:
        return A
    pad = w // 2
    P = np.pad(A, ((0, 0), (pad, pad)), mode='edge')
    C = np.concatenate([np.zeros((A.shape[0], 1)), np.cumsum(P, axis=1)], axis=1)
    return (C[:, w:] - C[:, :-w]) / float(w)


def mk(mags):
    return wtlib.finalize(wtlib.cycles_from_mags(np.maximum(np.asarray(mags, float), 0.0)))


# adult-male formant centres in Hz (F1, F2, F3) -- textbook vowel acoustics
VOW = {'u': (320, 800, 2560), 'o': (500, 1000, 2600), 'aw': (570, 840, 2410),
       'a': (730, 1090, 2440), 'ae': (660, 1720, 2410), 'e': (530, 1840, 2480),
       'i': (270, 2290, 3010), 'er': (490, 1350, 1690), 'uh': (640, 1190, 2390)}


# =========================== VOCAL / FORMANT ============================================

def terra_vowel_arc():
    """PURPOSE: the workhorse -- one continuous u-o-a-ae-e-i sweep for talking pads and
    formant-morph leads. Formants pinned to f0 = 110 Hz."""
    seq = ['u', 'o', 'a', 'ae', 'e', 'i']
    f1 = path_step([VOW[v][0] for v in seq], log=True)
    f2 = path_step([VOW[v][1] for v in seq], log=True)
    f3 = path_step([VOW[v][2] for v in seq], log=True)
    air = path_step([0.005, 0.011, 0.048, 0.062, 0.030, 0.085])   # per-vowel, not a ramp
    tilt = path_step([2.35, 2.10, 1.52, 1.40, 1.66, 1.14])
    M = np.zeros((FR, NH))
    for i in range(FR):
        e = (reson(f1[i], 70) + reson(f2[i], 100, -5) + reson(f3[i], 140, -11)
             + reson(3400, 190, -19) + reson(4500, 260, -25))
        M[i] = e * n ** (-tilt[i])
    return mk(norm(M) + air[:, None] * shelf(42.0, 2.0))


def terra_throat():
    """PURPOSE: a swallowed, closed throat that OPENS -- vocal wah / talkbox gesture with a
    very low F1 that climbs while F2 falls. Formants pinned to f0 = 110 Hz."""
    f1 = path([205, 213, 232, 278, 440, 960], log=True)
    f2 = path([2350, 2300, 2180, 1950, 1500, 1040], log=True)
    f3 = path([2480, 2500, 2530, 2590, 2680, 2790], log=True)
    tilt = path([2.90, 2.82, 2.66, 2.38, 1.82, 1.05])
    air = path([0.0008, 0.0014, 0.003, 0.009, 0.032, 0.095])
    M = np.zeros((FR, NH))
    for i in range(FR):
        e = (reson(f1[i], 55, 3) + reson(f2[i], 130, -7) + reson(f3[i], 170, -14)
             + reson(4100, 300, -24))
        M[i] = e * n ** (-tilt[i])
    return mk(norm(M) + air[:, None] * shelf(58.0, 1.7))


def terra_whisper():
    """PURPOSE: breathy pad that dissolves into a whispered ghost -- the glottal buzz dies
    away while the /e/ formants stay exactly where they were. Pinned to f0 = 110 Hz."""
    v = 1.0 - ss(np.clip((X - 0.04) / 0.82, 0, 1))          # voiced source 1 -> 0
    a = ss(np.clip((X - 0.08) / 0.88, 0, 1))                # aspiration 0 -> 1
    R = rndpath(1101, 5, 0.30, 1.0)
    env = (reson(530, 55) + reson(1840, 95, -6) + reson(2480, 135, -11)
           + reson(3500, 210, -17) + reson(4600, 300, -22))
    voiced = env * n ** -1.95
    voiced /= voiced.max()
    hiss = env * shelf(55.0, 2.2) * n ** -0.52
    hiss /= hiss.max()
    M = v[:, None] * voiced[None, :] + (0.95 * a)[:, None] * (hiss[None, :] * R)
    return mk(M + 0.0009)


def terra_nasal():
    """PURPOSE: honky nasal lead -- an anti-resonance (spectral ZERO) sweeps up through the
    formants, which is what actually makes a voice sound nasal. Pinned to f0 = 110 Hz."""
    f1 = path([300, 345, 460, 590, 690, 730], log=True)
    fz = path([580, 760, 1050, 1450, 1900, 2350], log=True)
    fz2 = path([3600, 3300, 3050, 2800, 2600, 2450], log=True)
    dep = path([0.10, 0.35, 0.65, 0.90, 1.00, 1.00])
    air = path([0.002, 0.006, 0.014, 0.026, 0.040, 0.055])
    tilt = path([1.98, 1.86, 1.74, 1.64, 1.56, 1.48])
    M = np.zeros((FR, NH))
    for i in range(FR):
        e = (reson(250, 55, -3) + reson(f1[i], 80) + reson(1090, 125, -6)
             + reson(2440, 170, -12) + reson(2950, 220, -16) + reson(4300, 320, -23))
        M[i] = (e * notch(fz[i], 170.0) ** dep[i]
                * notch(fz2[i], 520.0) ** (0.8 * dep[i]) * n ** (-tilt[i]))
    breath = 1.4 * gauss_l(52.0, 0.90) + 0.45 * shelf(18.0, 3.0)
    return mk(norm(M) + air[:, None] * breath[None, :])


def terra_choir():
    """PURPOSE: instant choral 'ah' -- five formant sets at slightly different BODY SIZES
    (vocal-tract length, not pitch) fan out from unison to a wide ensemble. f0 = 110 Hz."""
    spread = path([0.005, 0.030, 0.062, 0.098, 0.135, 0.165])
    sing = path([-30, -22, -15, -9, -4, 0])                 # singer's formant swells
    air = path([0.004, 0.009, 0.018, 0.030, 0.043, 0.056])
    d = np.array([-1.0, -0.55, 0.0, 0.55, 1.0])
    gv = np.array([0.55, 0.80, 1.0, 0.80, 0.55])
    M = np.zeros((FR, NH))
    for i in range(FR):
        acc = np.zeros(NH)
        for dj, gj in zip(d, gv):
            k = 1.0 + spread[i] * dj
            acc += gj * (reson(730 * k, 78) + reson(1090 * k, 120, -5)
                         + reson(2440 * k, 175, -12) + reson(3300 * k, 240, -18))
        acc += 1.6 * reson(2800, 260, sing[i])
        M[i] = acc * n ** -1.5
    breath = 1.05 * gauss_l(112.0, 0.95) + 0.25 * shelf(45.0, 3.2)
    return mk(norm(M) + air[:, None] * breath[None, :])


def terra_child():
    """PURPOSE: body-size morph -- one vowel dragged from a huge chest-resonant giant to a
    child, by scaling the whole formant set 0.68x -> 1.5x. f0 = 110 Hz."""
    k = path([0.62, 0.78, 0.95, 1.15, 1.38, 1.62])
    tilt = path([2.55, 2.30, 2.05, 1.75, 1.45, 1.15])
    air = path([0.004, 0.010, 0.020, 0.034, 0.052, 0.075])
    M = np.zeros((FR, NH))
    for i in range(FR):
        e = (reson(660 * k[i], 70 * k[i]) + reson(1720 * k[i], 110 * k[i], -4)
             + reson(2410 * k[i], 160 * k[i], -10) + reson(3400 * k[i], 230 * k[i], -17)
             + reson(4600 * k[i], 320 * k[i], -23))
        M[i] = e * n ** (-tilt[i])
    A = np.zeros((FR, NH))
    for i in range(FR):
        A[i] = air[i] * shelf(16.0 * k[i] ** 2.6, 2.1)
    return mk(norm(M) + A)


def terra_consonant():
    """PURPOSE: consonant / fricative colour -- sh -> s -> f -> kh -> sh bands over a weak
    voiced buzz, for spoken-word textures and 'ess' sweeps. Bands pinned to f0 = 110 Hz."""
    fc = path([2900, 6200, 4200, 1450, 2900], log=True)     # band centre, Hz
    w = path([0.75, 0.55, 2.20, 1.05, 0.75])                # band width, octaves
    buzz_g = path([0.30, 0.10, 0.05, 0.22, 0.30])
    R = rndpath(1202, 6, 0.20, 1.0)
    buzz = np.exp(-0.5 * (L2 / 1.15) ** 2) * n ** -0.8
    buzz /= buzz.max()
    M = np.zeros((FR, NH))
    for i in range(FR):
        c = fc[i] / F0
        band = np.exp(-0.5 * (np.log2(n / c) / w[i]) ** 2)
        M[i] = band * (0.35 + 0.65 * R[i]) + buzz_g[i] * buzz
    return mk(M)


def terra_vox_glass():
    """PURPOSE: the transformation table -- an /aw/ vowel morphs (in dB, so it glides) into a
    pure hollow odd-harmonic instrument tone. Vowel end pinned to f0 = 110 Hz."""
    a = ss(np.clip((X - 0.05) / 0.9, 0, 1))
    vowel = (reson(570, 60) + reson(840, 105, -4) + reson(2410, 165, -11)
             + reson(3400, 240, -18)) * n ** -1.45
    vowel = vowel / vowel.max() + 0.075 * shelf(52.0, 1.9)
    odd = (n % 2 == 1).astype(float)
    tone = (odd + 0.010) * n ** -1.55 * shelf(160.0, 2.6)
    tone /= tone.max()
    lv, lt = np.log(np.maximum(vowel, 1e-7)), np.log(np.maximum(tone, 1e-7))
    M = np.exp(lv[None, :] * (1 - a)[:, None] + lt[None, :] * a[:, None])
    return mk(M)


def terra_diphthong():
    """PURPOSE: baked-in talking motion -- F1/F2 travel an elliptical LOOP through vowel
    space (oy-ow-ay), one and a half turns, so a slow frame LFO speaks. f0 = 110 Hz."""
    th = 2 * np.pi * 2.5 * X
    f1 = np.exp(np.log(430.0) + 0.50 * np.sin(th))
    f2 = np.exp(np.log(1500.0) + 0.66 * np.cos(th + 0.55))
    f3 = np.exp(np.log(2450.0) + 0.14 * np.sin(2 * th + 1.1))
    air = 0.038 + 0.032 * np.sin(th * 0.80 - 1.2)
    M = np.zeros((FR, NH))
    for i in range(FR):
        e = (reson(f1[i], 68) + reson(f2[i], 115, -5) + reson(f3[i], 175, -12)
             + reson(3600, 260, -20))
        M[i] = e * n ** -1.55
    return mk(norm(M) + air[:, None] * shelf(60.0, 1.6))


def terra_growl():
    """PURPOSE: growled / creaky vocal bass -- the fundamental stays clean while the upper
    spectrum tears into an alternating-harmonic rattle. /a/ formants, f0 = 110 Hz."""
    d2 = path([0.36, 0.56, 0.76, 0.92, 1.0, 0.97])
    d3 = path([0.10, 0.22, 0.40, 0.60, 0.80, 0.94])
    rough = path([0.58, 0.68, 0.78, 0.88, 0.96, 1.0])
    air = path([0.006, 0.018, 0.044, 0.075, 0.048, 0.020])
    f1 = path([620, 570, 510, 455, 410, 375], log=True)
    ring = path([-14, -10, -6, -2, 2, 5])
    R = rndpath(1303, 6, 0.035, 1.0)         # deep, irregular glottal tearing
    hi = np.clip(np.log2(n / 2.6) / 1.1, 0, 1)              # rattle starts at harmonic ~3
    alt = 0.5 + 0.5 * np.cos(np.pi * n)                     # 1 on even, 0 on odd
    tri = 0.5 + 0.5 * np.cos(2 * np.pi * n / 3.0)           # every third harmonic
    M = np.zeros((FR, NH))
    for i in range(FR):
        env = (reson(f1[i], 70) + reson(950, 115, -4) + reson(2100, 170, -10)
               + 1.5 * reson(3400, 190, ring[i])) * n ** -1.42
        m = env * (1.0 - 0.94 * d2[i] * hi * (1.0 - alt))
        m = m * (1.0 - 0.75 * d3[i] * hi * (1.0 - tri))
        M[i] = m * ((1.0 - rough[i]) + rough[i] * R[i])
    rasp = 1.45 * gauss_l(215.0, 0.82) * shelf(300.0, 3.0) + 0.28 * shelf(26.0, 3.2)
    return mk(norm(M) + air[:, None] * (rasp[None, :] * (0.06 + 0.94 * R)))


def terra_vox_mask():
    """PURPOSE: inhuman vocoder mask -- six resonances start stacked on ONE 500 Hz pole and
    fan out across the whole spectrum. Formant positions pinned to f0 = 110 Hz."""
    tgt = np.array([175.0, 430.0, 950.0, 2100.0, 4400.0, 8800.0])
    end = np.array([2150.0, 2350.0, 2550.0, 2790.0, 3020.0, 3260.0])
    gain = np.array([-2.0, 0.0, -3.0, -6.0, -9.0, -12.0])
    sp = ss(np.clip(2.0 * X, 0, 1))               # fan out over the first half...
    cl = ss(np.clip(2.0 * X - 1.0, 0, 1))         # ...then slam shut on one bright cluster
    bell = np.sin(np.pi * X) ** 0.8
    air = 0.005 + 0.070 * bell
    M = np.zeros((FR, NH))
    for i in range(FR):
        acc = np.zeros(NH)
        for j in range(6):
            fo = np.exp(np.log(500.0) * (1 - sp[i]) + np.log(tgt[j]) * sp[i])
            fc = np.exp(np.log(fo) * (1 - cl[i]) + np.log(end[j]) * cl[i])
            acc += reson(fc, 45.0 + 0.045 * fc, gain[j])
        M[i] = acc * n ** (-1.32 + 0.30 * bell[i])
    M = norm(M)
    for i in range(FR):
        fo = np.exp(np.log(500.0) * (1 - sp[i]) + np.log(tgt[5]) * sp[i])
        top = np.exp(np.log(fo) * (1 - cl[i]) + np.log(end[5]) * cl[i]) / F0
        M[i] += air[i] * (gauss_l(top, 0.80) + 0.10 * shelf(top * 0.5, 2.4))
    return mk(M)


def terra_chanter():
    """PURPOSE: operatic solo -- an /a/-to-/e/ chant where the singer's formant at 2.8 kHz
    blooms from nothing to dominant, with a slow tract wobble. f0 = 110 Hz."""
    wob = 1.0 + 0.095 * np.sin(2 * np.pi * 5.5 * X)          # tract vibrato
    f1 = path([730, 690, 640, 590, 550, 530], log=True) * wob
    f2 = path([1090, 1220, 1400, 1580, 1730, 1840], log=True) / wob
    sing = path([-34, -24, -14, -5, 4, 10]) + 3.5 * np.sin(2 * np.pi * 5.5 * X + 1.0)
    air = path([0.004, 0.013, 0.034, 0.055, 0.030, 0.009])
    M = np.zeros((FR, NH))
    for i in range(FR):
        e = (reson(f1[i], 65) + reson(f2[i], 105, -4) + reson(2480, 180, -13)
             + 2.2 * reson(2800, 130, sing[i]) + reson(4200, 300, -22))
        M[i] = e * n ** -1.42
    ring = 1.55 * gauss_l(34.0, 0.70) + 0.22 * shelf(20.0, 3.0)
    return mk(norm(M) + air[:, None] * ring[None, :])


# =========================== GRANULAR / TEXTURE =========================================

def terra_grain_bed():
    """PURPOSE: the bread-and-butter granular bed -- a wide pitched-noise wash that collapses
    down onto a clean tone as the frame axis advances."""
    R = rndpath(2101, 7, 0.04, 1.0)
    cut = path([430, 340, 255, 175, 105, 46, 20], log=True)
    con = path([1.0, 0.95, 0.86, 0.72, 0.52, 0.28, 0.06])
    M = np.zeros((FR, NH))
    for i in range(FR):
        M[i] = (1.0 / (1.0 + (n / cut[i]) ** 3.0)) * (R[i] ** (3.0 * con[i])) * n ** -0.70
    return mk(M)


def terra_cloud():
    """PURPOSE: three grain CLOUDS that start piled on top of each other and drift apart into
    a low rumble, a mid body and a high shimmer -- ambient bed with internal motion."""
    c1 = path([42, 24, 13, 7.5, 4.5], log=True)
    c2 = path([46, 62, 96, 150, 235], log=True)
    c3 = path([50, 112, 250, 430, 640], log=True)
    w1 = path([0.13, 0.28, 0.48, 0.70, 0.92])
    w2 = path([0.11, 0.22, 0.38, 0.56, 0.75])
    w3 = path([0.15, 0.30, 0.52, 0.78, 1.05])
    R = rndpath(2202, 6, 0.08, 1.0)
    M = np.zeros((FR, NH))
    for i in range(FR):
        env = (1.00 * gauss_l(c1[i], w1[i]) + 0.72 * gauss_l(c2[i], w2[i])
               + 0.50 * gauss_l(c3[i], w3[i]))
        M[i] = env * (0.20 + 0.80 * R[i]) * n ** -0.35
    return mk(M)


def terra_dust():
    """PURPOSE: dust motes over a solid note -- a rock-steady low tone with sparse fixed
    grains far above it that thicken from a handful to a swarm."""
    core = gauss_l(1.6, 1.05) * n ** -0.3
    core /= core.max()
    pos = rndpath(2303, 5, 0.0, 1.0)
    amp = rndpath(2304, 5, 0.25, 1.0)
    thr = path([0.950, 0.918, 0.870, 0.800, 0.705, 0.580])
    M = np.zeros((FR, NH))
    hf = shelf(430.0, 1.6) * np.clip(np.log2(n / 5.0) / 1.2, 0, 1)
    for i in range(FR):
        g = np.clip((pos[i] - thr[i]) / 0.030, 0, 1)
        M[i] = core + 0.62 * g * amp[i] * hf
    return mk(M)


def terra_hiss_chord():
    """PURPOSE: riser payoff -- flat metallic hiss that condenses onto a major-triad partial
    set (4:5:6), so a sweep resolves into a chord instead of just stopping."""
    a = ss(np.clip((X - 0.10) / 0.85, 0, 1))
    R = rndpath(2405, 6, 0.06, 1.0)
    chord = np.zeros(NH)
    for base in (4, 5, 6, 8):
        for k in range(1, 130):
            idx = base * k
            if idx <= NH:
                chord[idx - 1] = max(chord[idx - 1], 1.0 / (k ** 1.05))
    broad = None
    M = np.zeros((FR, NH))
    for i in range(FR):
        wf = 25.0 * (1.0 - a[i]) ** 1.4
        w0 = int(np.floor(wf))
        fw = wf - w0
        cs = ((1.0 - fw) * boxsmooth(chord[None, :], 2 * w0 + 1)[0]
              + fw * boxsmooth(chord[None, :], 2 * w0 + 3)[0])
        cs = cs / max(cs.max(), 1e-9)
        sel = (1.0 - a[i]) + a[i] * (0.02 + 0.98 * cs)
        broad = shelf(560.0 * (1.0 - a[i]) + 150.0 * a[i], 2.0)
        M[i] = R[i] * broad * sel * n ** (-0.55 - 0.55 * a[i])
    return mk(M)


def terra_comb_drift():
    """PURPOSE: flanged noise bed -- a LINEAR comb whose teeth spread from 2.5 to 60
    harmonics apart while the wash above it closes down; slow, seasick texture."""
    R = rndpath(2506, 6, 0.09, 1.0)
    sp = path([2.5, 4.0, 7.0, 12.0, 22.0, 38.0, 60.0], log=True)
    dp = path([0.20, 0.45, 0.70, 0.88, 1.00, 1.00, 0.90])
    sh = path([1.0, 1.5, 2.1, 2.9, 3.6, 4.2, 4.6])
    cut = path([430, 370, 305, 240, 178, 125, 82], log=True)
    M = np.zeros((FR, NH))
    for i in range(FR):
        comb = (0.5 + 0.5 * np.cos(2 * np.pi * n / sp[i])) ** sh[i]
        M[i] = R[i] * ((1.0 - dp[i]) + dp[i] * comb) * shelf(cut[i], 2.2) * n ** -0.45
    return mk(M)


def terra_density():
    """PURPOSE: a literal density fader -- partials per OCTAVE climb from under one to fully
    saturated, so one frame knob takes you from a sparse chime-wash to a solid noise wall."""
    d = path([0.85, 1.4, 2.3, 3.8, 6.2, 10.0, 16.0], log=True)
    gw = path([0.105, 0.120, 0.145, 0.185, 0.250, 0.350, 0.520])
    A = rndpath(2607, 5, 0.30, 1.0)
    M = np.zeros((FR, NH))
    for i in range(FR):
        u = np.mod(d[i] * L2, 1.0)
        dist = np.minimum(u, 1.0 - u)
        g = np.exp(-0.5 * (dist / gw[i]) ** 2)
        M[i] = g * A[i] * n ** -0.88
    return mk(M)


def terra_tone_noise():
    """PURPOSE: the round trip -- a resonant tone dissolves into full noise and re-forms as a
    different tone (an octave stack); for transitions that come out somewhere else."""
    ns = np.sin(np.pi * X) ** 1.25
    a = ss(X)
    R = rndpath(2708, 6, 0.06, 1.0)
    toneA = n ** -1.35 * (1.0 + 3.0 * np.exp(-0.5 * ((n - 7.0) / 2.2) ** 2))
    toneA /= toneA.max()
    toneB = np.zeros(NH)
    for j in range(0, 10):
        toneB += 2.0 ** (-0.33 * j) * gauss_l(2.0 ** j, 0.075)
    toneB /= toneB.max()
    noise = shelf(170.0, 1.8) * n ** -0.75
    M = np.zeros((FR, NH))
    for i in range(FR):
        tone = (1 - a[i]) * toneA + a[i] * toneB
        M[i] = (1.0 - 0.92 * ns[i]) * tone + 0.92 * ns[i] * noise * R[i]
    return mk(M)


def terra_sandstorm():
    """PURPOSE: a scanning sand sweep -- a smeared, random-walk texture seen through a window
    that climbs the spectrum and widens from a narrow whistle to a full storm."""
    c = path([4.0, 9.0, 22.0, 60.0, 165.0, 400.0, 650.0], log=True)
    w = path([0.32, 0.46, 0.72, 1.15, 1.80, 2.70, 3.90])
    W = rndpath(2809, 8, -1.0, 1.0)
    walk = np.cumsum(W, axis=1)
    walk = walk - walk.mean(axis=1, keepdims=True)
    walk = walk / np.maximum(np.abs(walk).max(axis=1, keepdims=True), 1e-9)
    tex = np.exp(1.9 * walk)
    M = np.zeros((FR, NH))
    for i in range(FR):
        M[i] = tex[i] * gauss_l(c[i], w[i])
    return mk(M)


def terra_pollen():
    """PURPOSE: forty isolated spectral grains that migrate to new positions -- a glittering,
    almost-empty granular sparkle for tops and IDM detail."""
    rng = np.random.default_rng(2910)
    K = 40
    pa = np.exp(rng.uniform(np.log(2.0), np.log(62.0), K))
    pb = np.exp(rng.uniform(np.log(11.0), np.log(950.0), K))
    amp = rng.uniform(0.30, 1.0, K)
    M = np.zeros((FR, NH))
    anchor = gauss_l(1.0, 0.45)
    for i in range(FR):
        u = ss(X[i])
        pos = np.exp(np.log(pa) * (1 - u) + np.log(pb) * u)
        acc = (0.60 - 0.45 * u) * anchor
        for p, g in zip(pos, amp):
            hw = 1.30 + 0.021 * p
            acc += g * np.exp(-0.5 * ((n - p) / hw) ** 2)
        M[i] = acc
    return mk(M)


def terra_sieve():
    """PURPOSE: IDM gapping -- two interfering modular sieves punch moving families of holes
    in a fixed noise field; irregular, rhythmic-sounding spectra that never repeat."""
    k1 = path([2.0, 2.45, 3.0, 3.7, 4.5, 5.5, 6.7], log=True)
    k2 = path([3.3, 4.0, 4.8, 5.8, 7.0, 8.4, 10.1], log=True)
    r1 = path([0.30, 0.27, 0.24, 0.21, 0.18, 0.16, 0.14])
    off = path([0.0, 0.10, 0.22, 0.35, 0.48, 0.60, 0.70])
    A = rndpath(3011, 5, 0.28, 1.0)
    M = np.zeros((FR, NH))
    for i in range(FR):
        u1 = np.mod(n / k1[i] + off[i], 1.0)
        u2 = np.mod(n / k2[i] - 0.5 * off[i], 1.0)
        d1 = np.minimum(u1, 1 - u1)
        d2 = np.minimum(u2, 1 - u2)
        g1 = np.clip((r1[i] - d1) / 0.11 + 1.0, 0.075, 1.0)
        g2 = np.clip((r1[i] - d2) / 0.13 + 1.0, 0.110, 1.0)
        M[i] = A[i] * g1 * g2 * n ** -0.55 * shelf(190.0, 2.2)
    return mk(M)


def terra_velvet():
    """PURPOSE: no hard edges -- a smooth undulating spectral ripple (three cosines in
    log-frequency, drifting) for soft breathing texture pads."""
    wr = np.array([0.30, 0.55, 0.95, 1.55, 2.40])          # fixed ripple rates, cyc/octave
    ph = np.array([0.0, 1.9, 3.4, 5.1, 2.3])
    ga = [path([1.9, 1.2, 0.5, 0.9, 1.8, 1.1]),            # each rate swells at its own time
          path([0.4, 1.6, 2.0, 1.1, 0.4, 1.5]),
          path([1.3, 0.5, 1.4, 2.1, 1.2, 0.4]),
          path([0.2, 0.9, 1.7, 0.6, 1.9, 2.0]),
          path([0.8, 0.3, 1.0, 1.8, 0.7, 1.6])]
    drift = path([0.0, 0.5, 1.0, 1.5, 2.0, 2.5])           # slow common phase drift
    cut = path([300, 420, 560, 420, 300, 200], log=True)
    tilt = path([1.05, 0.92, 0.80, 0.72, 0.66, 0.62])
    M = np.zeros((FR, NH))
    for i in range(FR):
        rip = np.zeros(NH)
        for j in range(5):
            rip += ga[j][i] * np.cos(2 * np.pi * wr[j] * L2 + ph[j] + drift[i])
        M[i] = np.exp(rip - 2.4) * shelf(cut[i], 1.7) * n ** (-tilt[i])
    return mk(M)


def terra_shatter():
    """PURPOSE: a solid noise wall that FRACTURES -- fixed random shards are carved out and
    widened until only splinters are left; downlifters, impacts, glass-break transitions."""
    F = rndpath(3212, 4, 0.0, 1.0)
    S = boxsmooth(F, 11)
    S = (S - S.min(axis=1, keepdims=True)) / np.maximum(
        S.max(axis=1, keepdims=True) - S.min(axis=1, keepdims=True), 1e-9)
    thr = path([0.00, 0.14, 0.34, 0.56, 0.78, 0.98])
    dep = path([0.00, 0.40, 0.72, 0.92, 1.00, 1.00])
    nc = path([950, 620, 330, 145, 52, 13], log=True)       # fracture front descends
    cut = path([470, 395, 325, 250, 172, 98], log=True)
    tx = rndpath(3213, 4, 0.55, 1.0)
    keep = np.clip(np.log2(n / 2.2) / 1.1, 0, 1)            # protect the low end
    M = np.zeros((FR, NH))
    for i in range(FR):
        front = np.clip(np.log2(n / nc[i]) / 0.9 + 1.0, 0, 1)
        car = np.clip((thr[i] * front - S[i]) / 0.07, 0, 1) * keep
        M[i] = shelf(cut[i], 2.3) * n ** -0.50 * tx[i] * (1.0 - dep[i] * car)
    return mk(M)


TABLES = [
    ("TERRA VOWEL ARC",   terra_vowel_arc),
    ("TERRA THROAT",      terra_throat),
    ("TERRA WHISPER",     terra_whisper),
    ("TERRA NASAL",       terra_nasal),
    ("TERRA CHOIR",       terra_choir),
    ("TERRA CHILD",       terra_child),
    ("TERRA CONSONANT",   terra_consonant),
    ("TERRA VOX GLASS",   terra_vox_glass),
    ("TERRA DIPHTHONG",   terra_diphthong),
    ("TERRA GROWL",       terra_growl),
    ("TERRA VOX MASK",    terra_vox_mask),
    ("TERRA CHANTER",     terra_chanter),
    ("TERRA GRAIN BED",   terra_grain_bed),
    ("TERRA CLOUD",       terra_cloud),
    ("TERRA DUST",        terra_dust),
    ("TERRA HISS CHORD",  terra_hiss_chord),
    ("TERRA COMB DRIFT",  terra_comb_drift),
    ("TERRA DENSITY",     terra_density),
    ("TERRA TONE NOISE",  terra_tone_noise),
    ("TERRA SANDSTORM",   terra_sandstorm),
    ("TERRA POLLEN",      terra_pollen),
    ("TERRA SIEVE",       terra_sieve),
    ("TERRA VELVET",      terra_velvet),
    ("TERRA SHATTER",     terra_shatter),
]

# ══════════════════════════════════════════════════════════════════════════════════════
# fb606 — THE MERGED TEN. The bank shipped as eight generated folders; the browser showed a
# DIFFERENT set for its built-ins, so the same sound had two names depending on where you
# looked. The two sets are now ONE ten-folder taxonomy, and every module declares which of
# the ten each of its tables belongs to. gate.py reads CATEGORY and never guesses from the
# module name — a table with no entry here is a hard error, not a silent "GEN_WHATEVER".
# ══════════════════════════════════════════════════════════════════════════════════════

# Formant tables and the granular/breath textures that share their anatomy — one folder.
# The texture half stays here rather than moving to Cinematic: it is built from the same
# vocal-tract material and a user hunting a breath bed looks for it next to the choir.
CATEGORY = {nm: "Vocal" for nm, _ in TABLES}


if __name__ == "__main__":
    _fp, _names = {}, []
    for _nm, _fn in TABLES:
        _fr = _fn()
        print(wtlib.selfcheck(_nm, _fr)[1])
        _fp[_nm] = wtlib.fingerprint(_fr)
        _names.append(_nm)
    _pairs = []
    for _i in range(len(_names)):
        for _j in range(_i + 1, len(_names)):
            _pairs.append((wtlib.distance(_fp[_names[_i]], _fp[_names[_j]]),
                           _names[_i], _names[_j]))
    _pairs.sort()
    print("\nCLOSEST PAIRS")
    for _d, _x, _y in _pairs[:8]:
        print("  %6.2f dB   %-18s <-> %s" % (_d, _x, _y))
    print("MIN PAIRWISE DISTANCE = %.2f dB" % _pairs[0][0])
