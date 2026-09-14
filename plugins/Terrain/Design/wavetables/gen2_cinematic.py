"""
gen2_cinematic.py — Terrain factory bank, CINEMATIC: new candidates for the 500 (fb638).

Huge, dark, wide and evolving. Dense-and-wide grounds, drones, brass and choir swells, risers that live
inside the table, tension clusters, film-score dread, and a few luminous ones. Everything is computed from
first principles (additive spectra or time-domain constructions band-limited at 8x); nothing third-party
is read.

THE JOURNEY RULE: frame 0 is playable, the frame axis is a PROCESS (pressure, drive, bow position, a cluster
closing in, a shock steepening), and the last frame is something wild.

Pitch notes: tables built on a VIRTUAL ROOT (a harmonic family on every b-th harmonic) sound above the
played note: base 2 = +1 oct, base 4 = +2 oct, base 10 = +3 oct + major third, base 24 = +4 oct + minor sixth
(approx). Each docstring says so where it applies.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, wtlib
from scipy.ndimage import gaussian_filter1d

F, SZ, NH = wtlib.FRAMES, wtlib.SIZE, wtlib.NH
K = wtlib.n_ax.astype(float)             # harmonic numbers 1..1024
KK = K[None, :]
U = np.linspace(0.0, 1.0, F)             # the frame axis
UU = U[:, None]
OS = 8
N8 = SZ * OS
T = wtlib.t
T8 = np.arange(N8) / N8
NMAX = 1000
ODD = (wtlib.n_ax % 2 == 1)
EVN = ~ODD
SINE_PH = np.full(NH, -np.pi / 2)        # every partial in sine phase (saw-like shapes for 1/k)


# ── kit ──────────────────────────────────────────────────────────────────────────────────────
def col(v):
    return np.asarray(v, float).reshape(-1, 1)


def sm(x):
    x = np.clip(np.asarray(x, float), 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def ramp(x, a, b):
    return np.clip((np.asarray(x, float) - a) / (b - a), 0.0, 1.0)


def geo(a, b, x):
    return a * (b / a) ** np.asarray(x, float)


def db(x):
    return 10.0 ** (np.asarray(x, float) / 20.0)


def glog(c, w):
    """Gaussian bump in log-frequency, centre c (harmonic number), width w (natural-log units)."""
    return np.exp(-0.5 * ((np.log(KK) - np.log(col(c))) / col(w)) ** 2)


def lp(fc, q):
    return (1.0 + (KK / col(fc)) ** 2) ** (-col(q))


def hp(fc, s=3.0):
    return 1.0 / (1.0 + (col(fc) / KK) ** s)


def walk(rng, n, sigma, wrap=False):
    """(F, n) smooth random walks along the frame axis, unit std per column."""
    w = rng.standard_normal((F, n))
    w = gaussian_filter1d(w, sigma, axis=0, mode='wrap' if wrap else 'reflect')
    w = w - w.mean(axis=0, keepdims=True)
    return w / (w.std(axis=0, keepdims=True) + 1e-12)


def schroeder(m):
    """Schroeder's low-crest phase for a (mean) power spectrum: phi_k = -2pi sum_{l<k} (k-l) p_l.
    One phase set per table, fixed across frames. Dense flat grounds come out as an in-cycle chirp."""
    m = np.asarray(m, float)
    p = (m ** 2).mean(axis=0) if m.ndim == 2 else m ** 2
    p = p / max(p.sum(), 1e-30)
    P = np.concatenate([[0.0], np.cumsum(p)[:-1]])
    Q = np.concatenate([[0.0], np.cumsum(K * p)[:-1]])
    return -2.0 * np.pi * (K * P - Q)


def to_cycles(m, phase=None):
    m = np.nan_to_num(np.asarray(m, float), nan=0.0, posinf=0.0, neginf=0.0)
    ph = wtlib.PHASE if phase is None else np.asarray(phase, float)
    S = np.zeros((m.shape[0], SZ // 2 + 1), complex)
    S[:, 1:NH + 1] = m * np.exp(1j * ph)
    S[:, NMAX + 1:] = 0.0
    return np.fft.irfft(S, n=SZ, axis=1)


def spec(m, phase=None):
    return rolled(to_cycles(m, phase))


def bl8(y):
    """8x-oversampled cycles (F, 16384) -> (F, 2048), harmonics 1..1000 kept (alias-free)."""
    Y = np.fft.rfft(y, axis=1)
    X = np.zeros((y.shape[0], SZ // 2 + 1), complex)
    X[:, 1:NMAX + 1] = Y[:, 1:NMAX + 1]
    return np.fft.irfft(X, n=SZ, axis=1)


def tdone(y8):
    return wtlib.finalize(bl8(y8))


def cyc8(mags, phase):
    """One magnitude row -> one 8x cycle (for time-domain processing of an additive source)."""
    S = np.zeros(N8 // 2 + 1, complex)
    S[1:NH + 1] = np.asarray(mags, float) * np.exp(1j * np.asarray(phase, float))
    S[NMAX + 1:] = 0.0
    x = np.fft.irfft(S, n=N8)
    return x / np.abs(x).max()


def put(row, idx, amp):
    idx = np.asarray(idx)
    amp = np.broadcast_to(np.asarray(amp, float), idx.shape)
    ok = (idx >= 1) & (idx <= NMAX)
    if ok.any():
        np.add.at(row, idx[ok].astype(np.intp) - 1, amp[ok])


def putf(row, x, amp):
    """Add amp at FRACTIONAL harmonic positions x, split linearly between the two neighbour bins."""
    x = np.asarray(x, float)
    amp = np.broadcast_to(np.asarray(amp, float), x.shape)
    lo = np.floor(x)
    fr = x - lo
    put(row, lo.astype(int), amp * (1.0 - fr))
    put(row, lo.astype(int) + 1, amp * fr)


def cdist(t, t0):
    """Circular signed distance on the cycle, in [-0.5, 0.5)."""
    return (t - t0 + 0.5) % 1.0 - 0.5


def family(base, amps_fn, nmax=NMAX):
    """Bins and amplitudes of a harmonic family on a virtual root `base` (partials j*base)."""
    j = np.arange(1, nmax // base + 1)
    return j * base, amps_fn(j)


# formant sets (Hz, dB, Hz) — the public Csound tenor/bass table, plus a hum of our own
VOW = {
    'm': ([260, 1100, 2300, 2900, 3500], [0, -28, -30, -32, -40], [60, 150, 200, 200, 200]),
    'u': ([350, 600, 2700, 2900, 3300], [0, -20, -17, -14, -26], [40, 60, 100, 120, 120]),
    'o': ([400, 800, 2600, 2800, 3000], [0, -10, -12, -12, -26], [40, 80, 100, 120, 120]),
    'a': ([650, 1080, 2650, 2900, 3250], [0, -6, -7, -8, -22], [80, 90, 120, 130, 140]),
    'e': ([400, 1700, 2600, 3200, 3580], [0, -14, -12, -14, -20], [70, 80, 100, 120, 120]),
    'i': ([290, 1870, 2800, 3250, 3540], [0, -15, -18, -20, -30], [40, 90, 100, 120, 120]),
}


def vowel_path(keys):
    """keys = [(u, vowel, fscale, bwscale, dbtilt)] -> per-frame (F,5) freq, amp dB, bandwidth."""
    us = np.array([k[0] for k in keys])
    Fq = np.array([np.log(np.array(VOW[k[1]][0]) * k[2]) for k in keys])
    Am = np.array([np.array(VOW[k[1]][1], float) * k[4] for k in keys])
    Bw = np.array([np.log(np.array(VOW[k[1]][2]) * k[3]) for k in keys])
    fq = np.stack([np.interp(U, us, Fq[:, i]) for i in range(5)], 1)
    am = np.stack([np.interp(U, us, Am[:, i]) for i in range(5)], 1)
    bw = np.stack([np.interp(U, us, Bw[:, i]) for i in range(5)], 1)
    return np.exp(fq), am, np.exp(bw)


def formant_env(fhz, fq, am, bw, order=1.0):
    """fhz (NH,) partial frequencies; fq/am/bw (F,5) -> (F,NH) magnitude envelope. order 1 = one resonance
    (Lorentzian magnitude), order 2 = two cascaded (steeper skirts, deeper valleys between formants)."""
    env = np.zeros((F, len(fhz)))
    for i in range(fq.shape[1]):
        x = (fhz[None, :] - fq[:, i:i + 1]) / (0.5 * bw[:, i:i + 1])
        env += db(am[:, i:i + 1]) * (1.0 + x * x) ** (-0.5 * order)
    return env


# ══════════════════════════════════════════════════════════════════════════════════════════════
#  THE TABLES
# ══════════════════════════════════════════════════════════════════════════════════════════════

def abyssal_pressure():
    """SUBBY. A sine sub (h1, h2 -12 dB, h3 -22 dB) under a DENSE GROUND of every harmonic 4..1000, each within
    +-2.5 dB of the ground's level. Frame axis = PRESSURE: the ground rises from -50 to -18 dB under the sub, its
    low edge sinks from h180 to h4 so the roar closes in on the fundamental, its ripple crawls (a slow random walk
    per partial) and its tilt straightens to flat. Schroeder phase: the ground is drawn as an in-cycle chirp."""
    rng = np.random.default_rng(101)
    s = sm(U)
    rip = walk(rng, NH, 6.0) * 2.5
    lvl = -50.0 + 32.0 * s ** 1.15
    tilt = -3.0 * (1.0 - s)
    ground = db(col(lvl) + rip + col(tilt) * np.log2(KK / 180.0)) * hp(geo(180.0, 4.0, s), 4.0)
    ground[:, :3] = 0.0
    m = ground.copy()
    m[:, 0] += 1.0
    m[:, 1] += db(-12)
    m[:, 2] += db(-22)
    return spec(m, schroeder(ground))


def tectonic_grind():
    """SUBBY, time-built. A sine sub plus a triangle-WAVEFOLDED copy of itself, with GRAVEL (seeded noise that
    only lives where the fold is folding). Frame axis = the folder's gain (1.5 -> 14) and an asymmetric bias
    (0 -> 0.45) grind harder while the dry sine stays underneath: the fundamental survives, the top turns to
    crushed rock. Last frame: a sine carrying a serrated fold wall and a gravel hiss."""
    s = sm(U)
    rng = np.random.default_rng(1906)
    x = np.sin(2 * np.pi * T8)[None, :]
    g = geo(1.5, 14.0, s)[:, None]
    b = (0.45 * s ** 1.3)[:, None]
    v = g * x + b
    fold = -(4.0 * np.abs(((v + 1.0) / 4.0) % 1.0 - 0.5) - 1.0)
    fold = fold - fold.mean(axis=1, keepdims=True)
    mix = (0.15 + 0.72 * s ** 0.9)[:, None]
    grav = rng.standard_normal(N8)[None, :] * np.abs(fold) ** 2 * (0.05 + 0.9 * s ** 1.4)[:, None]
    return rolled(bl8(x + mix * fold + grav))


def braam_wall():
    """BRAAM. A brass POWER CHORD on virtual roots 2 and 3 (an octave and a twelfth above the played note; the
    bins between are empty) with a formant hump, sine phase. Frame axis = the BLAST: the brass brightens (lowpass
    at partial 3 -> 150) and is driven into an asymmetric tanh, gain 0.6 -> 22; the drive's intermodulation fills
    the empty bins and a DIFFERENCE-TONE SUB at the played note appears. The last third BLATS: a buzz rides the
    clipped plateaus (a ringing formant climbing h40 -> h90) until the wall is torn."""
    s = sm(U)
    y = np.zeros((F, N8))
    fc = geo(3.0, 150.0, s ** 0.8)
    g = geo(0.6, 22.0, s)
    bias = 0.35 * s ** 1.2
    cen = geo(7.0, 16.0, s)
    blat = sm(ramp(U, 0.6, 1.0))
    qb = geo(40.0, 90.0, blat)
    for i in range(F):
        a = np.zeros(NH)
        for base, gain in ((2, 1.0), (3, 0.8)):
            j = np.arange(1, NMAX // base + 1)
            k = j * base
            amp = gain * j ** -0.8 / (1.0 + (j / fc[i]) ** 2) \
                * (1.0 + 2.0 * np.exp(-0.5 * ((np.log(k) - np.log(cen[i])) / 0.4) ** 2))
            a[k - 1] += amp
        x = cyc8(a, SINE_PH)
        yi = np.tanh(g[i] * (x + bias[i])) - np.tanh(g[i] * bias[i])
        if blat[i] > 0:
            yi = yi + 0.45 * blat[i] * np.sin(2 * np.pi * qb[i] * T8) * np.abs(yi) ** 3
        y[i] = yi
    return rolled(bl8(y))


def horn_rip():
    """BRASS SWELL on a virtual root 2 (sounds +1 oct). Frame axis = DYNAMICS with brass's nonlinear steepening:
    harmonic j's level is L^((j-1)/3) * j^-0.75, L = 0.04 -> 1, so pp is a mellow horn and fff blares flat to h400.
    The last third goes CUIVRE: a nasal buzz formant near h24-40 swells and a rasp carpet fills the empty odd bins."""
    L = geo(0.04, 1.0, ramp(U, 0.0, 0.75) ** 0.9)
    j = np.arange(1, NMAX // 2 + 1)
    m = np.zeros((F, NH))
    amp = (L[:, None] ** ((j[None, :] - 1) / 3.0)) * j[None, :] ** -col(0.75 - 0.3 * ramp(U, 0.6, 1.0))
    amp *= 1.0 + 1.2 * np.exp(-0.5 * ((np.log(j) - np.log(5.0)) / 0.5) ** 2)[None, :]
    m[:, 2 * j - 1] = amp
    c = ramp(U, 0.6, 1.0)
    buzz = 1.0 + (6.0 * c)[:, None] * glog(geo(24.0, 40.0, c), np.full(F, 0.22))
    m *= buzz
    rng = np.random.default_rng(404)
    rasp = np.exp(0.8 * rng.standard_normal(NH))[None, :] * KK ** -0.45 * col(db(-40 + 30 * c) * sm(4.0 * c))
    rasp[:, EVN] = 0.0
    return rolled(to_cycles(m + rasp, schroeder(m + rasp)))


def foghorn_cascade():
    """SUBBY END. A dark odd-heavy reed on a virtual root 4 (sounds +2 oct) through a fixed horn-bell formant.
    Frame axis = a PERIOD-DOUBLING cascade: the base-2 family (2, 6, 10, ...) grows in, then the base-1 odd family
    (1, 3, 5, ...), so the horn falls two octaves into its own undertones while the lowpass opens; the last
    quarter breaks into a roaring, ragged carpet."""
    s = sm(U)
    bell = 0.35 + glog(14.0, 0.28)[0] + 0.5 * glog(33.0, 0.2)[0]
    env = KK ** -1.0 * lp(geo(30.0, 1000.0, s), np.full(F, 0.8)) * bell[None, :]
    m = np.zeros((F, NH))
    b4 = (wtlib.n_ax % 4 == 0)
    j4 = wtlib.n_ax // 4
    reed = np.where(j4 % 2 == 1, 1.0, 0.14)
    m[:, b4] = env[:, b4] * reed[b4][None, :] * 4.0
    gA = sm(ramp(U, 0.22, 0.55))
    fA = (wtlib.n_ax % 4 == 2)
    m[:, fA] += env[:, fA] * col(2.2 * gA)
    gB = sm(ramp(U, 0.48, 0.80))
    m[:, ODD] += env[:, ODD] * col(1.6 * gB)
    gC = sm(ramp(U, 0.74, 1.0))
    rng = np.random.default_rng(55)
    rough = db(walk(rng, NH, 2.5) * 7.0 * col(gC))
    m = m * rough + col(0.32 * gC) * KK ** -0.1 * bell[None, :] * np.exp(0.6 * rng.standard_normal(NH))[None, :]
    return spec(m, chirp_ph(1200))


def humming_to_howling():
    """CHOIR SWELL. Formant choir (the public tenor/bass formant numbers, f0 taken as 110 Hz, each formant a
    double resonance so the valleys between them go deep): hum 'm' -> 'oo' -> 'oh' -> 'ah' -> 'eh', voices entering
    an octave, a twelfth and two octaves above (virtual roots 2, 3, 4), the source brightening 1/j^1.3 -> 1/j^0.6.
    The last frames HOWL: formants pushed up 80 %, bandwidths x3.5, an ensemble smear and a breath carpet."""
    s = sm(U)
    keys = [(0.00, 'm', 1.0, 0.9, 1.0), (0.18, 'u', 1.0, 0.9, 1.0), (0.36, 'o', 1.0, 0.9, 1.0),
            (0.56, 'a', 1.0, 1.0, 0.9), (0.74, 'e', 1.1, 1.3, 0.7), (0.88, 'a', 1.4, 2.2, 0.5),
            (1.00, 'a', 1.8, 3.5, 0.3)]
    fq, am, bw = vowel_path(keys)
    env = formant_env(K * 110.0, fq, am, bw, order=2.0) + 0.002
    m = np.zeros((F, NH))
    tilt = geo(1.3, 0.6, s)
    for base, ent in ((1, -1.0), (2, 0.28), (3, 0.50), (4, 0.66)):
        g = sm(ramp(U, ent, ent + 0.18)) if ent >= 0 else np.ones(F)
        idx = np.arange(base, NMAX + 1, base)
        jj = idx / base
        m[:, idx - 1] += col(g) * jj[None, :] ** -col(tilt)
    m *= env
    wid = 0.05 + 1.5 * sm(ramp(U, 0.7, 1.0))
    x = np.arange(-8, 9)
    for i in range(F):
        kern = np.exp(-0.5 * (x / wid[i]) ** 2)
        m[i] = np.sqrt(np.convolve(m[i] ** 2, kern / kern.sum(), mode='same'))
    rng = np.random.default_rng(606)
    breath = np.exp(0.7 * rng.standard_normal(NH))[None, :] * (env + 0.012) * col(db(-48 + 42 * s ** 2.0))
    m = m / np.maximum(m.max(axis=1, keepdims=True), 1e-12) + breath
    return spec(m, schroeder(m))


def bow_to_the_bridge():
    """STRINGS. Helmholtz bowed-string spectrum 1/k * |sin(pi k beta)| (the bow position beta nulls every
    1/beta-th harmonic) through a fixed body (air, wood and bridge-hill resonances). Frame axis = the BOW MOVES
    from the middle of the string (beta 0.45: hollow, nearly odd-only flautando, lowpassed) to hard sul
    ponticello (beta 0.011: flat, glassy, notches 90 apart); the last third is OVERPRESSURE: a scratch ripple
    (+-9 dB, per partial) and a grinding carpet."""
    s = sm(U)
    beta = geo(0.45, 0.011, s)
    a = KK ** -1.0 * np.abs(np.sin(np.pi * KK * col(beta))) / (np.pi * col(beta)) + 0.004 * KK ** -1.0
    body = 0.25 + 1.0 * glog(3.2, 0.22)[0] + 0.8 * glog(6.8, 0.2)[0] + 0.6 * glog(11.0, 0.18)[0] \
        + 0.9 * glog(40.0, 0.3)[0] + 0.5 * glog(95.0, 0.25)[0]
    m = a * body[None, :] * lp(geo(14.0, 1000.0, s ** 0.8), np.full(F, 1.0))
    c = sm(ramp(U, 0.62, 1.0))
    rng = np.random.default_rng(77)
    m = m * db(walk(rng, NH, 1.2) * 9.0 * col(c)) + col(0.02 * c) * KK ** -0.35 * body[None, :]
    return spec(m, schroeder(m))


def semitone_vise():
    """TENSION CLUSTER. A dark drone on the played note, and above it a cluster of harmonic families on virtual
    roots 20..29 (neighbours ~70 cents apart, sounding ~4.5 oct up) that ENTER one by one and swell from -24 dB
    to above the drone. Frame axis = the vise closing; the last frames fill every root 16..34 into a dread wall."""
    s = sm(U)
    m = KK ** -1.3 * lp(np.full(F, 20.0), np.full(F, 1.3)) + db(-54) * KK ** -0.4
    order = [24, 25, 23, 26, 22, 27, 21, 28, 20, 29]
    lvl = db(-24 + 26 * s)
    for v, b in enumerate(order):
        g = sm(ramp(U, 0.04 + 0.066 * v, 0.16 + 0.066 * v)) * lvl
        idx = np.arange(b, NMAX + 1, b)
        jj = idx / b
        m[:, idx - 1] += col(g) * (jj ** -0.9 * np.exp(-(idx / 700.0) ** 2))[None, :]
    w = sm(ramp(U, 0.78, 1.0))
    for b in list(range(16, 20)) + list(range(30, 35)):
        idx = np.arange(b, NMAX + 1, b)
        jj = idx / b
        m[:, idx - 1] += col(w * lvl * 0.8) * (jj ** -0.8)[None, :]
    return spec(m, schroeder(m))


def dread_chord():
    """CHORD IN A CYCLE. Harmonic families on virtual roots: 5 and 10 (the chord root, +2/+3 oct and a major
    third above the played note), then 12 (minor third), 15 (fifth), 18 (minor seventh) — a minor-seventh swell —
    then it CURDLES: 14 (a tritone), 21 (a flat ninth), 11 (a neutral second) enter while the lowpass tears open
    from h12 to h1000 and every family flattens from 1/j^1.4 to 1/j^0.4."""
    s = sm(U)
    m = np.zeros((F, NH))
    fam = [(5, -1.0, 0.8), (10, -1.0, 1.0), (12, 0.10, 0.85), (15, 0.22, 0.7), (18, 0.36, 0.6),
           (14, 0.56, 0.75), (21, 0.68, 0.65), (11, 0.80, 0.6)]
    tilt = 1.4 - 1.0 * s
    for b, ent, gain in fam:
        g = (sm(ramp(U, ent, ent + 0.14)) if ent >= 0 else np.ones(F)) * gain
        idx = np.arange(b, NMAX + 1, b)
        jj = idx / b
        m[:, idx - 1] += col(g) * jj[None, :] ** -col(tilt)
    m *= lp(geo(12.0, 1000.0, s), np.full(F, 1.1))
    return spec(m, schroeder(m))


def black_monolith():
    """MICROPOLYPHONY. A sub (h1, h2) and a mass of harmonic families on virtual roots 3..24 that enter outward
    from 8 (7, 9, 6, 10 ...) — each voice with its own slowly wandering level (+-6 dB) and brightness. Frame axis
    = the mass assembling: one voice over a sub becomes a three-octave cluster of 22 independent voices."""
    s = sm(U)
    rng = np.random.default_rng(2001)
    order = [8, 7, 9, 6, 10, 5, 11, 12, 4, 13, 14, 15, 16, 3, 17, 18, 19, 20, 21, 22, 23, 24]
    lv = walk(rng, len(order), 5.0)
    br = walk(rng, len(order), 7.0)
    m = np.zeros((F, NH))
    m[:, 0] = 1.0
    m[:, 1] = db(-10)
    for v, b in enumerate(order):
        ent = 0.8 * v / len(order) - 0.04
        g = sm(ramp(U, ent, ent + 0.12)) * db(-8 + 6 * lv[:, v])
        fc = geo(2.0, 40.0, 0.5 + 0.35 * br[:, v] * 0.5 + 0.35 * s)
        idx = np.arange(b, NMAX + 1, b)
        jj = idx / b
        m[:, idx - 1] += col(g) * jj[None, :] ** -0.9 / (1.0 + (jj[None, :] / col(fc)) ** 2) ** 0.7
    return spec(m)


def thunderhead():
    """STORM. A pink random-magnitude ground whose every partial rumbles on its own slow walk (+-5 dB), under a
    -12 dB/oct lowpass that climbs from h3 to h500. Frame axis = the storm building; LIGHTNING flares (five
    designed flashes that throw the cutoff up 6x for a few frames) strike more often toward the end."""
    s = sm(U)
    rng = np.random.default_rng(9090)
    base = np.exp(0.7 * rng.standard_normal(NH)) * K ** -0.5
    rum = db(walk(rng, NH, 3.0) * 5.0)
    fl = np.zeros(F)
    for c, h in ((0.42, 0.6), (0.61, 0.8), (0.76, 1.0), (0.88, 1.0), (0.965, 1.0)):
        fl += h * np.exp(-0.5 * ((U - c) / 0.012) ** 2)
    cut = geo(3.0, 500.0, s) * (1.0 + 6.0 * fl)
    m = base[None, :] * rum * (1.0 + (KK / col(cut)) ** 2) ** -1.0
    m[:, :6] *= 2.0
    return spec(m)


def tinnitus_aftermath():
    """DREAD, wild by the end. Frame 0 is a dense, ragged low rumble (every partial +-9 dB at random). Frame axis
    = the blast's AFTERMATH: the rumble's middle hollows out (a notch that widens across h4..h300), a ringing whine
    cluster at h171..h175 (+ octave and twelfth) rises from -60 dB to on top of everything and a narrow band of
    air at h350 swells. Ends as a sub, a black gap, and the whine."""
    s = sm(U)
    rng = np.random.default_rng(1313)
    rum = np.exp(1.0 * rng.standard_normal(NH))[None, :] * KK ** -0.9 * lp(np.full(F, 80.0), np.full(F, 1.0))
    hole = 1.0 - 0.998 * col(s ** 0.8) * np.exp(-0.5 * ((np.log(KK) - np.log(24.0)) / col(0.3 + 1.3 * s)) ** 2)
    m = rum * hole
    air = col(db(-46 + 20 * s)) * glog(350.0, 0.3) * np.exp(0.5 * rng.standard_normal(NH))[None, :]
    m = m + air
    wh = db(-60 + 64 * s ** 1.3)
    for k, a in zip(range(171, 176), (0.3, 0.7, 1.0, 0.8, 0.4)):
        m[:, k - 1] += a * wh
    for k, a in ((345, 0.2), (346, 0.35), (347, 0.2), (519, 0.12)):
        m[:, k - 1] += a * wh
    m[:, 0] += 0.6
    return spec(m)


def heartbeat_fibrillation():
    """TIME-BUILT DREAD. A low sine body and a lub-dub: two sine-filled bumps with a valve click on each onset.
    Frame axis = the heart RACES AND FAILS: the gap widens 0.10 -> 0.36, the bumps tighten and brighten, the clicks
    sharpen, then extra beats appear at seeded places until the last frame is fibrillation — nine jittered beats."""
    s = sm(U)
    rng = np.random.default_rng(71)
    ex_pos = rng.uniform(0, 1, 9)
    ex_amp = rng.uniform(0.45, 1.0, 9)
    ex_on = np.sort(rng.uniform(0.45, 0.97, 9))
    y = np.zeros((F, N8))
    for i in range(F):
        u = s[i]
        gap = 0.10 + 0.26 * u
        w = geo(0.065, 0.009, u)
        q = geo(2.5, 24.0, u)
        wc = geo(0.004, 0.0005, u)
        beats = [(0.2, 1.0), (0.2 + gap, 0.72)]
        for p, a, o in zip(ex_pos, ex_amp, ex_on):
            g = sm((U[i] - o) / 0.06)
            if g > 0:
                beats.append((p, a * g))
        yi = 0.35 * np.sin(2 * np.pi * T8)
        for t0, a in beats:
            d = cdist(T8, t0)
            yi += a * np.exp(-0.5 * (d / w) ** 2) * np.sin(2 * np.pi * q * d)
            yi += 0.35 * a * (0.2 + 1.2 * u) * (d / wc) * np.exp(-0.5 * (d / wc) ** 2)
        y[i] = yi
    return tdone(y)


def shepard_ascent():
    """RISER. Octave-spaced components (each with its fifth) glide up two octaves through a bell-shaped
    log-frequency window that itself climbs 3.6 octaves; thirds join halfway and every component spreads into
    a band. Frame axis = the riser: a soft hollow tone becomes a bright, dense sheet still climbing."""
    s = sm(U)
    m = np.zeros((F, NH))
    for i in range(F):
        u = s[i]
        sh = 2.0 * U[i]
        cen = np.log2(18.0) + 3.6 * u
        sig = 1.0 + 0.7 * u
        spread = 0.001 + 0.07 * u ** 1.3
        ints = [(1.0, 1.0), (1.5, 0.6)] + ([(1.25, 0.5 * sm((U[i] - 0.45) / 0.2))] if U[i] > 0.45 else [])
        for jn in range(-2, 9):
            f0 = 4.0 * 2.0 ** (jn + sh)
            for r, a in ints:
                f = f0 * r
                if f < 1 or f > NMAX:
                    continue
                e = np.exp(-0.5 * ((np.log2(f) - cen) / sig) ** 2) * a
                if e < 1e-5:
                    continue
                bw = max(spread * f, 0.3)
                offs = np.linspace(-2.5, 2.5, 21) * bw
                wts = np.exp(-0.5 * (offs / bw) ** 2)
                putf(m[i], f + offs, e * wts / wts.sum())
    m[:, 0] += 0.02
    m += db(-58) * KK ** -0.2 * col(sm(U))
    return spec(m, schroeder(m))


def reverse_cymbal_rise():
    """RISER. A sine body with a cymbal breathing behind it: a dense metallic partial field (random magnitudes with
    clustered hot spots) through a band-pass that sweeps from h12 to h700 — an 18 dB/oct skirt below the centre,
    and above it a slope that opens from 12 to 4 dB/oct. Frame axis = the reverse-cymbal swell: the cymbal rises
    from -30 dB and swallows the tone, the low end is sucked out, and the last frame is pure sizzle."""
    s = sm(U)
    rng = np.random.default_rng(8181)
    base = np.exp(1.0 * rng.standard_normal(NH))
    for c in rng.uniform(np.log(20), np.log(900), 14):
        base *= 1.0 + 3.0 * np.exp(-0.5 * ((np.log(K) - c) / 0.03) ** 2)
    c = geo(12.0, 700.0, s ** 1.1)
    below = (1.0 + (col(c) / KK) ** 2) ** -1.5
    above = (1.0 + (KK / col(c)) ** 2) ** -col(1.0 - 0.67 * s)
    shimmer = db(walk(rng, NH, 1.0) * 3.0)
    cym = base[None, :] * below * above * shimmer
    cym = cym / cym.max(axis=1, keepdims=True) * col(db(-30 + 30 * sm(ramp(U, 0.0, 0.6))))
    m = cym.copy()
    m[:, 0] += 1.0 - sm(ramp(U, 0.25, 0.85))
    return spec(m)


def aurora_curtain():
    """LUMINOUS. A soft sine-ish body under five glowing bands (h40..h420) that WAVE: each band's centre swings on
    its own slow sine, each band carries its own comb of folds. Frame axis = the aurora igniting: bands rise from
    -40 dB to above the body, widen, and fuse into a shimmering sheet by the last frame."""
    s = sm(U)
    m = KK ** -2.2 * 1.0
    m = np.repeat(m, F, axis=0)
    rng = np.random.default_rng(303)
    lvl = db(-40 + 44 * s ** 1.2)
    for i, c0 in enumerate([40.0, 75.0, 130.0, 230.0, 420.0]):
        rate = rng.uniform(1.0, 3.0)
        ph = rng.uniform(0, 2 * np.pi)
        c = c0 * 2.0 ** (0.5 * np.sin(2 * np.pi * rate * U + ph))
        w = 0.12 + 0.5 * s ** 1.5
        P = rng.uniform(2.5, 7.0)
        comb = (0.5 + 0.5 * np.cos(2 * np.pi * KK / P + col(6 * U))) ** 2
        m += col(lvl * rng.uniform(0.6, 1.0)) * glog(c, w) * (0.15 + 0.85 * comb) * (c0 / 40.0) ** -0.3
    return spec(m)


def starfield():
    """LUMINOUS. A sine with a quiet octave, and stars: single high partials (h40..h1000, -30..-8 dB) that are
    born one after another and twinkle on their own walks. Frame axis = night falling: from a bare sine to a sky
    full of stars, the late ones gathering into a milky band around h300."""
    rng = np.random.default_rng(4242)
    n = 700
    early = np.exp(rng.uniform(np.log(40), np.log(NMAX), n // 2))
    late = np.exp(rng.normal(np.log(300), 0.35, n - n // 2))
    pos = np.clip(np.round(np.concatenate([early, late])), 40, NMAX).astype(int)
    birth = np.concatenate([np.sort(rng.uniform(0.02, 0.6, n // 2)), np.sort(rng.uniform(0.35, 0.98, n - n // 2))])
    amp = db(rng.uniform(-30, -8, n))
    tw = walk(rng, n, 2.0)
    m = np.zeros((F, NH))
    m[:, 0] = 1.0
    m[:, 1] = db(-18)
    for k in range(n):
        g = sm((U - birth[k]) / 0.05) * amp[k] * (0.55 + 0.45 * np.tanh(tw[:, k]))
        m[:, pos[k] - 1] += g
    return spec(m)


def sunrise_chorale():
    """LUMINOUS. Dawn as rays: the octave-and-fifth stack (2, 3, 4, 6, 8, 12, 16, 24 ... 768) over a sine enters
    one ray at a time, lowest first — each ray a narrow band that widens with height — over a faint glow whose
    lowpass lifts (h3 -> h900). Frame axis = sunrise; the last fifth OVEREXPOSES: every ray blooms wide and the
    glow comes up to meet them: white light.
    Phase: Schroeder's law drawn from the LAST frame's spectrum (one set for the whole table), so the white-light
    climax is spread into an in-cycle chirp instead of a click-burst and plays at full level (crest < 3)."""
    s = sm(U)
    Kc = geo(3.0, 900.0, s)
    glow = KK ** -0.7 * lp(Kc, np.full(F, 0.7)) * col(db(-34 + 22 * sm(ramp(U, 0.55, 1.0))))
    m = glow.copy()
    m[:, 0] += 1.0
    stack = np.unique(np.concatenate([2 ** np.arange(1, 11), 3 * 2 ** np.arange(0, 9)]))
    stack = stack[(stack >= 2) & (stack <= NMAX)]
    bloom = sm(ramp(U, 0.76, 1.0))
    offs = np.arange(-40, 41)
    for st in stack:
        e = 0.03 + 0.62 * np.log2(st) / np.log2(768.0)
        g = sm(ramp(U, e - 0.06, e + 0.08)) * st ** -0.45
        wdt = 0.25 + 0.012 * st + 0.05 * st * bloom
        bins = st + offs
        ok = (bins >= 1) & (bins <= NMAX)
        prof = np.exp(-0.5 * (offs[None, ok] / wdt[:, None]) ** 2)
        m[:, bins[ok] - 1] += col(g) * prof
    return spec(m, schroeder(m[-1:]))


def event_horizon():
    """WILD END. A bright 1/k^0.85 ramp falls into a horizon at h200: every partial k is pulled to 200 + (k-200)*q,
    q = 1 -> 0.05, its power piling up where partials crowd. Frame axis = the SWALLOW: the harmonic series
    squeezes from both sides into a dense burning ring at h150..h240 with nothing left below or above it."""
    s = sm(U)
    q = geo(1.0, 0.05, s ** 1.2)
    H = 200.0
    a2 = K ** -1.7
    a2[NMAX:] = 0
    m = np.zeros((F, NH))
    for i in range(F):
        b = H + (K - H) * q[i]
        p = np.zeros(NH)
        putf(p, b, a2)
        m[i] = np.sqrt(p)
    m[:, :] += db(-66) * KK ** -0.3
    return spec(m, chirp_ph(260))


def shockwave():
    """TIME-BUILT. An N-wave (a sonic boom: jump up, linear fall through zero, jump back) centred in the cycle
    with its ground echo already trailing it, each front ringing into a low damped tone. Frame axis = the SHOCK:
    the wave shortens 72 % -> 22 % of the cycle, its fronts steepen (rise 0.02 -> 0.0003), the ring climbs
    14 -> 220 cycles and grows, and more reflections pile on at seeded delays until the cycle is a jagged wall.
    Level: where the reflections' rings stack into one tall lobe, that lobe's tip is soft-limited (8x, before the
    band-limit) so no frame's crest exceeds 3.6 — the late wall plays at the level of the rest, not 2-3 dB down."""
    s = sm(U)
    rng = np.random.default_rng(1947)
    refl_d = np.concatenate([rng.uniform(0.1, 0.9, 7), [0.31]])
    refl_on = np.concatenate([np.sort(rng.uniform(0.22, 0.9, 7)), [-1.0]])
    refl_a = np.concatenate([rng.uniform(0.35, 0.8, 7) * np.array([1, -1, 1, -1, 1, -1, 1]), [-0.45]])
    y = np.zeros((F, N8))

    def nwave(cc, L, rise, ring, q):
        e = cdist(T8, cc)
        S = lambda x: 0.5 * (1.0 + np.tanh(x / rise))
        out = (-2.0 * e / L) * S(e + L / 2) * S(L / 2 - e)
        for edge in (-L / 2, L / 2):
            x = cdist(T8, cc + edge)
            out += ring * (x > 0) * np.exp(-np.maximum(x, 0) / 0.08) * np.sin(2 * np.pi * q * x)
        return out
    for i in range(F):
        u = s[i]
        L = 0.72 - 0.50 * u
        rise = geo(0.02, 0.0003, u)
        ring = 1.0 + 0.5 * u ** 1.2
        q = geo(14.0, 220.0, u)
        yi = nwave(0.5, L, rise, ring, q)
        for d, o, a in zip(refl_d, refl_on, refl_a):
            g = 1.0 if o < 0 else sm((U[i] - o) / 0.1)
            if g > 0:
                yi += g * a * nwave(0.5 + d, L * 0.8, rise * 1.5, ring, q * 1.3)
        y[i] = yi
    return rolled(bl8(crest_limit(y, 3.6, 0.7)))


