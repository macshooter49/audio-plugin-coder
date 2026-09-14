"""
gen2_textures_b.py — Terrain factory library, TEXTURES module B (new tables for the 500, fb638).

Max's own sounds, second batch: the Circuit Motions one-shots (licensed by Waves Crate — Max,
2026-09-13) and his 2023 SOLO packs (Soundsource 1-4, Anachronous Multi-Kit, Analog Tools 1).
No collaborations, no other producers' files, no Splice archive. The source files are READ-ONLY:
this module re-reads them every time it runs and writes nothing next to them.

Module A (gen2_textures.py) takes the 2024/2025 picks through the plain three imports. This module
uses different sources and leans on what happens AFTER the import — every table is one nameable
process riding the sound's life:
  PITCH-SYNC   period-accurate cycles (YIN-style difference function, parabolic peak, median-smoothed
               track), each period spline-sampled to 2048, drift-corrected so it loops, band-limited to
               the source's own Nyquist, fundamental phase aligned across frames so the morph glides.
               Then a process rides the life: hard sync of the captured cycle, a wavefolder, a cranked
               asymmetric drive, a harmonic stretch that bends the bass into a bell.
  FREEZE       STFT magnitudes (8192 window, 3-window average) sampled onto the 1024-harmonic grid,
               wtlib's fixed phase. Frame axis = the sound's life, or re-sorted dark->bright, or
               sparsified down to its spectral skeleton.
  GRAIN        raw 2048-sample chops, loop-crossfaded (the pedal grit and aliasing stay), or chops that
               get longer and are squeezed into one cycle, or a magnitude cloud of seeded grains.
  CROSS-BREED  one sound's cepstral envelope carved into another's harmonic body; two sounds trading
               fine structure and envelope; a sample used as the phase modulator of a sine; a slice of a
               sample turned into a waveshaper transfer curve; a clean sub under a rising gravel top.
Every table ends in wtlib.finalize(); time-domain processes run at 8x and keep harmonics <= 1000.
Deterministic: fixed file segments, seeded numpy Generators only.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import wtlib
import soundfile as sf
from scipy.signal import resample_poly, medfilt
from scipy.interpolate import CubicSpline
from scipy.fft import dct, idct
from scipy.ndimage import uniform_filter1d

F, NH, SZ, SR = wtlib.FRAMES, wtlib.NH, wtlib.SIZE, wtlib.SR
KH = 1000                                    # keep harmonics <= 1000 (lessons: band-limiting)
s = np.linspace(0.0, 1.0, F)                 # the frame axis
sc = s[:, None]
KK = np.arange(SZ // 2 + 1)                  # rfft bin index (bin k = harmonic k)
kh = np.arange(1, NH + 1, dtype=float)       # harmonic numbers of a magnitude row
OS = 8
T8 = np.arange(SZ * OS) / (SZ * OS)
TWO_PI = 2.0 * np.pi

# ── sources (READ-ONLY) ───────────────────────────────────────────────────────────────────────────
WC = "/Users/macshooter/Desktop/Waves Crate/"
ROOTS = {
    "CM":  (WC + "2025/Waves Crate 1.0/9. VSTS/WAVES CRATE - CIRCUIT MOTIONS/2. ONE_SHOTS", "WAVES CRATE - CIRCUIT MOTIONS"),
    "ANA": (WC + "2023/MULTI KITS/MACSHOOTER - ANACHRONOUS MULTI-KIT", "MACSHOOTER - ANACHRONOUS MULTI-KIT (2023)"),
    "SS1": (WC + "2023/MULTI KITS/MACSHOOTER - SOUNDSOURCE VOL. 1", "MACSHOOTER - SOUNDSOURCE VOL. 1 (2023)"),
    "SS2": (WC + "2023/MULTI KITS/MACSHOOTER - SOUNDSOURCE VOL. 2", "MACSHOOTER - SOUNDSOURCE VOL. 2 (2023)"),
    "SS3": (WC + "2023/MULTI KITS/MACSHOOTER - SOUNDSOURCE VOL. 3", "MACSHOOTER - SOUNDSOURCE VOL. 3 (2023)"),
    "SS4": (WC + "2023/MULTI KITS/MACSHOOTER - SOUNDSOURCE VOL. 4", "MACSHOOTER - SOUNDSOURCE VOL. 4 (2023)"),
    "AT1": (WC + "2023/PRESET KITS/WAVES CRATE - ANALOG TOOLS (EFFECT PRESET KIT)/Waves Crate - BONUS ONE SHOTS & CHOPS",
            "WAVES CRATE - ANALOG TOOLS 1 (2023)"),
}
_PATHS, _AUDIO = {}, {}


def src(root, name):
    """Full path of `name` under a pack root (walked in sorted order, so the answer is stable)."""
    key = (root, name)
    if key not in _PATHS:
        base = ROOTS[root][0]
        direct = os.path.join(base, name)
        if os.path.isfile(direct):
            _PATHS[key] = direct
        else:
            for dp, dn, fn in os.walk(base):
                dn.sort()
                if name in fn:
                    _PATHS[key] = os.path.join(dp, name)
                    break
            else:
                raise FileNotFoundError("%s: %s" % (root, name))
    return _PATHS[key]


def load(root, name):
    """Mono float64 at 44.1 kHz, DC removed. Cached per process; the file is only ever read."""
    p = src(root, name)
    if p not in _AUDIO:
        x, sr = sf.read(p, dtype="float64", always_2d=True)
        x = x.mean(axis=1)
        if int(sr) != SR:
            g = int(np.gcd(int(sr), SR))
            x = resample_poly(x, SR // g, int(sr) // g)
        _AUDIO[p] = x - x.mean()
    return _AUDIO[p]


def envelope(x, win=1024):
    return np.sqrt(np.maximum(np.convolve(x * x, np.ones(win) / win, "same"), 0.0))


def span(x, on_db=-24.0, off_db=-30.0):
    """(onset, end) samples: first crossing of on_db, last sample above off_db (re: envelope peak)."""
    e = envelope(x)
    pk = e.max()
    a = int(np.argmax(e > pk * 10 ** (on_db / 20)))
    b = int(len(e) - 1 - np.argmax(e[::-1] > pk * 10 ** (off_db / 20)))
    return a, b


# ── pitch-sync ────────────────────────────────────────────────────────────────────────────────────
def period_at(x, c, P0, lo=0.85, hi=1.18):
    """Period (samples, fractional) near sample c: normalised difference function over ~3 periods,
    minimum inside [lo, hi] x P0 (no octave errors), parabolic refinement."""
    tmax = int(P0 * hi) + 2
    W = max(int(3 * P0), 2048)
    a = int(c - W // 2)
    a = max(0, min(a, len(x) - W - tmax - 2))
    seg = x[a:a + W + tmax]
    n = 1 << int(np.ceil(np.log2(W + tmax + W)))
    r = np.fft.irfft(np.conj(np.fft.rfft(seg[:W], n)) * np.fft.rfft(seg, n), n)[:tmax + 1]
    cs = np.concatenate([[0.0], np.cumsum(seg * seg)])
    E0 = cs[W]
    Et = cs[np.arange(tmax + 1) + W] - cs[np.arange(tmax + 1)]
    dn = (E0 + Et - 2 * r) / np.maximum(E0 + Et, 1e-12)
    t0, t1 = int(P0 * lo), min(int(P0 * hi), tmax - 1)
    j = t0 + int(np.argmin(dn[t0:t1 + 1]))
    y0, y1, y2 = dn[j - 1], dn[j], dn[j + 1]
    den = y0 - 2 * y1 + y2
    off = 0.5 * (y0 - y2) / den if abs(den) > 1e-12 else 0.0
    return j + float(np.clip(off, -0.5, 0.5))


def grab(x, t, P):
    """ONE period starting at sample t (fractional), spline-sampled to 2048 points, the start/end
    mismatch removed as a linear drift so it loops. Returns its rfft (bin k = harmonic k)."""
    i0 = int(np.floor(t)) - 3
    i1 = int(np.ceil(t + P)) + 5
    i0c, i1c = max(i0, 0), min(i1, len(x))
    seg = np.zeros(i1 - i0)
    seg[i0c - i0:i1c - i0] = x[i0c:i1c]
    y = CubicSpline(np.arange(i0, i1), seg)(t + P * np.arange(SZ + 1) / SZ)
    y = y[:SZ] - (y[SZ] - y[0]) * np.arange(SZ) / SZ
    return np.fft.rfft(y)


def align(H, start_phase=-np.pi / 2):
    """Rotate every cycle so the fundamental's phase is the same in every frame (the morph glides).
    Frames with a weak fundamental are aligned to the previous frame by circular correlation."""
    out = np.empty_like(H)
    prev = None
    for i, h in enumerate(H):
        m = np.abs(h[1:9])
        if m[0] >= 0.2 * m.max() or prev is None:
            g = h * np.exp(1j * KK * (start_phase - np.angle(h[1])))
        else:
            c = np.fft.irfft(h * np.conj(prev), n=SZ)
            g = h * np.exp(2j * np.pi * KK * int(np.argmax(c)) / SZ)
        out[i] = g
        prev = g
    return out


def pitch_sync(x, f0, a, b, curve=1.0, hcap=KH):
    """(H, hmax): 128 aligned one-period spectra spread over [a, b] (positions a + (b-a) s^curve)."""
    P0 = SR / f0
    b = min(b, len(x) - int(3 * P0) - 8)
    anchors = np.linspace(a + P0, b, 48)
    Ps = medfilt(np.array([period_at(x, c, P0) for c in anchors]), 5)
    pos = a + (b - a) * s ** curve
    P = np.interp(pos, anchors, Ps)
    H = np.array([grab(x, t, p) for t, p in zip(pos, P)])
    hmax = int(min(hcap, 0.97 * P.min() / 2))
    H[:, 0] = 0
    H[:, hmax + 1:] = 0
    return align(H), hmax


def to_frames(H):
    return np.fft.irfft(H, n=SZ, axis=1)


def peak1(fr):
    return fr / np.maximum(np.abs(fr).max(axis=1, keepdims=True), 1e-12)


def up(fr):
    X = np.fft.rfft(fr, axis=1)
    Y = np.zeros((fr.shape[0], OS * SZ // 2 + 1), complex)
    Y[:, :X.shape[1]] = X * OS
    return np.fft.irfft(Y, n=OS * SZ, axis=1)


def down(y, hmax=KH):
    Y = np.fft.rfft(y, axis=1)[:, :SZ // 2 + 1] / (y.shape[1] // SZ)
    Y[:, 0] = 0
    Y[:, hmax + 1:] = 0
    return np.fft.irfft(Y, n=SZ, axis=1)


def expo(a, b, c=1.0):
    return a * (b / a) ** (s ** c)


def dark_first(fr):
    """Order a life so frame 0 is the darker end (usable first, the wild end last)."""
    M = np.abs(np.fft.rfft(fr, axis=1))[:, 1:]
    w = M ** 2
    cen = (w * np.arange(1, w.shape[1] + 1)).sum(1) / np.maximum(w.sum(1), 1e-30)
    q = max(4, F // 8)
    return fr[::-1].copy() if cen[:q].mean() > cen[-q:].mean() else fr


# ── freeze / grain / cross-breed helpers ─────────────────────────────────────────────────────────
def freeze(x, cs, W=8192, nav=3, point=False):
    """(len(cs), NH) magnitudes: STFT power around each centre, averaged over nav windows (hop W/4)
    and over the W/2048 bins that belong to each harmonic of the 2048 grid (k * 21.53 Hz).
    point=True samples ONLY the grid bin itself (no averaging: the raw speckle of the sound stays)."""
    hop = W // 4
    win = np.hanning(W)
    r = W // SZ
    idx = r * np.arange(1, NH + 1)[:, None] + np.arange(-(r // 2), r - r // 2)[None, :]
    xp = np.pad(x, (W, W + nav * hop))
    out = np.zeros((len(cs), NH))
    for i, c in enumerate(cs):
        acc = np.zeros(W // 2 + 1)
        for j in range(nav):
            o = int(c + W + (j - (nav - 1) / 2) * hop - W // 2)
            acc += np.abs(np.fft.rfft(xp[o:o + W] * win)) ** 2
        P = np.concatenate([acc / nav, np.zeros(r)])
        out[i] = np.sqrt(P[r * np.arange(1, NH + 1)]) if point else np.sqrt(P[idx].mean(1))
    out[:, KH:] = 0
    return out


def mags_to_frames(M):
    M = np.array(M, dtype=float)
    M[:, KH:] = 0
    return wtlib.cycles_from_mags(M)


def centroid_rows(M):
    w = M ** 2
    return (w * kh[:w.shape[1]]).sum(1) / np.maximum(w.sum(1), 1e-30)


def chop(x, p, L, X=256):
    """Raw chop of L samples at p, the X samples after it crossfaded into its head (seamless loop),
    fitted into one 2048 cycle by FFT (L > 2048 = time-squeezed, brick-walled at the grid)."""
    p = int(max(0, min(p, len(x) - L - X - 1)))
    seg = x[p:p + L + X]
    y = seg[:L].copy()
    w = np.sin(0.5 * np.pi * (np.arange(X) + 0.5) / X) ** 2
    y[:X] = y[:X] * w + seg[L:L + X] * (1 - w)
    Y = np.fft.rfft(y)
    Z = np.zeros(SZ // 2 + 1, complex)
    n = min(len(Y), SZ // 2 + 1)
    Z[:n] = Y[:n]
    Z[0] = 0
    Z[KH + 1:] = 0
    return np.fft.irfft(Z, n=SZ)


def envelope_of(M, ncoef=30):
    """Cepstral (DCT-of-log) spectral envelope of magnitude rows, ncoef coefficients."""
    fl = 1e-7 * np.maximum(M.max(axis=1, keepdims=True), 1e-30)
    L = np.log(np.maximum(M, fl))
    C = dct(L, type=2, norm="ortho", axis=1)
    C[:, ncoef:] = 0
    return np.exp(idct(C, type=2, norm="ortho", axis=1))


def mags(H):
    return np.abs(H[:, 1:NH + 1])


def norm_rows(M):
    return M / np.maximum(M.max(axis=1, keepdims=True), 1e-30)


def fin(fr, roll=0):
    fr = np.asarray(fr, dtype=float)
    if roll:
        fr = np.roll(fr, roll, axis=1)
    return wtlib.finalize(fr)


# ═════════════════════════════════════════════════════════════════════════════════════════════════
#  THE TABLES
# ═════════════════════════════════════════════════════════════════════════════════════════════════

# ── pitch-sync: the life itself ─────────────────────────────────────────────────────────────────
def fuse_wire_bass():
    """SS2 bass (C2), 16 s: period-accurate cycles over its whole life — a 40-semitone filter sweep
    that Max played by hand, captured one period at a time. Subby; ~335 harmonics at the bright end."""
    x = load("SS2", "SOUNDSOURCE Bass - TicTac - (C).wav")
    a, b = span(x, -24, -34)
    H, hm = pitch_sync(x, 63.8, a, b)
    return fin(dark_first(to_frames(H)))


def opening_jaw():
    """SS2 synth (C2), 10 s: the whole note's life by pitch-sync (from 0.3 s in, past the attack click) —
    a 45-semitone opening from a round sub to a buzzing, snarling top."""
    x = load("SS2", "SOUNDSOURCE Synth - Arm & Leg - (C).wav")
    a, b = span(x, -10, -32)
    H, hm = pitch_sync(x, 64.0, a + int(0.3 * SR), b)
    return fin(dark_first(to_frames(H)))


# ── pitch-sync + a process riding the life ──────────────────────────────────────────────────────
def captured_sync():
    """Circuit Motions note (C4): each captured period becomes the SLAVE of a hard-sync oscillator whose
    ratio climbs 1 -> 9 while the note decays underneath. A sampled cycle screaming like a sync lead."""
    x = load("CM", "WC - CM - KLAUZINHO18.wav")
    a, b = span(x, -20, -30)
    H, hm = pitch_sync(x, 261.7, a, b, curve=1.6)
    cyc = up(peak1(to_frames(H)))
    r = expo(1.0, 9.0, 1.1)
    M = OS * SZ
    out = np.empty_like(cyc)
    for i in range(F):
        p = (r[i] * T8 % 1.0) * M
        i0 = np.floor(p).astype(int)
        w = p - i0
        out[i] = cyc[i, i0 % M] * (1 - w) + cyc[i, (i0 + 1) % M] * w
    return fin(down(out), roll=SZ // 2)


def pedal_fold():
    """Circuit Motions note (C4) through a triangle wavefolder whose gain rises 1 -> 30 (and a bias creeps
    in, so even harmonics join) as the note lives: the pedal stomped harder every frame until the cycle is a
    nest of lopsided zigzags."""
    x = load("CM", "WC - CM - KLAUZINHO92.wav")
    a, b = span(x, -20, -30)
    H, hm = pitch_sync(x, 261.4, a, b, curve=1.3)
    cyc = up(peak1(to_frames(H)))
    g = expo(1.0, 30.0, 1.2)[:, None]
    v = g * cyc + 0.4 * sc
    return fin(down(4 * np.abs(((v + 1) / 4) % 1 - 0.5) - 1))


def cranked_sub():
    """Anachronous sub (C1): pitch-synced over its life, then an asymmetric drive cranked 1 -> 40 with a
    growing bias, and the clipped result folded back on itself (fold 1 -> 8, with 30 % of the clipped
    signal kept under the fold), then every frame RMS-matched and soft-limited (tanh at 2.5x RMS) like an
    output stage, so no fold setting collapses a frame into a quiet needle. The 33 Hz body survives; the
    top turns into a square, then into a shredded buzz-saw."""
    x = load("ANA", "bass - more dreams.wav")
    a, b = span(x, -24, -30)
    H, hm = pitch_sync(x, 32.7, a, b)
    cyc = up(peak1(to_frames(H)))
    g = expo(1.0, 40.0, 0.9)[:, None]
    bias = 0.5 * sc ** 1.2
    y = np.tanh(g * (cyc + bias)) - np.tanh(g * bias)
    y = peak1(y)
    y = 0.7 * np.sin(0.5 * np.pi * (1 + 7 * sc ** 1.5) * y) + 0.3 * y
    y = y - y.mean(axis=1, keepdims=True)
    y = y / np.maximum(np.sqrt((y * y).mean(axis=1, keepdims=True)), 1e-12)
    y = 2.5 * np.tanh(y / 2.5)
    return fin(down(y))


def bass_to_bell():
    """Anachronous bass (C2): the pitch-synced life with its harmonic series stretched k -> k^(1+a),
    a = 0 -> 0.5. The bass keeps its root while the overtones walk out into a struck-metal spread."""
    x = load("ANA", "bass - deadly sins.wav")
    a, b = span(x, -24, -32)
    H, hm = pitch_sync(x, 65.5, a, b)
    A = mags(H)
    alpha = 0.5 * s ** 1.2
    out = np.zeros((F, NH))
    for i in range(F):
        tgt = np.round(kh ** (1 + alpha[i])).astype(int)
        m = (tgt <= KH) & (A[i] > 0)
        np.add.at(out[i], tgt[m] - 1, A[i][m])
    return fin(mags_to_frames(out))


# ── spectral freeze ──────────────────────────────────────────────────────────────────────────────
def white_static_bloom():
    """SS2 texture, 12 s: frozen onto the 1024-harmonic grid across its life — a hiss that climbs from
    a mid-range grind to a blinding 15 kHz spray. Maximum harmonics."""
    x = load("SS2", "SOUNDSOURCE Texture - YE.wav")
    a, b = span(x, -30, -40)
    M = freeze(x, np.linspace(a + 4096, b - 4096, F))
    return fin(mags_to_frames(M))


def sorted_hiss():
    """Anachronous noise texture frozen, then its 128 moments RE-ORDERED dark -> bright and smoothed
    over 5 frames: a messy noise bed turned into a playable brightness axis."""
    x = load("ANA", "texture - noise - c.wav")
    a, b = span(x, -30, -40)
    M = freeze(x, np.linspace(a + 4096, b - 4096, F), nav=2)
    M = M[np.argsort(centroid_rows(M))]
    M = uniform_filter1d(M, size=5, axis=0, mode="nearest")
    return fin(mags_to_frames(M))


def submerged():
    """Anachronous underwater accent frozen RAW across its life (one window, the exact grid bins, no
    averaging — the speckle of the bubbles stays): a 43-semitone surfacing from muffled pressure into
    bubbling mid-range partials."""
    x = load("ANA", "accent - underwater.wav")
    a, b = span(x, -30, -40)
    M = freeze(x, np.linspace(a + 4096, b - 4096, F), nav=1, point=True)
    return fin(dark_first(mags_to_frames(M)))


def circuit_haze():
    """Circuit Motions texture (C5) frozen: the pitched preset becomes a noisy harmonic comb whose
    shimmer and grit evolve over its life."""
    x = load("CM", "WC - CM - KLAUZINHO3.wav")
    a, b = span(x, -30, -40)
    M = freeze(x, np.linspace(a + 4096, b - 4096, F))
    return fin(mags_to_frames(M))


def bone_glitter():
    """SS2 wave texture frozen, then SPARSIFIED: each frame keeps only the bins that stand above the local
    median by a threshold that climbs 0 -> 24 dB. Dense wash -> a skeleton of glittering partials."""
    x = load("SS2", "SOUNDSOURCE Texture - Wave.wav")
    a, b = span(x, -30, -40)
    M = freeze(x, np.linspace(a + 4096, b - 4096, F))
    from scipy.ndimage import median_filter
    L = 20 * np.log10(np.maximum(M, 1e-12))
    med = median_filter(L, size=(1, 31), mode="nearest")
    thr = 24.0 * s ** 0.8
    keep = (L - med) >= thr[:, None]
    soft = np.where(keep, M, M * 10 ** (-60 * sc / 20))
    return fin(mags_to_frames(soft))


def frozen_glissando():
    """Anachronous harp glissando frozen: the frame axis IS the glissando, a ladder of partials sweeping
    through the grid, each moment a different chord of plucked strings, down into the fading tail."""
    x = load("ANA", "accent - harpglissando.wav")
    a, b = span(x, -30, -50)
    M = freeze(x, np.linspace(a + 2048, b - 2048, F), W=4096, nav=2)
    return fin(mags_to_frames(M))


# ── grain ────────────────────────────────────────────────────────────────────────────────────────
def chipped_glass():
    """Anachronous glassy accent chopped RAW: 128 consecutive-ish 2048-sample chops, loop-crossfaded,
    nothing smoothed — the saturation, aliasing and flutter of the pedal chain stay in."""
    x = load("ANA", "accent - shoulderchip.wav")
    a, b = span(x, -30, -40)
    pos = np.linspace(a, b - 4096, F)
    return fin(np.array([chop(x, p, SZ) for p in pos]))


def squeezed_chimes():
    """Anachronous digital chimes: each frame is a longer chop (2048 -> 32768 samples) squeezed into one
    cycle — the chimes accelerate past pitch into a fizzing, glitching spray. The source's amplitude envelope
    is flattened first (10 ms RMS), so every strike counts equally and no frame is a lone spike."""
    x = load("ANA", "accent - digichimes.wav")
    a, b = span(x, -30, -40)
    e = np.sqrt(np.convolve(x * x, np.ones(441) / 441, "same"))
    x = x / (e + 1e-3 * e.max())
    L = np.round(expo(2048.0, 32768.0, 1.0)).astype(int)
    a += int(0.12 * SR)
    pos = a + (b - a - L - 512) * 0.15 * s
    return fin(np.array([chop(x, p, l, X=min(1024, l // 8)) for p, l in zip(pos, L)]))


def grain_fog():
    """Anachronous 'sound' accent as a magnitude cloud: 16 seeded Hann grains per frame summed as
    MAGNITUDES (no cancellation) from a region that drifts through the whole file and widens as it goes
    (dark end first): a 40-semitone fog bank of the accent's own grain."""
    x = load("ANA", "accent - sound.wav")
    a, b = span(x, -30, -40)
    rng = np.random.default_rng(0x7E81)
    win = np.hanning(SZ)
    out = np.zeros((F, NH))
    cen = a + (b - a - SZ) * (0.02 + 0.96 * s)
    spread = (b - a) * (0.005 + 0.2 * s ** 1.5)
    for i in range(F):
        ps = np.clip(cen[i] + rng.uniform(-1, 1, 16) * spread[i], a, b - SZ).astype(int)
        acc = np.zeros(SZ // 2 + 1)
        for p in ps:
            acc += np.abs(np.fft.rfft(x[p:p + SZ] * win))
        out[i] = acc[1:NH + 1]
    return fin(dark_first(mags_to_frames(out)))


# ── cross-breeds ─────────────────────────────────────────────────────────────────────────────────
def carved_rumble():
    """Cross-breed: the Circuit Motions note's spectral ENVELOPE (cepstral, 30 coefficients) over its life,
    carved into the 650-harmonic body of a buzzing Anachronous C1 bass. A 33 Hz growl that sings."""
    body_x = load("ANA", "bass - ready for war.wav")
    ba, bb = span(body_x, -24, -30)
    Hb, hmb = pitch_sync(body_x, 32.8, ba, bb)
    B = mags(Hb)
    fineB = B / envelope_of(B + 1e-9, 40)
    cx = load("CM", "WC - CM - KLAUZINHO10.wav")
    a, b = span(cx, -24, -36)
    E = envelope_of(freeze(cx, np.linspace(a + 4096, b - 4096, F)) + 1e-9, 30)
    # envelope on absolute frequency: body harmonic k (at C1) = grid bin k * 32.8 / 21.53
    gpos = kh * 32.8 / (SR / SZ)
    Ek = np.array([np.interp(gpos, kh, e, right=e[KH - 1]) for e in E])
    out = fineB * Ek
    out[:, hmb:] = 0
    return fin(mags_to_frames(out))


def swapped_skins():
    """Cross-breed (A fine + B envelope -> B fine + A envelope): a pitch-synced, noisy Anachronous bass (C2)
    and the Anachronous 'gram' texture trade skins across the table, geometric crossfade in both halves."""
    ax = load("ANA", "bass - firefly.wav")
    a, b = span(ax, -24, -32)
    Ha, hma = pitch_sync(ax, 65.4, a, b)
    A = norm_rows(mags(Ha)) + 1e-6
    bx = load("ANA", "texture - gram - c.wav")
    c, d = span(bx, -30, -40)
    B = norm_rows(freeze(bx, np.linspace(c + 4096, d - 4096, F))) + 1e-6
    eA, eB = envelope_of(A, 24), envelope_of(B, 24)
    fA, fB = A / eA, B / eB
    x = sc ** 1.0
    fine = fA ** (1 - x) * fB ** x
    env = eB ** (1 - x) * eA ** x
    return fin(mags_to_frames(fine * env))


def sample_shred():
    """A sine phase-modulated BY Max's sound: a 2048-sample window sliding through a Circuit Motions note is
    the modulator, index 0.2 -> 9. A pure sub at frame 0, shredded by its own source at the end."""
    x = load("CM", "WC - CM - KLAUZINHO70.wav")
    a, b = span(x, -24, -34)
    pos = np.linspace(a, b - 4096, F)
    W = peak1(np.array([chop(x, p, SZ) for p in pos]))
    I = expo(0.2, 9.0, 1.0)[:, None]
    y = np.sin(TWO_PI * T8[None, :] + I * up(W))
    return fin(down(y))


def jagged_transfer():
    """A 2048-sample slice of an Anachronous bell texture, integrated and de-trended, used as a WAVESHAPER
    transfer curve. The drive sweeps 0.08 -> 1 (and the bias drifts), so a sine picks up the sound's jagged
    fingerprint wider and wider. The curve itself starts smoothed over 400 samples (a gentle S) and sharpens
    to the raw slice by the last frame."""
    x = load("ANA", "texture - bellz - c.wav")
    a, b = span(x, -30, -40)
    p = int(a + 0.35 * (b - a))
    sl = x[p:p + SZ]
    cur = np.cumsum(sl - sl.mean())
    cur = cur - np.linspace(cur[0], cur[-1], SZ)
    cur = cur / np.abs(cur).max()
    g = expo(0.25, 1.0, 1.0)
    bias = 0.25 * np.sin(np.pi * s)
    ks = np.round(expo(401.0, 1.0, 0.8)).astype(int)
    grid = np.arange(SZ)
    y = np.empty((F, OS * SZ))
    for i in range(F):
        c = uniform_filter1d(cur, int(ks[i]), mode="nearest") if ks[i] > 1 else cur
        c = c / np.abs(c).max()
        v = np.clip(g[i] * np.sin(TWO_PI * T8) + bias[i], -1, 1)
        y[i] = np.interp((v + 1) * 0.5 * (SZ - 1), grid, c)
    return fin(down(y))


def sub_under_gravel():
    """A clean SS3 sub (C1, pitch-synced over its life) under a gravel TOP: the frozen spectrum of a gritty
    Anachronous accent, high-passed above harmonic 10 and raised from -60 dB to +6 dB over the table.
    The sub never leaves; the top goes from a whisper to a landslide."""
    x = load("SS3", "SOUNDSOURCE Bass - Don't Trip Sub (C).wav")
    a, b = span(x, -20, -30)
    H, hm = pitch_sync(x, 32.7, a, b)
    S = mags(H)
    S = S / S.max(axis=1, keepdims=True)
    gx = load("ANA", "accent - shoulderchip.wav")
    c, d = span(gx, -30, -40)
    G = freeze(gx, np.linspace(c + 4096, d - 4096, F), nav=2)
    hp = 1.0 / (1.0 + (10.0 / kh) ** 4)
    G = norm_rows(G * hp)
    lvl = 10 ** ((-60 + 66 * s ** 0.9) / 20)[:, None]
    return fin(mags_to_frames(S + lvl * G))


def shrapnel_crawl():
    """Analog Tools 1 'textured' one-shot as STRETCHED grain: chops of 256 -> 2048 samples fitted into one
    cycle — frame 0 is the texture slowed to an eighth (a dark, crawling rumble), the last frame the raw
    full-speed shrapnel, while the chop position walks through the file's loud moments (within 18 dB of
    its peak, so no frame is a peak-normalised dip)."""
    x = load("AT1", "ANALOG TOOLS - Textured Warfare - (C).wav")
    e = envelope(x)
    loud = np.where(e[:len(x) - 2600] > e.max() * 10 ** (-18 / 20))[0]
    pos = loud[np.round(np.linspace(0, len(loud) - 1, F)).astype(int)]
    L = np.round(expo(256.0, 2048.0, 1.0)).astype(int)
    return fin(np.array([chop(x, p, l, X=max(32, l // 8)) for p, l in zip(pos, L)]))


def clang_lead():
    """Analog Tools 1 lead (C3), pitch-synced over its life and RING-MODULATED by harmonic m = 0 -> 48
    (adjacent integers crossfaded, so every frame stays periodic): the lead splits into mirrored sidebands
    and ends as a pan-lid clang."""
    x = load("AT1", "ANALOG TOOLS - Pots & Pans Lead - (C).wav")
    a, b = span(x, -12, -32)
    H, hm = pitch_sync(x, 130.8, a, b)
    c = peak1(to_frames(H))
    m = 48.0 * s ** 1.4
    m0 = np.floor(m)
    fr = (m - m0)[:, None]
    t = wtlib.t[None, :]
    car = (1 - fr) * np.cos(TWO_PI * m0[:, None] * t) + fr * np.cos(TWO_PI * (m0[:, None] + 1) * t)
    return fin(c * car)


# ═════════════════════════════════════════════════════════════════════════════════════════════════
TABLES = [
    ("FUSE WIRE BASS", fuse_wire_bass),
    ("OPENING JAW", opening_jaw),
    ("CAPTURED SYNC", captured_sync),
    ("PEDAL FOLD", pedal_fold),
    ("CRANKED SUB", cranked_sub),
    ("BASS TO BELL", bass_to_bell),
    ("WHITE STATIC BLOOM", white_static_bloom),
    ("SORTED HISS", sorted_hiss),
    ("SUBMERGED", submerged),
    ("CIRCUIT HAZE", circuit_haze),
    ("BONE GLITTER", bone_glitter),
    ("FROZEN GLISSANDO", frozen_glissando),
    ("CHIPPED GLASS", chipped_glass),
    ("SQUEEZED CHIMES", squeezed_chimes),
    ("GRAIN FOG", grain_fog),
    ("CARVED RUMBLE", carved_rumble),
    ("SWAPPED SKINS", swapped_skins),
    ("SAMPLE SHRED", sample_shred),
    ("JAGGED TRANSFER", jagged_transfer),
    ("SUB UNDER GRAVEL", sub_under_gravel),
    ("SHRAPNEL CRAWL", shrapnel_crawl),
    ("CLANG LEAD", clang_lead),
]
CATEGORY = {ident: "Textures" for ident, _ in TABLES}


def _p(root, name, method):
    return dict(pack=ROOTS[root][1], file=name, method=method)


PROVENANCE = {
    "FUSE WIRE BASS":     _p("SS2", "SOUNDSOURCE Bass - TicTac - (C).wav", "pitch-sync (life)"),
    "OPENING JAW":        _p("SS2", "SOUNDSOURCE Synth - Arm & Leg - (C).wav", "pitch-sync (life)"),
    "CAPTURED SYNC":      _p("CM", "WC - CM - KLAUZINHO18.wav", "pitch-sync + hard sync of the captured cycle"),
    "PEDAL FOLD":         _p("CM", "WC - CM - KLAUZINHO92.wav", "pitch-sync + sine wavefolder"),
    "CRANKED SUB":        _p("ANA", "bass - more dreams.wav", "pitch-sync + asymmetric drive"),
    "BASS TO BELL":       _p("ANA", "bass - deadly sins.wav", "pitch-sync + harmonic stretch"),
    "WHITE STATIC BLOOM": _p("SS2", "SOUNDSOURCE Texture - YE.wav", "spectral-freeze (life)"),
    "SORTED HISS":        _p("ANA", "texture - noise - c.wav", "spectral-freeze, brightness-sorted"),
    "SUBMERGED":          _p("ANA", "accent - underwater.wav", "spectral-freeze (life)"),
    "CIRCUIT HAZE":       _p("CM", "WC - CM - KLAUZINHO3.wav", "spectral-freeze (life)"),
    "BONE GLITTER":       _p("SS2", "SOUNDSOURCE Texture - Wave.wav", "spectral-freeze + peak sparsify"),
    "FROZEN GLISSANDO":   _p("ANA", "accent - harpglissando.wav", "spectral-freeze (life)"),
    "CHIPPED GLASS":      _p("ANA", "accent - shoulderchip.wav", "grain (raw looped chops)"),
    "SQUEEZED CHIMES":    _p("ANA", "accent - digichimes.wav", "grain (squeezed chops)"),
    "GRAIN FOG":          _p("ANA", "accent - sound.wav", "grain (magnitude cloud)"),
    "CARVED RUMBLE":      dict(pack=ROOTS["ANA"][1] + " + " + ROOTS["CM"][1],
                               file="bass - ready for war.wav (body) + WC - CM - KLAUZINHO10.wav (envelope)",
                               method="cross-breed (envelope transplant)"),
    "SWAPPED SKINS":      dict(pack=ROOTS["ANA"][1],
                               file="bass - firefly.wav + texture - gram - c.wav",
                               method="cross-breed (fine/envelope swap)"),
    "SAMPLE SHRED":       _p("CM", "WC - CM - KLAUZINHO70.wav", "cross-breed (sample as phase modulator)"),
    "JAGGED TRANSFER":    _p("ANA", "texture - bellz - c.wav", "cross-breed (sample as transfer curve)"),
    "SUB UNDER GRAVEL":   dict(pack=ROOTS["SS3"][1] + " + " + ROOTS["ANA"][1],
                               file="SOUNDSOURCE Bass - Don't Trip Sub (C).wav + accent - shoulderchip.wav",
                               method="cross-breed (pitch-sync sub + frozen grit top)"),
    "SHRAPNEL CRAWL":     _p("AT1", "ANALOG TOOLS - Textured Warfare - (C).wav", "grain (stretched chops)"),
    "CLANG LEAD":         _p("AT1", "ANALOG TOOLS - Pots & Pans Lead - (C).wav", "pitch-sync + ring modulation"),
}


if __name__ == "__main__":
    import time
    for ident, fn in TABLES:
        t0 = time.time()
        fr = fn()
        ok, line = wtlib.selfcheck(ident, fr)
        print("%s  %.2f s" % (line, time.time() - t0))
