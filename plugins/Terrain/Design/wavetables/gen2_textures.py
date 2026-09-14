"""
gen2_textures.py — Terrain factory library, TEXTURES module A (new tables for the 500, fb638).

Max's OWN Waves Crate one-shots, 2024 + 2025 SOLO packs only: Modular Expression, Pedalphonics Vol. 1,
Creator Crate Vol. 1, Creator Club (Textures / Pedal Thoughts / Onward), Analog Tools 3 + 4, Polybrute
Expressions. No "x <partner>" collaborations, no community/Care Package files, no Splice archive, no
Circuit Motions "KLAUZINHO" files, no files credited to anyone else (e.g. the KEYMAJOR vocal tapes).
Module B (gen2_textures_b.py) owns Circuit Motions and the 2023 packs, so the two never share a source.
The source files are READ-ONLY: this module re-reads them every time it runs and writes nothing near them.
PROVENANCE (bottom of the file) records pack + file + method for every table.

THE THREE IMPORTS
  PITCH-SYNC  a period track (normalised difference function, parabolic peak, median-smoothed) over the
              sound's life; each frame is ONE exact period (or an average of 2-3 consecutive ones),
              spline-sampled to 2048, its start/end drift removed so it loops, band-limited to the
              source's own Nyquist, and rotated so the fundamental has the same phase in every frame
              (the morph glides instead of flickering).
  FREEZE      STFT power (8192 window, several windows averaged) sampled onto the 1024-harmonic grid
              (harmonic k = k * 21.53 Hz): the sound becomes a harmonic comb that keeps its colour.
              ONE fixed phase law per table: wtlib.PHASE, a generalised Schroeder set fitted to the
              table's average spectrum (low crest), or a log chirp that draws the spectrum as a sweep.
  GRAIN       raw 2048-sample chops, the loop closed with a crossfade: pedal grit and aliasing stay in.

THE JOURNEY  In every table the frame axis runs through the sound's life AND drives ONE nameable process
(a pedal or a circuit idea) from nothing to extreme, so frame 0 is Max's sound, recognisable and
playable, and the last frame is something wild. The grain tables are raw grit from frame 0. Eight are
subby: the source's own fundamental is kept under the process so the body survives while the top goes
monstrous (two more, LIMPING OCTAVE and OCTAVE DIVIDER GLASS, grow a new sub-octave as they travel).
Time-domain processes run at 8x oversampling and keep harmonics <= 1000. Deterministic: fixed file
segments and seeded numpy Generators only.
"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import wtlib
import soundfile as sf
from scipy.signal import resample_poly, medfilt
from scipy.interpolate import CubicSpline
from scipy.fft import dct, idct
from scipy.ndimage import gaussian_filter1d

F, NH, SZ, SR = wtlib.FRAMES, wtlib.NH, wtlib.SIZE, wtlib.SR
KH = 1000                                  # keep harmonics <= 1000
OS = 8                                     # oversampling for time-domain processes
N8 = SZ * OS
s = np.linspace(0.0, 1.0, F)               # the frame axis
sc = s[:, None]
KK = np.arange(SZ // 2 + 1)                # rfft bin = harmonic number
kh = np.arange(1, NH + 1, dtype=float)     # harmonic numbers of a magnitude row
T1 = np.arange(SZ) / SZ
T8 = np.arange(N8) / N8
TAU = 2.0 * np.pi

# ── sources (READ-ONLY) ──────────────────────────────────────────────────────────────────────────
WC = "/Users/macshooter/Desktop/Waves Crate/"
_ME = "2025/Waves Crate 1.0/7. MULTI KITS/MACSHOOTER - MODULAR EXPRESSION/"
_PP = "2025/Waves Crate 1.0/7. MULTI KITS/WAVES CRATE - PEDALPHONICS VOL. 1/"
_CC = "2024/7. MULTI KITS/WAVES CRATE - CREATOR CRATE VOL. 1/1. CREATOR = TOOLKIT/"
_CL = "2025/Waves Crate 1.0/8. CREATOR CLUB/WAVES CRATE - CREATOR CLUB/"
_A3 = "2024/4. PRESETS/WAVES CRATE - ANALOG TOOLS 3/"
_A4 = "2025/Waves Crate 1.0/7. MULTI KITS/WAVES CRATE - ANALOG TOOLS 4/"
_PB = "2025/Waves Crate 1.0/7. MULTI KITS/WAVES CRATE - POLYBRUTE EXPRESSIONS/"
ME, PP, CC = "MACSHOOTER - MODULAR EXPRESSION (2025)", "WAVES CRATE - PEDALPHONICS VOL. 1 (2025)", \
    "WAVES CRATE - CREATOR CRATE VOL. 1 (2024)"
CL, A3, A4, PB = "WAVES CRATE - CREATOR CLUB (2025)", "WAVES CRATE - ANALOG TOOLS 3 (2024)", \
    "WAVES CRATE - ANALOG TOOLS 4 (2025)", "WAVES CRATE - POLYBRUTE EXPRESSIONS (2025)"
SRC = {   # key: (pack, path under the Waves Crate root)
    "hardware":   (ME, _ME + "one shots/bass one shots/TRKTRN_MDULRXPRSSN_synth_bass_one_shot_hardware_C.wav"),
    "forever":    (ME, _ME + "one shots/bass one shots/TRKTRN_MDULRXPRSSN_synth_bass_one_shot_forever_C.wav"),
    "young_goat": (ME, _ME + "one shots/bass one shots/TRKTRN_MDULRXPRSSN_synth_bass_one_shot_young_goat_C.wav"),
    "say_less":   (ME, _ME + "one shots/bass one shots/TRKTRN_MDULRXPRSSN_synth_bass_one_shot_say_less_C.wav"),
    "stalker":    (ME, _ME + "one shots/bass one shots/TRKTRN_MDULRXPRSSN_sub_bass_stalker_C.wav"),
    "pacmane":    (ME, _ME + "one shots/synth one shots/TRKTRN_MDULRXPRSSN_synth_key_one_shot_pacmane_C.wav"),
    "vetements":  (ME, _ME + "one shots/synth one shots/TRKTRN_MDULRXPRSSN_synth_textural_one_shot_vetements_C.wav"),
    "driving":    (ME, _ME + "one shots/synth one shots/TRKTRN_MDULRXPRSSN_synth_one_shot_driving_C.wav"),
    "harsh_man":  (ME, _ME + "one shots/synth one shots/TRKTRN_MDULRXPRSSN_synth_one_shot_harsh_man_C.wav"),
    "glo":        (ME, _ME + "one shots/synth one shots/TRKTRN_MDULRXPRSSN_synth_sequencer_one_shot_glo_trials_C.wav"),
    "crazed":     (ME, _ME + "one shots/fx one shots/TRKTRN_MDULRXPRSSN_fx_one_shot_crazed_delay.wav"),
    "numerator":  (ME, _ME + "loops/pad loops/TRKTRN_MDULRXPRSSN_127_synth_pad_loop_numerator_Cmin.wav"),
    "death":      (ME, _ME + "one shots/vox one shots/TRKTRN_MDULRXPRSSN_vox_one_shot_death_sound.wav"),
    "os1_2":      (PP, _PP + "4. PDL + ONE S./ONE S. - WAVES CRATE - OS 1 2.wav"),
    "os5_2b":     (PP, _PP + "4. PDL + ONE S./ONE S. - WAVES CRATE - OS 5 #2.wav"),
    "os2":        (PP, _PP + "4. PDL + ONE S./ONE S. - WAVES CRATE - OS 2.wav"),
    "os4_2":      (PP, _PP + "4. PDL + ONE S./ONE S. - WAVES CRATE - OS 4 #2.wav"),
    "phrase20":   (PP, _PP + "5. PDL + CHPSx R./CHPSx R - WAVES CRATE - PHRASE20.wav"),
    "phrase22":   (PP, _PP + "5. PDL + CHPSx R./CHPSx R - WAVES CRATE - PHRASE22.wav"),
    "misc36":     (PP, _PP + "1. PDL + EAR C./EAR C. - WAVES CRATE - MISC36.wav"),
    "wild_bass":  (CC, _CC + "EPHEMERAL (ONE SHOTS)/! II. BASS ONE SHOTS/WC BASS ONE SHOT - WILD BASSES.wav"),
    "last_stand": (CC, _CC + "EPHEMERAL (ONE SHOTS)/! II. BASS ONE SHOTS/WC BASS ONE SHOT - LAST STAND.wav"),
    "sauron":     (CC, _CC + "EPHEMERAL (ONE SHOTS)/! II. BASS ONE SHOTS/WC BASS ONE SHOT - I THINK (SAURON).wav"),
    "king":       (CC, _CC + "MANIFESTO (DRUM LIBRARY)/VII. EXTRA + FX/WC SOUND - KING.wav"),
    "dub":        (CC, _CC + "MANIFESTO (DRUM LIBRARY)/VII. EXTRA + FX/WC SOUND - DUB CENTRAL.wav"),
    "jupuip":     (CC, _CC + "MANIFESTO (DRUM LIBRARY)/VII. EXTRA + FX/WC SOUND - JUPUIPwav.wav"),
    "barter":     (CC, _CC + "CURIO (PEDAL MEMORIES)/WC PEDAL MEMORIES - BARTER.wav"),
    "messiah":    (CC, _CC + "MOONBOW (ACCENTS)/II. VOCAL + ACCENTS/WC VOCALS - MESSIAH.wav"),
    "maryground": (CL, _CL + "WAVES CRATE - MULTI KIT VOL. 3/WAVES CRATE - PEDAL THOUGHTS/WAVES CRATE - MARYGROUND (BLOOPER + THERMAE).wav"),
    "nile":       (CL, _CL + "WAVES CRATE - MULTI KIT VOL. 3/WAVES CRATE - PEDAL THOUGHTS/WAVES CRATE - NILE (BLOOPER).wav"),
    "flip":       (CL, _CL + "WAVES CRATE - MULTI KIT VOL. 3/WAVES CRATE - PEDAL THOUGHTS/WAVES CRATE - CAN YOU FLIP? (BLOOPER + THERMAE).wav"),
    "chops":      (CL, _CL + "WAVES CRATE - MULTI KIT VOL. 3/WAVES CRATE - PEDAL THOUGHTS/WAVES CRATE - CHOPS (BLOOPER + THERMAE).wav"),
    "bass3":      (CL, _CL + "WAVES CRATE - MULTI KIT VOL. 3/WAVES CRATE - TEXTURES/WAVES CRATE - BASS 3.wav"),
    "bass5":      (CL, _CL + "WAVES CRATE - MULTI KIT VOL. 3/WAVES CRATE - TEXTURES/WAVES CRATE - BASS 5.wav"),
    "mystic":     (CL, _CL + "WAVES CRATE - ONWARD TEXTURES/ONWARD - WAVES CRATE - MYSTIC FEELING.wav"),
    "hollows":    (A3, _A3 + "3. ANALOG TEXTURES/WAVES CRATE - HOLLOWS C# (PHRASE).wav"),
    "ibet":       (A3, _A3 + "5. PEDAL CONSTRUCTS/(CHOP) I BET YOU CANT USE THIS - MICROCOSM.wav"),
    "recorded":   (A3, _A3 + "5. PEDAL CONSTRUCTS/(CHOP) RECORDED - THERMAE + MICROCOSM.wav"),
    "game_over":  (A3, _A3 + "5. PEDAL CONSTRUCTS/(CHOP) GAME OVER - MICROCOSM.wav"),
    "ruler":      (A4, _A4 + "6. INSRMNT SHOTS/AT4 - WAVES CRATE - RULER OF THE WORLD (C).wav"),
    "manipulate": (A4, _A4 + "6. INSRMNT SHOTS/AT4 - WAVES CRATE - MANIPULATIVE (C).wav"),
    "telegram":   (A4, _A4 + "3. MISC TEXTURES/AT4 - WAVES CRATE - TELEGRAM.wav"),
    "palmer":     (A4, _A4 + "3. MISC TEXTURES/AT4 - WAVES CRATE - KING PALMER.wav"),
    "wired":      (PB, _PB + "2. POLBRUTE EXPRESSIONS/IV. NOISE/WAVES CRATE - WIRED.wav"),
}
_AUDIO = {}


def load(key):
    """Mono float64 at 44.1 kHz, DC removed. Cached per process; the file is only ever READ."""
    if key not in _AUDIO:
        x, sr = sf.read(WC + SRC[key][1], dtype="float64", always_2d=True)
        x = x.mean(axis=1)
        sr = int(sr)
        if sr != SR:
            g = int(np.gcd(sr, SR))
            x = resample_poly(x, SR // g, sr // g)
        _AUDIO[key] = x - x.mean()
    return _AUDIO[key]


# ── small tools ──────────────────────────────────────────────────────────────────────────────────
def expo(a, b, c=1.0):
    return a * (b / a) ** (s ** c)


def pk(fr):
    fr = np.asarray(fr, dtype=float)
    return fr / np.maximum(np.abs(fr).max(axis=1, keepdims=True), 1e-12)


def spec(fr):
    return np.fft.rfft(fr, axis=1)


def wave(H):
    return np.fft.irfft(H, n=SZ, axis=1)


def up(fr):
    """(F, SZ) periodic frames -> (F, SZ*OS) by spectral zero-padding (exact band-limited upsample)."""
    X = np.fft.rfft(fr, axis=1)
    Y = np.zeros((fr.shape[0], N8 // 2 + 1), complex)
    Y[:, :X.shape[1]] = X * OS
    return np.fft.irfft(Y, n=N8, axis=1)


def down(y, hmax=KH):
    """(F, n*SZ) -> (F, SZ), brick-walled at hmax harmonics (and DC removed)."""
    Y = np.fft.rfft(y, axis=1)[:, :SZ // 2 + 1] / (y.shape[1] // SZ)
    Y[:, 0] = 0
    Y[:, hmax + 1:] = 0
    return np.fft.irfft(Y, n=SZ, axis=1)


def read(c, w):
    """Read periodic cycles c (F, n) at phase w (cycles, any shape broadcastable to c): linear interp."""
    n = c.shape[1]
    p = np.broadcast_to(np.mod(w, 1.0) * n, c.shape)
    i0 = np.floor(p).astype(np.int64)
    fr = p - i0
    i0 %= n
    i1 = (i0 + 1) % n
    a = np.take_along_axis(c, i0, axis=1)
    b = np.take_along_axis(c, i1, axis=1)
    return a + (b - a) * fr


def keep_sub(y, ref, ks=1, lvl=1.0):
    """Put the SOURCE's own bins 1..ks under a processed table: the body survives, the top is the process.
    The sub is scaled so its strongest bin is lvl x the strongest processed bin above ks."""
    Y, R = spec(y), spec(ref)
    top = np.abs(Y[:, ks + 1:]).max(axis=1)
    sub = np.abs(R[:, 1:ks + 1]).max(axis=1)
    Y[:, 1:ks + 1] = R[:, 1:ks + 1] * (lvl * top / np.maximum(sub, 1e-12))[:, None]
    return wave(Y)


def smooth_frames(H, sig):
    if sig <= 0:
        return H
    return gaussian_filter1d(H.real, sig, axis=0, mode="nearest") + 1j * gaussian_filter1d(H.imag, sig, axis=0, mode="nearest")


def best_roll(fr):
    """Rotate EVERY frame by the same amount so the wrap lands where the table is quietest. A rotation leaves
    the magnitude spectrum, every metric and the fingerprint unchanged (wtkit's own advice for step waves)."""
    x = np.asarray(fr, dtype=float)
    best, br = np.inf, 0
    for r in range(0, SZ, SZ // 32):
        y = np.roll(x, r, axis=1)
        d = np.abs(np.diff(y, axis=1))
        typ = np.partition(d, d.shape[1] - 20, axis=1)[:, -20:].mean(axis=1)
        sr = float((np.abs(y[:, 0] - y[:, -1]) / np.maximum(typ, 1e-12)).max())
        if sr < best:
            best, br = sr, r
    return np.roll(x, br, axis=1)


def fin(fr, roll=0):
    fr = np.asarray(fr, dtype=float)
    if roll:
        fr = np.roll(fr, roll, axis=1)
    return wtlib.finalize(fr)


# ── PITCH-SYNC ───────────────────────────────────────────────────────────────────────────────────
def _period(x, c, P0, lo=0.82, hi=1.22):
    """Period (fractional samples) near sample c: normalised difference function, minimum searched in
    [lo, hi] x P0 (no octave errors), parabolic refinement."""
    tmax = int(P0 * hi) + 2
    W = max(int(2.5 * P0), 1024)
    a = int(c - W // 2)
    a = max(0, min(a, len(x) - W - tmax - 2))
    seg = x[a:a + W + tmax]
    n = 1 << int(np.ceil(np.log2(2 * (W + tmax))))
    r = np.fft.irfft(np.conj(np.fft.rfft(seg[:W], n)) * np.fft.rfft(seg, n), n)[:tmax + 1]
    cs = np.concatenate([[0.0], np.cumsum(seg * seg)])
    tau = np.arange(tmax + 1)
    E = cs[W] + cs[tau + W] - cs[tau]
    d = (E - 2 * r) / np.maximum(E, 1e-18)
    t0, t1 = max(2, int(P0 * lo)), min(int(P0 * hi), tmax - 1)
    j = t0 + int(np.argmin(d[t0:t1 + 1]))
    y0, y1, y2 = d[j - 1], d[j], d[j + 1]
    den = y0 - 2 * y1 + y2
    off = 0.5 * (y0 - y2) / den if abs(den) > 1e-12 else 0.0
    return j + float(np.clip(off, -0.5, 0.5))


def _grab(x, t, L):
    """L samples (fractional) from t, spline-sampled to 2048 points, start/end drift removed -> rfft."""
    i0 = int(np.floor(t)) - 3
    i1 = int(np.ceil(t + L)) + 5
    seg = np.zeros(i1 - i0)
    a, b = max(i0, 0), min(i1, len(x))
    if b > a:
        seg[a - i0:b - i0] = x[a:b]
    y = CubicSpline(np.arange(i0, i1), seg)(t + L * np.arange(SZ + 1) / SZ)
    y = y[:SZ] - (y[SZ] - y[0]) * np.arange(SZ) / SZ
    return np.fft.rfft(y)


def _rot(h, ref_bin, target=-np.pi / 2):
    return h * np.exp(1j * KK * (target - np.angle(h[ref_bin])) / ref_bin)


def _xalign(h, prev):
    c = np.fft.irfft(h * np.conj(prev), n=SZ)
    return h * np.exp(TAU * 1j * KK * int(np.argmax(c)) / SZ)


def psync(key, f0, t0, t1, curve=1.0, navg=2, nper=1, smooth=1.0, hcap=KH, pos=None):
    """(H, hmax): 128 aligned spectra. Frame i holds `nper` consecutive periods found near position
    t0 + (t1-t0) s^curve (seconds), averaged over `navg` successive grabs; fundamental phase fixed.
    nper may be an array (one per frame): the fundamental of the SOURCE then sits at bin nper."""
    x = load(key)
    P0 = SR / f0
    a, b = int(t0 * SR), int(t1 * SR)
    npm = int(np.max(nper)) if np.ndim(nper) else int(nper)
    b = min(b, len(x) - int((navg + npm + 3) * P0) - 16)
    anchors = np.linspace(a, b, 64)
    Ps = medfilt(np.array([_period(x, c, P0) for c in anchors]), 5)
    if pos is None:
        pos = a + (b - a) * s ** curve
    else:
        pos = a + (b - a) * np.asarray(pos)
    P = np.interp(pos, anchors, Ps)
    NP = np.broadcast_to(np.asarray(nper), (F,)).astype(int)
    H = np.zeros((F, SZ // 2 + 1), complex)
    prev = None
    for i in range(F):
        rb = NP[i]
        acc = np.zeros(SZ // 2 + 1, complex)
        for j in range(navg):
            h = _grab(x, pos[i] + j * P[i] * NP[i], P[i] * NP[i])
            m = np.abs(h[1:rb * 8 + 1])
            if np.abs(h[rb]) >= 0.15 * m.max() or prev is None:
                h = _rot(h, rb)
            else:
                h = _xalign(h, prev)
            acc += h
        H[i] = acc / navg
        prev = H[i]
    hmax = int(min(hcap, 0.97 * NP.min() * P.min() / 2 if NP.min() == NP.max() else 0.97 * P.min() / 2))
    H[:, 0] = 0
    # per-frame Nyquist of the source (a frame holding n periods has n*P/2 usable bins)
    lim = np.minimum(hcap, (0.97 * NP * P / 2).astype(int))
    for i in range(F):
        H[i, lim[i] + 1:] = 0
    H = smooth_frames(H, smooth)
    return H, int(lim.min()) if hmax <= 0 else hmax


# ── FREEZE ───────────────────────────────────────────────────────────────────────────────────────
def spectro(key, t0, t1, n=F, W=8192, nav=3, curve=1.0, ratio=None, hop_div=4):
    """(n, NH) magnitudes: STFT power around n centres over [t0, t1] s, averaged over nav windows,
    sampled on the harmonic grid (harmonic k = k*SR/2048 Hz, times `ratio[i]` when given)."""
    x = load(key)
    hop = W // hop_div
    win = np.hanning(W)
    xp = np.pad(x, (W + nav * hop, W + nav * hop))
    cs = t0 * SR + (t1 - t0) * SR * np.linspace(0, 1, n) ** curve
    r = W / SZ                             # FFT bins per harmonic
    out = np.zeros((n, NH))
    for i, c in enumerate(cs):
        acc = np.zeros(W // 2 + 1)
        for j in range(nav):
            o = int(c + W + nav * hop + (j - (nav - 1) / 2) * hop - W // 2)
            acc += np.abs(np.fft.rfft(xp[o:o + W] * win)) ** 2
        acc /= nav
        rr = 1.0 if ratio is None else ratio[i]
        # band-average the power over +-r/2 bins around each (possibly scaled) harmonic
        cum = np.concatenate([[0.0], np.cumsum(acc)])
        lo = np.clip(r * kh * rr - r * rr / 2, 0, W // 2)
        hi = np.clip(r * kh * rr + r * rr / 2, 0, W // 2)
        seg = np.interp(hi, np.arange(len(cum)), cum) - np.interp(lo, np.arange(len(cum)), cum)
        out[i] = np.sqrt(np.maximum(seg, 0) / np.maximum(hi - lo, 1e-9))
    out[:, KH:] = 0
    return out


def mags_frames(M, phase=None):
    M = np.array(M, dtype=float)
    M[:, KH:] = 0
    return wtlib.cycles_from_mags(M, phase)


def phase_for(M):
    """ONE fixed phase set for a whole table (so the morph glides), chosen for a low crest factor: the
    generalised Schroeder law for the table's AVERAGE power distribution p (Schroeder 1970):
    phi_k = -2 pi sum_{l<k} (k - l) p_l. Each harmonic's group delay sits where its share of energy lands."""
    P = np.asarray(M, dtype=float) ** 2
    p = (P / np.maximum(P.sum(axis=1, keepdims=True), 1e-30)).mean(axis=0)
    p = p / max(p.sum(), 1e-30)
    C = np.concatenate([[0.0], np.cumsum(p)[:-1]])
    D = np.concatenate([[0.0], np.cumsum(kh * p)[:-1]])
    return -TAU * (kh * C - D)


def chirp_phase(t0=0.08, t1=0.92, rising=True):
    """ONE fixed phase set that parks each harmonic's energy at its own moment inside the cycle: the group
    delay sweeps log-linearly from t0 (harmonic 1) to t1 (harmonic 1000), or the reverse. Every frame then
    DRAWS its frozen spectrum as a sweep through the cycle — rumble at one end, fizz at the other — and the set
    never moves, so the morph glides. (x = sum A_k cos(2 pi k t + phi_k) arrives at t = tau_k, phi_k = -2 pi
    sum_{l<=k} tau_l.)"""
    lk = np.log2(kh) / np.log2(KH)
    tau = t0 + (t1 - t0) * (lk if rising else 1.0 - lk)
    return -TAU * np.cumsum(tau)


def limit(fr, c=2.6):
    """Output stage: a soft limiter at c x the frame's RMS (at 8x, band-limited), so a spiky frame does not
    play quiet at equal peak."""
    x = up(pk(fr))
    r = np.sqrt((x ** 2).mean(axis=1, keepdims=True))
    return down(c * r * np.tanh(x / (c * r)))


def limit_spikes(fr, lo=2.6, k=3.0, passes=2):
    """limit() with a PER-FRAME ceiling, re-measured each pass: c = max(lo, lo + k (4 - crest)). A frame
    already under crest 4 gets a loose ceiling (barely touched, its spectrum stays); a needle frame gets lo
    and is squeezed pass after pass until it plays at the level of its neighbours."""
    y = np.asarray(fr, dtype=float)
    for _ in range(passes):
        x = up(pk(y))
        cr = np.abs(x).max(axis=1) / np.maximum(np.sqrt((x ** 2).mean(axis=1)), 1e-12)
        y = limit(y, np.maximum(lo, lo + k * (4.0 - cr))[:, None])
    return y


def envelope_of(M, ncoef=30):
    """Cepstral (DCT-of-log) spectral envelope of magnitude rows."""
    fl = 1e-6 * np.maximum(M.max(axis=1, keepdims=True), 1e-30)
    L = np.log(np.maximum(M, fl))
    C = dct(L, type=2, norm="ortho", axis=1)
    C[:, ncoef:] = 0
    return np.exp(idct(C, type=2, norm="ortho", axis=1))


def norm_rows(M):
    return M / np.maximum(M.max(axis=1, keepdims=True), 1e-30)


# ── GRAIN ────────────────────────────────────────────────────────────────────────────────────────
def chop(x, p, L=SZ, X=192):
    """Raw chop of L samples at p; the X samples after it crossfaded into its head (loop closed);
    fitted into one 2048 cycle by FFT (L != 2048 = time-scaled), brick-walled at KH."""
    p = int(max(0, min(p, len(x) - L - X - 1)))
    seg = x[p:p + L + X]
    y = seg[:L].copy()
    w = np.sin(0.5 * np.pi * (np.arange(X) + 0.5) / X) ** 2
    y[:X] = y[:X] * w + seg[L:L + X] * (1 - w)
    Y = np.fft.rfft(y)
    Z = np.zeros(SZ // 2 + 1, complex)
    n = min(len(Y), SZ // 2 + 1)
    Z[:n] = Y[:n] * (SZ / L)
    Z[0] = 0
    Z[KH + 1:] = 0
    return np.fft.irfft(Z, n=SZ)


def loud_positions(x, t0, t1, L=SZ, within_db=24.0, curve=1.0, walk=False):
    """128 chop starts through [t0, t1] s, each nudged to the nearest spot within `within_db` of the
    file's peak 10 ms RMS (so no frame is a near-silent dip that peak-normalises into hiss).
    walk=True: march through LOUD time instead — the 128 starts are spread evenly over the loud samples
    only (quantiles of the loud set), so a quiet gap is skipped rather than snapped to. The default
    nudge sends every frame that falls in a gap (or past the last loud sample) to the SAME start, which
    freezes runs of identical frames on short, gappy hits; walk=True gives every frame its own grain.
    (The default is kept for the tables whose loud set has no gap, so they stay byte-identical.)"""
    e = np.sqrt(np.convolve(x * x, np.ones(441) / 441, "same"))
    ok = np.flatnonzero(e > e.max() * 10 ** (-within_db / 20))
    ok = ok[(ok >= t0 * SR) & (ok <= t1 * SR - L)]
    want = t0 * SR + (t1 * SR - L - t0 * SR) * s ** curve
    if len(ok) == 0:
        return want.astype(int)
    if walk:
        return ok[np.round((len(ok) - 1) * s ** curve).astype(int)]
    idx = np.searchsorted(ok, want).clip(0, len(ok) - 1)
    return ok[idx]


# ═════════════════════════════════════════════════════════════════════════════════════════════════
#  THE TABLES
# ═════════════════════════════════════════════════════════════════════════════════════════════════

# ── SUBBY: the bass keeps its body, the process eats the top ──────────────────────────────────────
def self_devouring_sub():
    """Modular Expression 'hardware' bass (C1): pitch-synced over its life, then each cycle READS ITSELF —
    the phase it is played back at is pushed by its own value (self phase-modulation, two feedback passes),
    depth 0 -> 0.9 cycles. The 33 Hz body is kept under it; the top curls into a shredded, self-eating snarl."""
    H, _ = psync("hardware", 32.59, 0.07, 2.55, navg=2, smooth=1.0)
    c = pk(wave(H))
    c8 = up(c)
    beta = (0.003 + 0.9 * s ** 1.5)[:, None]
    y = read(c8, T8[None, :] + beta * c8)
    y = read(c8, T8[None, :] + beta * y)
    y = down(y)
    return fin(keep_sub(y, c, ks=1, lvl=1.3))


def upside_down_bass():
    """Creator Crate 'Wild Basses' (C1, up to 650 harmonics): the levels of harmonics 1..K are turned UPSIDE
    DOWN — harmonic k takes the level of harmonic K+1-k — while K sweeps from 2 to the bass's full series as it
    swells. The strong bottom of the bass is thrown to the top: a rising ramp of overtones slamming into a
    wall that climbs the spectrum. Each harmonic keeps its own phase; the real sub is kept underneath."""
    H, hm = psync("wild_bass", 32.54, 0.35, 3.1, navg=2, smooth=1.0)
    K = np.round(expo(2.0, float(hm), 1.0)).astype(int)
    Hn = H.copy()
    mag = np.abs(H)
    ph = np.exp(1j * np.angle(H))
    for i in range(F):
        k = np.arange(1, K[i] + 1)
        Hn[i, k] = mag[i, K[i] + 1 - k] * ph[i, k]
    return fin(keep_sub(wave(Hn), wave(H), ks=1, lvl=1.0))


def limping_octave():
    """Modular Expression 'forever' bass (C1): every frame holds TWO of its cycles, and the second one limps —
    an amplitude and phase wobble at half the pitch grows 0 -> total. Frame 0 is the bass an octave up;
    as the limp grows a sub-octave rises out of nowhere and the table falls an octave into a lurching growl."""
    H, _ = psync("forever", 32.72, 0.8, 3.75, navg=2, smooth=1.0)
    c8 = up(pk(wave(H)))
    a = (0.98 * s ** 1.3)[:, None]
    env = 1.0 + a * np.cos(TAU * T8)[None, :]
    ph = 0.22 * a * np.sin(TAU * T8)[None, :]
    y = env * read(c8, 2.0 * T8[None, :] + ph)
    y = y + (0.6 * a ** 2) * read(c8, 2.0 * T8[None, :] + 0.5 * ph) ** 3
    return fin(down(y))


def octave_fuzz_undertow():
    """Modular Expression 'young goat' bass (C1) through an octave-up rectifier fuzz, three stages deep: dry ->
    full-wave rectified (an octave up) -> rectified again around its middle (two up) -> and again (three up,
    chattering edges), each stage folding in over a third of the table. The fuzz is high-passed away from
    the bottom as it grows, and the goat's own fundamental is kept at full weight: a clean sub with a gap
    above it and a screaming octave-fuzz floating on top."""
    H, _ = psync("young_goat", 32.75, 0.05, 3.35, navg=2, smooth=1.0)
    c = pk(wave(H))
    x = up(c)
    amt = (3.0 * s)[:, None]
    y = x
    for st, off in ((0, 0.0), (1, 0.35), (2, 0.3)):
        a = np.clip(amt - st, 0.0, 1.0)
        r = np.abs(y - off * np.abs(y).max(axis=1, keepdims=True))
        y = (1 - a) * y + a * (r - r.mean(axis=1, keepdims=True))
        y = y / np.maximum(np.abs(y).max(axis=1, keepdims=True), 1e-9)
    y = np.tanh(expo(1.0, 6.0, 1.0)[:, None] * y)
    Y = spec(down(y))
    kc = (1.0 + 5.0 * s ** 1.2)[:, None]
    Y *= 1.0 - np.exp(-(KK[None, :] / kc) ** 4)
    return fin(keep_sub(wave(Y), c, ks=1, lvl=1.2))


def chebyshev_rumble():
    """Modular Expression 'say less' sub (C1), almost a sine, fed to a Chebyshev shaper cos(n acos x) whose
    order n climbs 1 -> 31 (through the fractional orders, so it shears rather than steps). The round sub
    becomes a comb of its own n-th harmonic, then a lattice of folded sub-cycles. Sub kept."""
    H, _ = psync("say_less", 32.62, 0.05, 4.15, navg=2, smooth=1.0)
    c = pk(wave(H))
    x = np.clip(up(c), -1.0, 1.0)
    n = expo(1.0, 48.0, 1.1)[:, None]
    y = np.cos(n * np.arccos(x))
    return fin(keep_sub(down(y), c, ks=1, lvl=1.2))


def crushed_stalker():
    """Modular Expression 'stalker' sub (B0) through a sample-rate + bit crusher with a failing clock:
    9 -> 1.3 bits, a sample-hold of 1 -> 200 (oversampled) steps per cycle, and the hold lengths jittering
    up to +-90 % (seeded). The deep sine turns into a staircase, then an uneven, stuttering digital ruin —
    the true sub stays underneath."""
    H, _ = psync("stalker", 31.70, 0.55, 2.3, navg=2, smooth=1.0)
    c = pk(wave(H))
    x = up(c)
    q = expo(256.0, 1.25, 1.0)
    hold = expo(1.0, 200.0, 1.1)
    jit = 0.9 * s ** 1.5
    u = np.random.default_rng(606).uniform(-1, 1, N8)
    y = np.empty_like(x)
    for i in range(F):
        z = np.round(x[i] * q[i]) / q[i]
        L = np.maximum(1, np.round(hold[i] * (1 + jit[i] * u))).astype(int)
        starts = np.concatenate([[0], np.cumsum(L)])
        starts = starts[starts < N8]
        idx = starts[np.searchsorted(starts, np.arange(N8), side="right") - 1]
        y[i] = z[idx]
    return fin(best_roll(keep_sub(down(y), c, ks=1, lvl=1.2)))


def knee_bent_bass():
    """Creator Crate 'I Think (Sauron)' bass (C1) read through a phase-distortion knee (the Casio trick on a
    sampled cycle): the read position rushes through the first part of the cycle and crawls through the rest,
    knee 0.5 -> 0.012, applied twice. The warm sub is pulled into a resonant, ripping saw-like tear. Sub kept."""
    H, _ = psync("sauron", 32.69, 0.45, 3.45, navg=2, smooth=1.0)
    c = pk(wave(H))
    c8 = up(c)
    d = expo(0.5, 0.012, 1.0)[:, None]
    t = T8[None, :]
    w = np.where(t < d, 0.5 * t / d, 0.5 + 0.5 * (t - d) / (1 - d))
    w2 = np.where(w < d, 0.5 * w / d, 0.5 + 0.5 * (w - d) / (1 - d))
    y = read(c8, w2)
    # an output stage tames the knee's needle (crest), and the wrap is rolled to the quietest point
    return fin(best_roll(limit(keep_sub(down(y), c, ks=1, lvl=1.1), 3.0)))


def crinkled_tape_sub():
    """Creator Club 'Bass 5' drone (C1, 14 s): its cycle played back from a tape that is being crinkled —
    a periodic random warp of the read position whose depth (0 -> 0.2 cycles) grows while the creases get
    finer (warp harmonics 1-3 at first, 12-90 at the end). A smooth sub turning into a creased, buzzing,
    torn ribbon whose sidebands fly away from the untouched sub."""
    H, _ = psync("bass5", 32.79, 0.1, 14.4, navg=2, smooth=1.5)
    c = pk(wave(H))
    c8 = up(c)
    rng = np.random.default_rng(5501)
    K = np.arange(1, 97)
    amp = rng.normal(0, 1, 96) / K ** 0.4
    phs = rng.uniform(0, TAU, 96)
    M = np.round(expo(3.0, 90.0, 1.0)).astype(int)
    M0 = np.round(expo(1.0, 12.0, 1.0)).astype(int)
    depth = (0.001 + 0.2 * s ** 1.4)
    y = np.empty_like(c8)
    for i in range(F):
        m = M[i]
        a0 = M0[i] - 1
        nz = (amp[a0:m, None] * np.sin(TAU * K[a0:m, None] * T8[None, :] + phs[a0:m, None])).sum(0)
        nz /= np.abs(nz).max()
        y[i] = read(c8[i:i + 1], T8[None, :] + depth[i] * nz[None, :])[0]
    return fin(keep_sub(down(y), c, ks=1, lvl=1.1), roll=SZ // 3)


def feedback_flange_drone():
    """Pedalphonics ear-candy MISC36 (C2, 156 harmonics): pitch-synced over its 7 s swell and run through
    a feedback flanger inside the cycle — delay 1/2 -> 1/300 cycle, feedback 0.2 -> 0.96. The warm swell
    grows resonant comb teeth that sharpen into a screaming metallic ladder."""
    H, _ = psync("misc36", 66.21, 1.2, 7.35, navg=2, smooth=1.0)
    d = expo(0.5, 1.0 / 300, 1.0)[:, None]
    g = (0.2 + 0.76 * s ** 0.8)[:, None]
    Hc = H / (1.0 - g * np.exp(-1j * TAU * KK[None, :] * d))
    return fin(wave(Hc))


def hollows_formant_walk():
    """Analog Tools 3 'Hollows' (C#1, 211 harmonics): pitch-synced over its life, its spectral ENVELOPE
    (cepstral, 40 coefficients) separated from the harmonics and slid upward 1x -> 7x while the harmonics
    stay put. The low drone opens like a mouth, then squeaks — a formant walking out of a bass."""
    H, hm = psync("hollows", 34.75, 0.18, 3.4, navg=2, smooth=1.0)
    M = np.abs(H[:, 1:NH + 1])
    E = envelope_of(M + 1e-9 * M.max(), 40)
    fine = H[:, 1:NH + 1] / np.maximum(E, 1e-12)
    alpha = expo(1.0, 7.0, 1.0)
    En = np.array([np.interp(kh / a, kh, E[i]) for i, a in enumerate(alpha)])
    Hn = np.zeros_like(H)
    Hn[:, 1:NH + 1] = fine * En
    Hn[:, hm + 1:] = 0
    return fin(wave(Hn))


def faceted_last_stand():
    """Creator Crate 'Last Stand' bass (C2): each frame is the bass cycle CLONED into N facets
    (N 1 -> 14, fractional counts crossfaded), each facet a compressed copy with its own level. A single
    swelling bass becomes a fan of stacked, stepped mini-cycles — geometry made of Max's bass."""
    H, _ = psync("last_stand", 65.24, 1.0, 7.95, navg=2, smooth=1.0)
    c8 = up(pk(wave(H)))
    Nf = expo(1.0, 14.0, 1.2)
    out = np.empty_like(c8)
    for i in range(F):
        lo = int(np.floor(Nf[i])); fr = Nf[i] - lo
        acc = 0
        for n, wgt in ((lo, 1 - fr), (lo + 1, fr)):
            if wgt <= 0:
                continue
            j = np.floor(n * T8).astype(int)
            gains = 1.0 - 0.8 * np.sin(np.pi * (j + 0.5) / n) ** 2
            acc = acc + wgt * gains * read(c8[i:i + 1], (n * T8)[None, :])[0]
        out[i] = acc
    return fin(down(out))


def _shift_bins(H, d):
    """Move every harmonic up by d slots (fractional d: the two neighbouring integer shifts crossfaded)."""
    lo = int(np.floor(d)); fr = d - lo
    out = np.zeros_like(H)
    for dd, wgt in ((lo, 1 - fr), (lo + 1, fr)):
        if wgt > 0 and dd < SZ // 2:
            out[1 + dd:] += wgt * H[1:SZ // 2 + 1 - dd]
    return out


def shifted_undertow():
    """Creator Club 'Bass 3' (C2, a pure sub swell) through a FREQUENCY SHIFTER WITH FEEDBACK (the Bode
    barber-pole trick on the harmonic grid): the bass is shifted up by 6 -> 37 slots and fed back into the
    shifter, feedback 0 -> 0.93, so copy after copy stacks up the spectrum. The sub stays put under a
    rising, spiralling ladder of its own ghosts. (Output stage: the ghosts pile up in phase into a needle at
    high feedback, so the frames go through limit_spikes — the last frames play at level, not 17 dB down.)"""
    H, _ = psync("bass3", 65.45, 3.0, 13.6, navg=2, smooth=1.5, hcap=400)
    sh = expo(6.0, 37.0, 1.0)
    g = 0.93 * s ** 0.9
    Hn = np.zeros_like(H)
    for i in range(F):
        Y = H[i].copy()
        for _ in range(40):
            Y = H[i] + g[i] * np.exp(1j * 0.9) * _shift_bins(Y, sh[i])
        Hn[i] = Y
    Hn[:, KH + 1:] = 0
    return fin(limit_spikes(keep_sub(wave(Hn), wave(H), ks=1, lvl=1.0), 2.6, 3.0, 2))


def self_composed():
    """Analog Tools 4 'Manipulative' instrument (C3, 168 harmonics): each cycle COMPOSED WITH ITSELF — the
    cycle's own value picks where in the cycle to read next, iterated three times with a gain 0 -> 2.6
    (a chaotic map made of Max's waveform). The warm instrument folds into nested, fractal copies of its
    own shape."""
    H, _ = psync("manipulate", 131.16, 0.2, 3.8, navg=2, smooth=1.0)
    c8 = up(pk(wave(H)))
    g = (2.6 * s ** 1.2)[:, None]
    a = np.clip(4 * s, 0, 1)[:, None]
    y = c8
    for _ in range(3):
        y = read(c8, T8[None, :] + 0.5 * g * y)
    return fin(down((1 - a) * c8 + a * y))


def chirped_pedal():
    """Pedalphonics one-shot OS 2 (C3): each period is played back at a speed that ACCELERATES inside the
    cycle (read position t^gamma, gamma 1 -> 6) while the pedal note opens over its 12 s life. A plain pedal
    cycle bent into a chirp — lazy at the start of every period, a whipcrack at the end."""
    H, _ = psync("os2", 131.88, 0.6, 11.95, navg=2, smooth=1.2)
    c8 = up(pk(wave(H)))
    gam = expo(1.0, 10.0, 0.8)[:, None]
    w = T8[None, :] ** gam
    return fin(down(read(c8, w)), roll=SZ // 2)


# ── mid / high pitched: Max's pedal and synth notes, pushed ───────────────────────────────────────
def pedal_air_exciter():
    """Pedalphonics one-shot OS 1 (C4 pedal drone, 53 st of its own travel) and a band-replicating exciter:
    its top octave is copied upward again and again to harmonic 1000, the roll-off of the copies relaxing
    from -40 dB/octave to -2. A dark pedal note that ends up breathing blinding air."""
    H, hm = psync("os1_2", 261.63, 0.4, 7.9, navg=2, smooth=1.0)
    M = np.abs(H)
    slope = expo(40.0, 1.0, 0.55)
    rng = np.random.default_rng(1101)
    ph = rng.uniform(0, TAU, SZ // 2 + 1)
    Hn = H.copy()
    for i in range(F):
        # the note's real top: its highest harmonic within 45 dB of its peak
        top = int(np.flatnonzero(M[i, 1:hm + 1] > M[i].max() * 10 ** (-45 / 20)).max()) + 1
        top = max(top, 6)
        base = top // 2
        k = np.arange(top + 1, KH + 1)
        src = base + ((k - base) % (top - base))
        g = 10 ** (-slope[i] / 20 * np.log2(k / top))
        Hn[i, top + 1:] = 0
        Hn[i, k] = M[i, src] * g * np.exp(1j * ph[k])
    return fin(wave(Hn))


def stacked_periods():
    """Pedalphonics one-shot OS 5 #2 (C3 pedal note, 10 s): each frame holds N CONSECUTIVE periods of the
    note (N 1 -> 16), so the cycle-to-cycle life of the pedal — its wobble, grit and chorus — becomes the
    waveform. The pitch climbs as N grows and the table fills with the in-between harmonics of its flutter."""
    NP = np.clip(np.round(expo(1.0, 16.0, 0.9)), 1, 16).astype(int)
    H, _ = psync("os5_2b", 130.53, 0.5, 9.3, navg=1, nper=NP, smooth=0.8)
    return fin(wave(H))


def octave_divider_glass():
    """Modular Expression 'pacmane' key (C5, a glassy swell) through an analog octave divider three flip-flops
    deep: each frame holds eight periods, and the note is ring-multiplied by square waves at 1/2, then 1/4,
    then 1/8 of its pitch (the sub-octave circuit), each stage folding in over a third of the table. A glass
    key that drops three octaves into a gnarly, gated organ-bass."""
    H, _ = psync("pacmane", 525.49, 0.6, 4.4, navg=1, nper=8, smooth=1.0)
    x = up(pk(wave(H)))
    y = x
    for st, (bn, ph) in enumerate(((4, 0.3), (2, 0.7), (1, 1.1))):
        a = np.clip(3.0 * s - st, 0, 1)[:, None]
        sq = np.sign(np.sin(TAU * bn * T8 + ph))[None, :]
        y = y * (1 - a + a * sq)
    a3 = np.clip(3.0 * s - 2, 0, 1)[:, None]
    y = y + (0.4 * a3) * np.abs(x).mean(axis=1, keepdims=True) * np.sign(np.sin(TAU * T8 + 1.1))[None, :]
    return fin(down(y))


def wah_stomp_phrase():
    """Pedalphonics chop PHRASE20 (D#3), pitch-synced over its swell, into a FUZZ -> WAH chain: the fuzz
    (gain 1 -> 20) spreads the phrase up to harmonic 1000, and the wah's resonant peak rides that field from
    harmonic 6 to 300 while its Q climbs and its gain goes from a vowel (+12 dB) to +40 dB. The phrase talks,
    then squeals through one needle-sharp formant in the middle of its own fuzz."""
    H, hm = psync("phrase20", 155.31, 0.8, 7.75, navg=2, smooth=1.0)
    x = up(pk(wave(H)))
    drv = expo(1.0, 20.0, 0.8)[:, None]
    Fz = spec(down(np.tanh(drv * x)))
    kc = expo(6.0, 300.0, 1.0)[:, None]
    bw = expo(0.35, 0.05, 1.0)[:, None]
    A = 10 ** (expo(12.0, 40.0, 1.0) / 20)[:, None]
    lk = np.log(np.maximum(KK, 1)[None, :] / kc)
    G = 1.0 + A * np.exp(-0.5 * (lk / bw) ** 2)
    return fin(wave(Fz * G))


def shimmer_cascade():
    """Analog Tools 4 'Ruler of the World' (C3): a shimmer — the pitch-shifting-delay trick — inside one cycle,
    tuned to ODD intervals: the spectrum is fed back through two shifters, a twelfth up (x3) and a
    seventeenth up (x5), again and again, feedback 0 -> 0.8. Every partial spawns a hollow, clarinet-like
    lattice of its odd overtones up to harmonic 1000: a dark note that ends as a glass pipe organ."""
    H, hm = psync("ruler", 130.92, 0.45, 2.8, navg=2, smooth=1.0)
    M = np.abs(H[:, 1:NH + 1])
    g = (0.8 * s ** 0.55)[:, None]
    Y = M.copy()
    for _ in range(8):
        S3 = np.zeros_like(Y)
        S3[:, 2::3] = Y[:, :NH // 3]         # harmonic k -> 3k
        S5 = np.zeros_like(Y)
        S5[:, 4::5] = Y[:, :NH // 5]         # harmonic k -> 5k
        Y = M + g * S3 + 0.8 * g * S5
    ph = np.where(M > 1e-6 * M.max(), np.angle(H[:, 1:NH + 1]), wtlib.PHASE[None, :])
    Hn = np.zeros_like(H)
    Hn[:, 1:NH + 1] = Y * np.exp(1j * ph)
    Hn[:, KH + 1:] = 0
    return fin(wave(Hn))


def audio_rate_vibrato():
    """Modular Expression 'vetements' texture (C4): its cycle read with a vibrato so fast it lives INSIDE the
    cycle — the read position swings m times per period, m climbing 1 -> 12 (neighbouring rates crossfaded)
    while the depth grows 0 -> 10 radians. Sidebands bloom around every harmonic, then spread into a wide,
    gappy comb: the soft texture becomes a warbling, metallic FM tangle."""
    H, _ = psync("vetements", 261.21, 0.6, 4.55, navg=2, smooth=1.0)
    c8 = up(pk(wave(H)))
    mm = expo(1.0, 12.0, 1.0)
    I = 10.0 * s ** 1.1
    out = np.empty_like(c8)
    for i in range(F):
        lo = int(np.floor(mm[i])); fr = mm[i] - lo
        acc = 0
        for m, wgt in ((lo, 1 - fr), (lo + 1, fr)):
            if wgt > 0:
                acc = acc + wgt * read(c8[i:i + 1], (T8 + I[i] * np.sin(TAU * m * T8) / (TAU * m))[None, :])[0]
        out[i] = acc
    return fin(best_roll(down(out)))


def self_ring():
    """Modular Expression 'driving' synth (C3): the cycle multiplied by ITSELF played m times faster
    (m 1 -> 64, neighbouring integers crossfaded) — a ring modulator whose carrier is the sound's own shape.
    From a warm synth to a clanging ring whose sidebands climb away and leave a hole where the note was."""
    H, _ = psync("driving", 130.61, 0.8, 5.4, navg=2, smooth=1.0)
    c8 = up(pk(wave(H)))
    m = expo(1.0, 64.0, 1.0)
    a = np.clip(3 * s, 0, 1)
    out = np.empty_like(c8)
    for i in range(F):
        lo = int(np.floor(m[i])); fr = m[i] - lo
        r = (1 - fr) * read(c8[i:i + 1], lo * T8[None, :])[0] + fr * read(c8[i:i + 1], (lo + 1) * T8[None, :])[0]
        out[i] = (1 - a[i]) * c8[i] + a[i] * c8[i] * r
    return fin(down(out))


def pedal_drone_life():
    """Pedalphonics one-shot OS 4 #2 (C3, 115 harmonics), plain and honest: one exact period per frame
    across its whole swell — the pedal chain breathing open by itself. The recognisable one: Max's pedal
    note, playable from the first frame to the last."""
    H, _ = psync("os4_2", 131.12, 0.25, 5.3, navg=3, smooth=1.2)
    return fin(wave(H))


def chopper_pedal():
    """Modular Expression 'harsh man' synth (C3) through a chopper (an audio-rate tremolo gate): N gates per
    cycle (1 -> 24) with the duty closing from 95 % to 30 %. The harsh drone is sliced into a buzzing,
    stuttering comb of its own fragments."""
    H, _ = psync("harsh_man", 130.28, 0.06, 2.7, navg=2, smooth=1.0)
    x = up(pk(wave(H)))
    N = expo(1.0, 40.0, 0.6)
    duty = 0.95 - 0.78 * s ** 0.6
    out = np.empty_like(x)
    for i in range(F):
        acc = 0
        lo = int(np.floor(N[i])); fr = N[i] - lo
        for n, wgt in ((lo, 1 - fr), (lo + 1, fr)):
            ph = np.mod(n * T8, 1.0)
            edge = 0.04
            gte = np.clip((duty[i] - np.abs(ph - 0.5) * 2) / edge + 0.5, 0, 1)
            gte = 0.5 - 0.5 * np.cos(np.pi * gte)
            acc = acc + wgt * gte
        out[i] = x[i] * acc
    return fin(best_roll(limit(down(out), 3.0)))


# ── FREEZE: textures on the harmonic grid ─────────────────────────────────────────────────────────
def crazed_delay_freeze():
    """Modular Expression 'crazed delay' FX, frozen onto the 1024-harmonic grid across its life: a delay
    tail spinning out of control becomes a pitched comb whose colour swirls from mid grind to a bright
    spray. Maximum harmonics, Schroeder phase (dense but not spiky)."""
    M = spectro("crazed", 0.0, 3.65, nav=3)
    M = gaussian_filter1d(M, 1.0, axis=0)
    return fin(mags_frames(M, phase_for(M)), roll=SZ // 2)


def sinking_texture():
    """Creator Club pedal thought 'Maryground' (Blooper + Thermae) frozen while the harmonic grid is
    RESCALED: harmonic k reads the texture at k x r, r 1 -> 0.16. The texture's timeline plays AND its
    spectrum stretches upward — it rises out of the floor, spreading into a shimmering, sparse spray."""
    r = expo(1.0, 0.16, 1.0)
    M = spectro("maryground", 0.0, 8.4, nav=3, ratio=r)
    return fin(best_roll(limit(mags_frames(M, chirp_phase(rising=False)), 2.8)))


def noise_tuned_to_chord():
    """Creator Crate pedal memory 'Barter' (19 s) frozen, and its energy pulled into a MINOR-NINE chord:
    a resonator bank at the chord's partials sharpens from a flat wash (width 40 slots) to needle peaks
    (0.6). A murky pedal memory that crystallises into a ringing chord made of its own noise."""
    M = spectro("barter", 0.0, 18.6, nav=4)
    ratios = np.array([1.0, 1.2, 1.5, 1.8, 2.25])        # minor 9: 1, m3, 5, m7, 9 (just)
    base = 6.0
    sig = expo(40.0, 0.6, 0.9)
    peaks = np.unique(np.concatenate([base * q * np.arange(1, 1 + int(KH / (base * q))) for q in ratios]))
    out = np.empty_like(M)
    for i in range(F):
        C = np.zeros(NH)
        near = peaks[(peaks > 0)]
        for p0 in near:
            lo, hi = int(max(0, p0 - 5 * sig[i])), int(min(NH, p0 + 5 * sig[i] + 1))
            C[lo:hi] += np.exp(-0.5 * ((kh[lo:hi] - p0) / sig[i]) ** 2)
        C /= C.max()
        out[i] = M[i] * (0.02 + C)
    return fin(mags_frames(out, phase_for(out)))


def spectral_time_skew():
    """Modular Expression pad loop 'numerator' frozen with a SPECTRAL DELAY: the high harmonics read the
    pad later than the low ones, the skew growing 0 -> 9 s across the table. The chord smears into itself —
    its lows now, its highs from the future — a pad torn along the frequency axis."""
    Sg = spectro("numerator", 0.0, 15.0, n=256, nav=2)
    Sg = gaussian_filter1d(Sg, 1.0, axis=0)
    base = 0.55 * s
    skew = 0.6 * s ** 1.3
    lk = np.log2(kh) / np.log2(KH)
    M = np.empty((F, NH))
    for i in range(F):
        tpos = np.clip(base[i] + skew[i] * lk, 0, 1)
        ti = np.round(tpos * 255).astype(int)
        M[i] = Sg[ti, np.arange(NH)]
    return fin(mags_frames(M))


def phrase_freeze():
    """Pedalphonics chop PHRASE22 frozen plainly over its life — the pedal phrase bending its colour through
    52 semitones, from a subby pulse to a bright, pitch-shifted halo. The honest spectral import."""
    M = spectro("phrase22", 0.0, 7.7, nav=3)
    M = gaussian_filter1d(M, 1.2, axis=0)
    return fin(mags_frames(M, phase_for(M)))


def scrambled_microcosm():
    """Analog Tools 3 pedal construct 'I Bet You Can't Use This' (Microcosm) frozen, then SCRAMBLED: every
    harmonic is thrown a seeded distance of up to 0 -> 260 slots. Starts as the granular looper's chord, ends
    as a shuffled deck of its own partials — every note in the wrong place, the energy intact."""
    M = spectro("ibet", 0.0, 15.3, nav=3)
    M = gaussian_filter1d(M, 1.0, axis=0)
    rng = np.random.default_rng(4141)
    u = rng.uniform(-1, 1, NH)
    D = 260.0 * s ** 1.5
    out = np.zeros_like(M)
    for i in range(F):
        pos = np.clip(np.arange(NH) + D[i] * u, 0, KH - 2)
        lo = np.floor(pos).astype(int); fr = pos - lo
        P = M[i] ** 2
        acc = np.zeros(NH)
        np.add.at(acc, lo, P * (1 - fr))
        np.add.at(acc, lo + 1, P * fr)
        out[i] = np.sqrt(acc)
    return fin(best_roll(limit(mags_frames(out, chirp_phase(rising=True)), 2.8)))


def pixelated_telegram():
    """Analog Tools 4 'Telegram' texture (15 s, 499 harmonics) frozen and PIXELATED along the frequency axis:
    blocks of 1 -> 64 harmonics each take their loudest member's level. A lush texture that turns into a
    stepped, blocky spectrum — the sound of a picture losing resolution."""
    M = spectro("telegram", 0.0, 14.7, nav=3)
    M = gaussian_filter1d(M, 1.0, axis=0)
    B = np.round(expo(1.0, 64.0, 1.0)).astype(int)
    out = np.empty_like(M)
    for i in range(F):
        b = B[i]
        n = int(np.ceil(NH / b)) * b
        pad = np.zeros(n); pad[:NH] = M[i]
        blk = pad.reshape(-1, b).max(axis=1)
        out[i] = np.repeat(blk, b)[:NH]
    return fin(best_roll(mags_frames(out, phase_for(out))))


def whitened_flip():
    """Creator Club pedal thought 'Can You Flip?' (Blooper + Thermae, 18 s) frozen and WHITENED: its spectral
    envelope divided out with power 0 -> 1.5. The warm pedal texture loses its colour, then inverts it —
    lows scooped, the hidden fizz on top suddenly loudest. Same sound, colour turned inside out. (The
    whitened end crams the loud top octave into a short stretch of the chirp: limit, then limit_spikes.)"""
    M = spectro("flip", 0.0, 18.5, nav=3)
    M = gaussian_filter1d(M, 1.0, axis=0)
    E = envelope_of(M + 1e-7 * M.max(), 18)
    a = (1.5 * s ** 1.1)[:, None]
    out = M / np.maximum(E, 1e-12) ** a
    out[:, KH:] = 0
    return fin(best_roll(limit_spikes(limit(mags_frames(out, chirp_phase(0.15, 0.85, rising=False)), 2.8), 2.6, 3.0, 3)))


def messiah_formants():
    """Creator Crate vocal accent 'Messiah' frozen, its formant envelope (30 cepstral coefficients) slid
    DOWN 1x -> 0.2x under the unchanged fine structure while the phrase plays. A voice sinking into a demon's
    throat — the words keep moving, the mouth keeps growing."""
    M = spectro("messiah", 0.0, 4.8, nav=3)
    M = gaussian_filter1d(M, 1.0, axis=0)
    E = envelope_of(M + 1e-7 * M.max(), 30)
    fine = M / np.maximum(E, 1e-12)
    alpha = expo(1.0, 0.2, 1.0)
    En = np.array([np.interp(kh / a, kh, E[i], right=E[i, KH - 1]) for i, a in enumerate(alpha)])
    return fin(mags_frames(fine * En))


def kaleidoscope_feeling():
    """Creator Club Onward texture 'Mystic Feeling' frozen and folded like a KALEIDOSCOPE: the spectrum below
    a mirror line is reflected back and forth across the whole grid, the line falling from harmonic 1000 to
    9. The texture repeats itself in mirrored bands, finer and finer, until it is a shimmering lattice."""
    M = spectro("mystic", 0.0, 11.9, nav=3)
    M = gaussian_filter1d(M, 1.0, axis=0)
    p = expo(1000.0, 9.0, 1.0)
    out = np.empty_like(M)
    for i in range(F):
        per = 2 * p[i]
        q = np.mod(kh - 1, per)
        idx = np.where(q < p[i], q, per - q)
        out[i] = np.interp(idx, np.arange(NH), M[i])
    out[:, KH:] = 0
    return fin(best_roll(limit(mags_frames(out, chirp_phase(0.1, 0.9, rising=True)), 2.8)))


def phaser_chops():
    """Creator Club pedal thought 'Chops' (Blooper + Thermae) frozen through a PHASER: notches spaced evenly
    in octaves (log-frequency, like the pedal's all-pass stages), their count 0 -> 26 and depth 0 -> -60 dB,
    and the whole comb drifting. A pedal loop swallowed by a swirling, vowel-ish notch ladder."""
    M = spectro("chops", 0.0, 6.8, nav=3)
    M = gaussian_filter1d(M, 1.0, axis=0)
    dens = (26.0 * s ** 1.2)[:, None] / np.log2(KH)
    depth = (s ** 0.8)[:, None]
    drift = (3.0 * s)[:, None]
    lk = np.log2(kh)[None, :]
    notch = 0.5 + 0.5 * np.cos(TAU * (dens * lk + drift))
    G = 1.0 - depth * 0.999 * notch ** 2
    return fin(mags_frames(M * G))


def compressed_recording():
    """Analog Tools 3 pedal construct 'Recorded' (Thermae + Microcosm) frozen through a SPECTRAL COMPRESSOR:
    every harmonic's level raised to the power 1 -> 0.4. The quiet hiss between the notes comes up to meet
    them, and the pitch-shifted phrase ends as a dense, roaring wall that still carries its chord. Max harmonics."""
    M = spectro("recorded", 0.0, 6.8, nav=3)
    M = gaussian_filter1d(M, 1.0, axis=0)
    g = expo(1.0, 0.4, 1.0)[:, None]
    out = norm_rows(M) ** g
    out[:, KH:] = 0
    return fin(mags_frames(out, phase_for(out)))


def death_rattle_freeze():
    """Modular Expression 'death sound' vox (half a second) frozen at fine time resolution (4096 window):
    the whole groan stretched across 128 frames, from the chesty onset through a gargling middle to the
    rattle of the last breath."""
    M = spectro("death", 0.0, 0.5, W=4096, nav=2, hop_div=8)
    M = gaussian_filter1d(M, 1.0, axis=0)
    return fin(mags_frames(M))


def wired_sieve():
    """Polybrute Expressions noise 'Wired' frozen and pushed through a HARMONIC SIEVE: only harmonics that are
    multiples of m survive, m sliding 1 -> 9 (soft windows between). The wired noise hums a pitch that jumps
    up through the series — noise forced to sing ever higher octaves, fifths and thirds."""
    M = spectro("wired", 0.0, 3.35, nav=3)
    M = gaussian_filter1d(M, 1.0, axis=0)
    m = expo(1.0, 9.0, 1.0)
    out = np.empty_like(M)
    for i in range(F):
        ph = np.cos(np.pi * kh / m[i]) ** 2
        sharp = 1.0 + 40.0 * s[i]
        out[i] = M[i] * (0.006 + ph ** sharp)
    return fin(mags_frames(out))


# ── GRAIN: raw chops, the grit stays ──────────────────────────────────────────────────────────────
def king_grain():
    """Creator Crate FX 'King' chopped RAW: 128 loop-closed 2048-sample grains marched through the hit from
    its glassy strike to its tremolo tail. Nothing smoothed — the saturation and aliasing of the chain stay."""
    x = load("king")
    p = loud_positions(x, 0.0, 2.35, within_db=30)
    return fin(np.array([chop(x, q) for q in p]))


def dub_stutter():
    """Creator Crate FX 'Dub Central' as a BUFFER STUTTER: each frame repeats one grain k times (k 1 -> 32,
    the grain 1/k of a cycle long), the grain position walking through the hit. Raw dub grit that stutters
    up into a pitched buzz as the loop shortens."""
    x = load("dub")
    p = loud_positions(x, 0.0, 1.7, within_db=24, walk=True)    # every frame its own grain
    K = np.round(expo(1.0, 32.0, 1.0)).astype(int)
    out = np.empty((F, SZ))
    for i in range(F):
        L = SZ // K[i]
        g = chop(x, p[i], L=L, X=max(16, L // 8))
        out[i] = g
    return fin(out)


def nile_collage():
    """Creator Club pedal thought 'Nile' (Blooper) as a TAPE COLLAGE: each frame is spliced from M pieces
    (M 1 -> 40) taken from seeded places that spread across the whole loop, crossfaded at the splices.
    One raw grain becomes a flickering mosaic of the loop's whole life inside a single cycle."""
    x = load("nile")
    rng = np.random.default_rng(8080)
    u = rng.uniform(0, 1, 64)
    Mn = np.round(expo(3.0, 40.0, 1.0)).astype(int)
    base = loud_positions(x, 0.3, 5.1, within_db=20)
    spread = 4.5 * SR * s ** 1.2
    out = np.empty((F, SZ))
    for i in range(F):
        m = Mn[i]
        edges = np.round(np.linspace(0, SZ, m + 1)).astype(int)
        y = np.zeros(SZ)
        X = 24
        for j in range(m):
            a, b = edges[j], edges[j + 1]
            L = b - a
            q = int(np.clip(base[i] + spread[i] * (u[j] - 0.5), 0, len(x) - SZ - 64))
            seg = x[q + a:q + b + X].copy()
            w = np.ones(L + X)
            w[:X] = np.sin(0.5 * np.pi * (np.arange(X) + 0.5) / X) ** 2
            w[L:] = 1 - w[:X]
            if m == 1:
                y += chop(x, q) * 0 + 0
                y = chop(x, q)
                break
            idx = (np.arange(a, b + X)) % SZ
            np.add.at(y, idx, seg * w)
        out[i] = y
    X = np.fft.rfft(out, axis=1); X[:, 0] = 0; X[:, KH + 1:] = 0
    return fin(limit(np.fft.irfft(X, n=SZ, axis=1), 2.8))


def jupuip_grain():
    """Creator Crate FX 'Jupuip' chopped RAW: a fizzing swell that sweeps 78 semitones, captured grain by
    grain — sub rumble at the start, blinding saturated fizz at the end."""
    x = load("jupuip")
    p = loud_positions(x, 0.0, 3.95, within_db=30)
    return fin(np.array([chop(x, q) for q in p]))


def grain_to_pulse():
    """Modular Expression sequencer one-shot 'glo trials' as grains whose window closes: a raw chop under a
    Gaussian window narrowing from the whole cycle to 7 % of it. The sequence's grain collapses into a
    single bright click-burst per cycle — a buzzing pulse carved out of Max's synth."""
    x = load("glo")
    p = loud_positions(x, 0.0, 6.55, within_db=24)
    sig = expo(0.9, 0.09, 1.0)
    out = np.empty((F, SZ))
    for i in range(F):
        g = chop(x, p[i])
        w = np.exp(-0.5 * ((T1 - 0.5) / sig[i]) ** 2)
        out[i] = g * w
    X = np.fft.rfft(out, axis=1); X[:, 0] = 0; X[:, KH + 1:] = 0
    return fin(limit(np.fft.irfft(X, n=SZ, axis=1), 2.6))


def palmer_crush():
    """Analog Tools 4 'King Palmer' texture (25 s of glassy fizz) chopped raw through a bit crusher whose
    depth falls 12 -> 1.5 bits while the grains walk the file: tape-and-pedal fizz ground into digital gravel."""
    x = load("palmer")
    p = loud_positions(x, 0.3, 24.8, within_db=20)
    q = expo(2048.0, 1.4, 1.0)
    out = np.empty((F, SZ))
    for i in range(F):
        g = chop(x, p[i])
        g /= max(np.abs(g).max(), 1e-12)
        out[i] = np.round(g * q[i]) / q[i]
    X = np.fft.rfft(out, axis=1); X[:, 0] = 0; X[:, KH + 1:] = 0
    return fin(np.fft.irfft(X, n=SZ, axis=1))


def chewed_tape():
    """Analog Tools 3 pedal construct 'Game Over' (Microcosm) as grains played from CHEWED TAPE: each
    loop-closed chop is read back at a speed that lurches within the cycle (a periodic warp, depth
    0 -> 0.2 cycles, getting rougher). The micro-looper's chord warbles, then mangles and tears."""
    x = load("game_over")
    p = loud_positions(x, 0.0, 1.45, within_db=24)
    g = np.array([chop(x, q) for q in p])
    c8 = up(g)
    rng = np.random.default_rng(3131)
    K = np.arange(1, 25)
    amp = rng.normal(0, 1, 24) / K
    phs = rng.uniform(0, TAU, 24)
    depth = 0.2 * s ** 1.3
    M = np.round(expo(2.0, 24.0, 1.0)).astype(int)
    y = np.empty_like(c8)
    for i in range(F):
        nz = (amp[:M[i], None] * np.sin(TAU * K[:M[i], None] * T8[None, :] + phs[:M[i], None])).sum(0)
        nz /= np.abs(nz).max()
        y[i] = read(c8[i:i + 1], T8[None, :] + depth[i] * nz[None, :])[0]
    return fin(down(y))


# ═════════════════════════════════════════════════════════════════════════════════════════════════
TABLES = [
    ("SELF DEVOURING SUB", self_devouring_sub),
    ("UPSIDE DOWN BASS", upside_down_bass),
    ("LIMPING OCTAVE", limping_octave),
    ("OCTAVE FUZZ UNDERTOW", octave_fuzz_undertow),
    ("CHEBYSHEV RUMBLE", chebyshev_rumble),
    ("CRUSHED STALKER", crushed_stalker),
    ("KNEE BENT BASS", knee_bent_bass),
    ("CRINKLED TAPE SUB", crinkled_tape_sub),
    ("FEEDBACK FLANGE DRONE", feedback_flange_drone),
    ("HOLLOWS FORMANT WALK", hollows_formant_walk),
    ("FACETED BASS", faceted_last_stand),
    ("SHIFTED UNDERTOW", shifted_undertow),
    ("SELF COMPOSED", self_composed),
    ("CHIRPED PEDAL", chirped_pedal),
    ("PEDAL AIR EXCITER", pedal_air_exciter),
    ("STACKED PERIODS", stacked_periods),
    ("OCTAVE DIVIDER GLASS", octave_divider_glass),
    ("WAH STOMP PHRASE", wah_stomp_phrase),
    ("SHIMMER CASCADE", shimmer_cascade),
    ("AUDIO RATE VIBRATO", audio_rate_vibrato),
    ("SELF RING", self_ring),
    ("PEDAL DRONE LIFE", pedal_drone_life),
    ("CHOPPER PEDAL", chopper_pedal),
    ("CRAZED DELAY FREEZE", crazed_delay_freeze),
    ("SINKING TEXTURE", sinking_texture),
    ("NOISE TUNED TO CHORD", noise_tuned_to_chord),
    ("SPECTRAL TIME SKEW", spectral_time_skew),
    ("PHRASE FREEZE", phrase_freeze),
    ("SCRAMBLED MICROCOSM", scrambled_microcosm),
    ("PIXELATED TELEGRAM", pixelated_telegram),
    ("NEGATIVE IMAGE", whitened_flip),
    ("SINKING VOICE", messiah_formants),
    ("HALL OF MIRRORS", kaleidoscope_feeling),
    ("PHASER CHOPS", phaser_chops),
    ("COMPRESSED RECORDING", compressed_recording),
    ("DEATH RATTLE FREEZE", death_rattle_freeze),
    ("WIRED SIEVE", wired_sieve),
    ("KING GRAIN", king_grain),
    ("DUB STUTTER", dub_stutter),
    ("NILE COLLAGE", nile_collage),
    ("FIZZ SWELL", jupuip_grain),
    ("GRAIN TO PULSE", grain_to_pulse),
    ("PALMER CRUSH", palmer_crush),
    ("CHEWED TAPE", chewed_tape),
]
CATEGORY = {ident: "Textures" for ident, _ in TABLES}

# ── provenance: which of Max's files each table re-reads, and how ─────────────────────────────────
_SOURCE_OF = {
    "SELF DEVOURING SUB": ("hardware", "pitch-sync + self phase-modulation"),
    "UPSIDE DOWN BASS": ("wild_bass", "pitch-sync + harmonic-level reversal"),
    "LIMPING OCTAVE": ("forever", "pitch-sync, two periods per frame + half-rate limp"),
    "OCTAVE FUZZ UNDERTOW": ("young_goat", "pitch-sync + three-stage rectifier fuzz"),
    "CHEBYSHEV RUMBLE": ("say_less", "pitch-sync + Chebyshev shaper"),
    "CRUSHED STALKER": ("stalker", "pitch-sync + bit/sample-rate crush, jittered clock"),
    "KNEE BENT BASS": ("sauron", "pitch-sync + phase-distortion knee"),
    "CRINKLED TAPE SUB": ("bass5", "pitch-sync + periodic random read warp"),
    "FEEDBACK FLANGE DRONE": ("misc36", "pitch-sync + feedback comb"),
    "HOLLOWS FORMANT WALK": ("hollows", "pitch-sync + cepstral envelope shift"),
    "FACETED BASS": ("last_stand", "pitch-sync + cycle cloning (facets)"),
    "SHIFTED UNDERTOW": ("bass3", "pitch-sync + frequency shifter with feedback"),
    "SELF COMPOSED": ("manipulate", "pitch-sync + iterated self-composition"),
    "CHIRPED PEDAL": ("os2", "pitch-sync + power-law read (in-cycle chirp)"),
    "PEDAL AIR EXCITER": ("os1_2", "pitch-sync + band-replication exciter"),
    "STACKED PERIODS": ("os5_2b", "pitch-sync, 1 -> 16 consecutive periods per frame"),
    "OCTAVE DIVIDER GLASS": ("pacmane", "pitch-sync, eight periods per frame + flip-flop divider"),
    "WAH STOMP PHRASE": ("phrase20", "pitch-sync + fuzz -> wah"),
    "SHIMMER CASCADE": ("ruler", "pitch-sync + x3/x5 shimmer feedback"),
    "AUDIO RATE VIBRATO": ("vetements", "pitch-sync + in-cycle vibrato"),
    "SELF RING": ("driving", "pitch-sync + ring by its own faster copy"),
    "PEDAL DRONE LIFE": ("os4_2", "pitch-sync (plain life)"),
    "CHOPPER PEDAL": ("harsh_man", "pitch-sync + audio-rate chopper"),
    "CRAZED DELAY FREEZE": ("crazed", "spectral-freeze (life)"),
    "SINKING TEXTURE": ("maryground", "spectral-freeze + grid rescale"),
    "NOISE TUNED TO CHORD": ("barter", "spectral-freeze + chord resonator bank"),
    "SPECTRAL TIME SKEW": ("numerator", "spectral-freeze + spectral delay"),
    "PHRASE FREEZE": ("phrase22", "spectral-freeze (life)"),
    "SCRAMBLED MICROCOSM": ("ibet", "spectral-freeze + seeded bin scramble"),
    "PIXELATED TELEGRAM": ("telegram", "spectral-freeze + frequency pixelation"),
    "NEGATIVE IMAGE": ("flip", "spectral-freeze + envelope whitening"),
    "SINKING VOICE": ("messiah", "spectral-freeze + cepstral envelope shift"),
    "HALL OF MIRRORS": ("mystic", "spectral-freeze + spectral mirror folding"),
    "PHASER CHOPS": ("chops", "spectral-freeze + log-spaced notches"),
    "COMPRESSED RECORDING": ("recorded", "spectral-freeze + spectral compressor"),
    "DEATH RATTLE FREEZE": ("death", "spectral-freeze (life, 4096 window)"),
    "WIRED SIEVE": ("wired", "spectral-freeze + harmonic sieve"),
    "KING GRAIN": ("king", "grain (raw chops)"),
    "DUB STUTTER": ("dub", "grain buffer stutter"),
    "NILE COLLAGE": ("nile", "grain tape collage"),
    "FIZZ SWELL": ("jupuip", "grain (raw chops)"),
    "GRAIN TO PULSE": ("glo", "grain + closing window"),
    "PALMER CRUSH": ("palmer", "grain + bit crush"),
    "CHEWED TAPE": ("game_over", "grain + periodic read warp"),
}
PROVENANCE = {ident: dict(pack=SRC[k][0], file=os.path.basename(SRC[k][1]), path=WC + SRC[k][1], method=m)
              for ident, (k, m) in _SOURCE_OF.items()}
assert set(PROVENANCE) == set(CATEGORY), "every table needs provenance"