def air_raid_siren():
    """IN-CYCLE SIREN. A sine sub plus a carrier on harmonic 20 whose phase is swung by the cycle itself
    (instantaneous frequency 20 + I cos 2pi t): a siren wailing INSIDE every cycle. Frame axis = the alarm: the
    swing I = 0 -> 19 spreads the carrier into a dense band, the carrier morphs sine -> saw (so the band gets
    teeth up to h1000) and a second wail at 3x the cycle rate joins in the last third."""
    s = sm(U)
    y = np.zeros((F, N8))
    for i in range(F):
        u = s[i]
        I = 19.0 * u ** 1.1
        I2 = 7.0 * sm(ramp(U[i], 0.62, 1.0))
        phi = 20.0 * T8 + I / (2 * np.pi) * np.sin(2 * np.pi * T8) + I2 / (6 * np.pi) * np.sin(6 * np.pi * T8)
        sh = sm(ramp(U[i], 0.2, 0.85))
        car = (1.0 - sh) * np.sin(2 * np.pi * phi) + sh * 0.8 * (2.0 * (phi % 1.0) - 1.0)
        y[i] = 0.7 * np.sin(2 * np.pi * T8) + (0.22 + 0.9 * u) * car
    return tdone(y)


def glacier_fracture():
    """DENSE GROUND that cracks. Every harmonic up to 1000 with a gentle ripple, its tilt brightening 1/k^1.1 ->
    1/k^0.4 as the ice splits; sixty cracks (notches in log-frequency, -60 dB deep) open at seeded moments and
    widen after they open. Frame axis = the ice FRACTURING: a smooth dense wall becomes isolated floes of
    partials adrift between black gaps. Schroeder phase: drawn as an in-cycle chirp. The phase law is taken from the
    FRACTURED frames (last 40 %, each frame's power normalised, fundamental left out) so the broken ice spreads
    across the cycle and keeps its level (crest < 3.2) instead of collapsing into one spike."""
    s = sm(U)
    rng = np.random.default_rng(6060)
    m = KK ** -col(1.1 - 0.7 * s) * np.exp(0.25 * rng.standard_normal(NH))[None, :] \
        * lp(np.full(F, 700.0), np.full(F, 0.6))
    pos = rng.uniform(np.log(2.5), np.log(950), 60)
    on = np.sort(rng.uniform(0.05, 0.92, 60))
    wmax = rng.uniform(0.03, 0.16, 60)
    lk = np.log(KK)
    for p, o, wm in zip(pos, on, wmax):
        w = wm * sm((U - o) / 0.25)
        act = (U > o)
        if not act.any():
            continue
        notch = 1.0 - 0.999 * col(act) * np.exp(-0.5 * ((lk - p) / np.maximum(col(w), 1e-4)) ** 2)
        m *= notch
    pw = m ** 2
    pw[:, 0] = 0.0
    pw = pw / np.maximum(pw.sum(axis=1, keepdims=True), 1e-30)
    return spec(m, schroeder(np.sqrt(pw[U > 0.6].sum(axis=0))))


def sonar_swarm():
    """TIME-BUILT, dark-to-luminous. A sine body carrying a PING: a Hann-windowed burst of harmonic 64 riding on
    it. Frame axis = echolocation: the ping climbs to h480, narrows, and multiplies — 1 burst becomes 28 seeded
    bursts scattered around the cycle, each at its own pitch, until the body is buried under clicks."""
    s = sm(U)
    rng = np.random.default_rng(3131)
    nb = 28
    pos = np.concatenate([[0.25], rng.uniform(0, 1, nb - 1)])
    pit = np.concatenate([[1.0], rng.uniform(0.6, 1.5, nb - 1)])
    on = np.concatenate([[-1.0], np.sort(rng.uniform(0.15, 0.95, nb - 1))])
    amp = np.concatenate([[1.0], rng.uniform(0.4, 1.0, nb - 1)])
    y = np.zeros((F, N8))
    for i in range(F):
        u = s[i]
        p0 = geo(64.0, 480.0, u)
        w = geo(0.12, 0.008, u)
        yi = np.sin(2 * np.pi * T8)
        a0 = 0.3 + 0.6 * u
        for t0, pr, o, a in zip(pos, pit, on, amp):
            g = 1.0 if o < 0 else sm((U[i] - o) / 0.05)
            if g <= 0:
                continue
            d = cdist(T8, t0)
            win = np.where(np.abs(d) < w / 2, 0.5 + 0.5 * np.cos(2 * np.pi * d / w), 0.0)
            yi += g * a * a0 * win * np.sin(2 * np.pi * p0 * pr * d)
        y[i] = yi
    return tdone(y)


def pendulum_blade():
    """TIME-BUILT SHAPE. Two crescent BLADES (the difference of two offset circular arcs: a lune with razor tips),
    one up, one down. Frame axis = the pendulum descends: the blades' distance swings, they thin to slivers (tips
    sharpen), and in the last third their edges serrate into saw teeth (8 -> 70 per blade)."""
    s = sm(U)
    y = np.zeros((F, N8))
    for i in range(F):
        u = s[i]
        r = 0.30 - 0.08 * u
        dl = r * (0.55 - 0.40 * u)
        D = 0.43 + 0.3 * np.sin(2 * np.pi * 1.25 * U[i]) * (0.3 + 0.7 * u)
        z = sm(ramp(U[i], 0.58, 1.0))
        teeth = 8.0 + 62.0 * z
        yi = np.zeros(N8)
        for c0, sg in ((0.25, 1.0), (0.25 + D, -1.0)):
            e = cdist(T8, c0)
            outer = np.sqrt(np.maximum(0.0, 1.0 - (e / r) ** 2))
            inner = np.sqrt(np.maximum(0.0, 1.0 - ((e - dl) / r) ** 2))
            cr = np.maximum(outer - inner, 0.0)
            serr = 1.0 - 0.6 * z * ((teeth * (e + r) / (2 * r)) % 1.0)
            yi += sg * cr * serr
        y[i] = yi
    return rolled(bl8(y))


def supernova_remnant():
    """THREE ACTS. A star (sine + a few harmonics) swells and brightens (1/k^3 -> 1/k^1); it DETONATES into a flat
    white wall of every harmonic; then the remnant: the core collapses to a faint pulsar (h1, h2) inside a hollow
    shell — a narrow ring of partials that expands from h60 to h700. Frame axis = the life of the star."""
    s = sm(U)
    rng = np.random.default_rng(1054)
    p = geo(3.0, 1.0, ramp(U, 0.0, 0.45))
    star = KK ** -col(p) * col(1.0 - 0.95 * sm(ramp(U, 0.45, 0.62)))
    blast = np.exp(0.3 * rng.standard_normal(NH))[None, :] * col(sm(ramp(U, 0.38, 0.52))
                                                                  * (1.0 - 0.9 * sm(ramp(U, 0.58, 0.8))))
    blast = blast * 0.12
    shc = geo(60.0, 700.0, ramp(U, 0.55, 1.0))
    shw = 0.5 - 0.38 * ramp(U, 0.55, 1.0)
    shell = glog(shc, shw) * col(sm(ramp(U, 0.5, 0.65)) * 0.5) * np.exp(0.35 * rng.standard_normal(NH))[None, :]
    m = star + blast + shell
    m[:, 0] += 0.25 * sm(ramp(U, 0.55, 0.7))
    m[:, 1] += 0.12 * sm(ramp(U, 0.55, 0.7))
    return spec(m)


def leviathan_roar():
    """SUBBY. A sine sub (h1 + h2 at -8 dB) and a beast's roar above it: a rough full-series source (every
    partial's level jitters along the table) shaped by low jaw formants that open (F1 h5 -> h13, F2 h14 -> h22,
    plus h45/h60/h110 throat rings). Frame axis = the ROAR rising from -40 dB to over the sub, the jaws gaping."""
    s = sm(U)
    rng = np.random.default_rng(1851)
    rough = db(walk(rng, NH, 1.6) * col(2.0 + 6.0 * s))
    wf = 0.15 + 0.3 * s
    fmt = 1.0 * glog(geo(5.0, 13.0, s), wf) + 0.7 * glog(geo(14.0, 22.0, s), wf) \
        + 0.4 * glog(np.full(F, 45.0), wf * 0.8) + 0.3 * glog(np.full(F, 60.0), wf * 0.8) \
        + 0.25 * glog(np.full(F, 110.0), wf * 0.7) + 0.03
    roar = KK ** -0.3 * rough * fmt * col(db(-40 + 42 * s ** 1.1))
    m = roar.copy()
    m[:, 0] += 1.0
    m[:, 1] += db(-8)
    return spec(m)


def warp_streaks():
    """TIME-BUILT CHIRP. A sine sub plus a linear chirp INSIDE the cycle (instantaneous frequency sweeps from
    harmonic 2 up to fb and snaps back each cycle — periodic because the mean frequency is an integer). Frame axis
    = the WARP JUMP: fb climbs 6 -> 900 so the chirp becomes a dense flat band, then parallel streaks (copies at
    seeded offsets) multiply to seven and comb the band into stripes."""
    s = sm(U)
    rng = np.random.default_rng(1977)
    offs = rng.uniform(0.05, 0.95, 6)
    ons = np.sort(rng.uniform(0.5, 0.95, 6))
    y = np.zeros((F, N8))
    for i in range(F):
        u = s[i]
        fa = 2.0
        fb = 2.0 * round((fa + geo(6.0, 900.0, u)) / 2.0) - fa
        yi = 0.8 * np.sin(2 * np.pi * T8)
        amp = 0.25 + 0.7 * u

        def chirp(t):
            return np.sin(2 * np.pi * (fa * t + 0.5 * (fb - fa) * t * t))
        yi += amp * chirp(T8)
        for o, on in zip(offs, ons):
            g = sm((U[i] - on) / 0.08)
            if g > 0:
                yi += amp * g * 0.8 * chirp((T8 - o) % 1.0)
        y[i] = yi
    return tdone(y)


def temporal_rift():
    """TWO SPECTRA CROSSING. The odd harmonics carry one band, the even harmonics another. Frame 0: a warm
    square-ish odd body with a faint even glass at h500. Frame axis = the RIFT: the odd band climbs h1.5 -> h600
    while the even band falls h500 -> h4 and swells; they cross mid-table (a fused saw moment) and end swapped."""
    s = sm(U)
    co = geo(1.5, 600.0, s)
    ce = geo(500.0, 4.0, s)
    bo = glog(co, np.full(F, 1.0)) * ODD[None, :] * KK ** -0.5
    be = glog(ce, np.full(F, 0.6)) * EVN[None, :] * col(db(-30 + 32 * s)) * col(co ** -0.5)
    m = bo + be + db(-45) * KK ** -0.3 * col(co ** -0.5)
    return spec(m, chirp_ph(500))


def falling_forever():
    """ENDLESS FALL. A dense bright ground (every harmonic) cut by log-spaced notches that keep DESCENDING — a
    barber-pole phaser frozen into the table. Frame axis = the fall: the notches slide down three notch-periods,
    deepen to -60 dB and crowd from 1.5 to 5 per octave, while the ground itself darkens from 1/k^0.3 to 1/k^1.4."""
    s = sm(U)
    D = geo(1.5, 5.0, s)
    ph = 3.0 * U
    depth = 0.7 + 0.299 * s ** 0.5
    pw = 1.0 + 2.0 * s
    arg = 2 * np.pi * (np.log2(KK) * col(D) + col(ph))
    notch = (1.0 - col(depth)) + col(depth) * ((0.5 + 0.5 * np.cos(arg)) ** col(pw))
    m = KK ** -col(0.3 + 1.1 * s) * notch
    return spec(m, schroeder(m))


def ghost_choir():
    """WHISPER TO POSSESSION. A choir on a virtual root 2 (+1 oct) whose SOURCE changes: frame 0 is a dark whisper
    (random-magnitude breath through 'oo' formants), then the harmonic voice condenses out of it ('oh' -> 'ah'),
    then the possession: each formant splits into three and climbs 50 %, the source flattens to a raw buzz and the
    whispers come back as a hiss."""
    s = sm(U)
    rng = np.random.default_rng(666)
    keys = [(0.0, 'u', 0.9, 1.0, 1.0), (0.35, 'o', 1.0, 1.0, 1.0), (0.6, 'a', 1.0, 1.3, 0.8), (1.0, 'i', 1.5, 2.5, 0.3)]
    fq, am, bw = vowel_path(keys)
    split = sm(ramp(U, 0.6, 1.0))
    env = formant_env(K * 110.0, fq, am, bw)
    for sp in (-1.0, 1.0):
        env += formant_env(K * 110.0, fq * (1.0 + 0.22 * sp * col(split)), am, bw) * col(split)
    env += 0.004 + 0.02 * col(split)
    noise = np.exp(0.9 * rng.standard_normal(NH))[None, :] * db(walk(rng, NH, 1.5) * 3.0) * KK ** -0.2 \
        * lp(geo(12.0, 1000.0, s), np.full(F, 0.8))
    harm = np.zeros((F, NH))
    idx = np.arange(2, NMAX + 1, 2)
    harm[:, idx - 1] = (idx / 2.0)[None, :] ** -col(1.3 - 1.3 * split)
    h = sm(ramp(U, 0.05, 0.5))
    wn = (1.0 - h) + 0.5 * split
    m = env * (col(h) * harm + col(wn) * noise * 0.4)
    return spec(m)


def wildfire():
    """TIME + SPECTRAL. A low roar (pink random ground under a rising lowpass) with CRACKLE: seeded
    derivative-of-Gaussian clicks scattered in the cycle. Frame axis = a spark becoming a blaze: the roar opens
    h8 -> h400 and the crackle multiplies from 0 to 48 embers per cycle."""
    s = sm(U)
    rng = np.random.default_rng(451)
    base = np.exp(0.6 * rng.standard_normal(NH)) * K ** -0.6
    m = base[None, :] * db(walk(rng, NH, 2.0) * 3.0) * lp(geo(8.0, 400.0, s), np.full(F, 1.0))
    roar = to_cycles(m)
    roar /= np.abs(roar).max(axis=1, keepdims=True)
    n = 48
    pos = rng.uniform(0, 1, n)
    on = np.sort(rng.uniform(0.08, 0.97, n))
    amp = rng.uniform(0.3, 1.0, n) * rng.choice([-1, 1], n)
    wc = rng.uniform(0.0007, 0.002, n)
    y = np.zeros((F, N8))
    for i in range(F):
        yi = np.zeros(N8)
        for p, o, a, w in zip(pos, on, amp, wc):
            g = sm((U[i] - o) / 0.04)
            if g <= 0:
                continue
            d = cdist(T8, p)
            yi += g * a * (d / w) * np.exp(-0.5 * (d / w) ** 2)
        y[i] = yi
    cr = bl8(y)
    return rolled(roar + 0.25 * cr)


def chirp_ph(n):
    """Sine phase plus a gentle quadratic term: group delay k/n cycles. Low harmonics keep their shape,
    dense high bands smear into an in-cycle chirp instead of a spike (low crest, readable shape)."""
    return -np.pi / 2 - np.pi * K * K / float(n)


def crest_limit(y8, ct, knee=0.7, iters=12):
    """Soft-limit the tallest peaks of each 8x frame so its band-limited crest (peak/RMS) is <= ct. The knee
    starts at knee*ct*RMS; a tanh shoulder takes the rest. Frames already under ct are left bit-exact."""
    out = np.array(y8, float)
    for i in range(out.shape[0]):
        x = out[i]
        xb = bl8(x[None])[0] / OS
        r = np.sqrt((xb ** 2).mean())
        if r <= 0 or np.abs(xb).max() / r <= ct:
            continue
        c = ct
        for _ in range(iters):
            thr, cl = knee * c * r, c * r
            a = np.abs(x)
            xl = np.where(a > thr, np.sign(x) * (thr + (cl - thr) * np.tanh((a - thr) / (cl - thr))), x)
            xb = bl8(xl[None])[0] / OS
            if np.abs(xb).max() / np.sqrt((xb ** 2).mean()) <= ct:
                break
            c *= 0.95
        out[i] = xl
    return out


def rolled(frames):
    """Rotate EVERY frame by the same amount so the wrap lands where the table is quietest. A rotation leaves
    each frame's magnitude spectrum (and every metric) unchanged; it only moves a riser off the seam."""
    x = np.asarray(frames, float)
    D = np.abs(x - np.roll(x, 1, axis=1))
    k = 20
    typ = np.partition(D, D.shape[1] - k, axis=1)[:, -k:].mean(axis=1)
    worst = (D / np.maximum(typ, 1e-12)[:, None]).max(axis=0)
    r = int(np.argmin(worst))
    return wtlib.finalize(np.roll(x, -r, axis=1))


TABLES = [
    ("ABYSSAL PRESSURE",        abyssal_pressure),
    ("TECTONIC GRIND",          tectonic_grind),
    ("BRAAM WALL",              braam_wall),
    ("HORN RIP",                horn_rip),
    ("FOGHORN CASCADE",         foghorn_cascade),
    ("HUMMING TO HOWLING",      humming_to_howling),
    ("BOW TO THE BRIDGE",       bow_to_the_bridge),
    ("SEMITONE VISE",           semitone_vise),
    ("DREAD CHORD",             dread_chord),
    ("BLACK MONOLITH",          black_monolith),
    ("THUNDERHEAD",             thunderhead),
    ("TINNITUS AFTERMATH",      tinnitus_aftermath),
    ("HEARTBEAT FIBRILLATION",  heartbeat_fibrillation),
    ("SHEPARD ASCENT",          shepard_ascent),
    ("REVERSE CYMBAL RISE",     reverse_cymbal_rise),
    ("AURORA CURTAIN",          aurora_curtain),
    ("STARFIELD",               starfield),
    ("SUNRISE CHORALE",         sunrise_chorale),
    ("EVENT HORIZON",           event_horizon),
    ("SHOCKWAVE",               shockwave),
    ("AIR RAID SIREN",          air_raid_siren),
    ("GLACIER FRACTURE",        glacier_fracture),
    ("SONAR SWARM",             sonar_swarm),
    ("PENDULUM BLADE",          pendulum_blade),
    ("SUPERNOVA REMNANT",       supernova_remnant),
    ("LEVIATHAN ROAR",          leviathan_roar),
    ("WARP STREAKS",            warp_streaks),
    ("TEMPORAL RIFT",           temporal_rift),
    ("FALLING FOREVER",         falling_forever),
    ("GHOST CHOIR",             ghost_choir),
    ("WILDFIRE",                wildfire),
]
CATEGORY = {ident: "Cinematic" for ident, _ in TABLES}


if __name__ == "__main__":
    import time
    for nm, fn in TABLES:
        t0 = time.time()
        fr = fn()
        ok, line = wtlib.selfcheck(nm, fr)
        print(line, " %.2fs" % (time.time() - t0))
