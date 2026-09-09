"""
wtlib — the shared wavetable harness for the Terrain factory bank.

EVERY generator imports this so that every table is built, sanitised and MEASURED
identically. If two generators used different normalisation the uniqueness gate would
be comparing apples to oranges and would pass tables that sound the same.

Contract for a generator module:

    import wtlib
    def my_table():
        ...
        return wtlib.finalize(frames)      # frames: (FRAMES, SIZE) float
    TABLES = [("TERRA SOMETHING", my_table), ...]

`finalize` is mandatory: it kills NaN/inf, removes DC, peak-normalises every frame, and
guarantees the exact dtype/shape the WAV writer and the gate expect.
"""
import math
import numpy as np

FRAMES = 128          # every table is 128 frames — Serum's ceiling is 300, ours will be 256
SIZE   = 2048         # samples per frame — the layout Serum/Vital/Terrain all read
NH     = SIZE // 2    # 1024 usable harmonics
SR     = 44100

t    = np.arange(SIZE) / SIZE          # one cycle, [0,1)
n_ax = np.arange(1, NH + 1)            # harmonic numbers 1..1024

# ONE fixed phase set, shared by every table and every frame. Phase must not move across
# frames or the morph turns to mush instead of gliding; it must not differ per table or
# two spectrally identical tables would look "different" to the gate for the wrong reason.
PHASE = np.random.default_rng(0xC0FFEE).uniform(0, 2 * np.pi, NH)


# ── construction ──────────────────────────────────────────────────────────────────────
def cycles_from_mags(mags, phase=None):
    """(F, NH) magnitudes -> (F, SIZE) cycles, via inverse FFT with the fixed phase set."""
    mags = np.nan_to_num(np.asarray(mags, dtype=float), nan=0.0, posinf=0.0, neginf=0.0)
    ph = PHASE if phase is None else phase
    out = np.zeros((mags.shape[0], SIZE))
    for i, m in enumerate(mags):
        spec = np.zeros(SIZE // 2 + 1, dtype=complex)
        spec[1:NH + 1] = m * np.exp(1j * ph)
        out[i] = np.fft.irfft(spec, n=SIZE)
    return out


def bandlimit(frames, nmax=1000):
    """Zero everything above harmonic nmax. Use when a time-domain construction would alias."""
    out = np.zeros_like(frames)
    for i, c in enumerate(frames):
        S = np.fft.rfft(c)
        S[nmax + 1:] = 0
        out[i] = np.fft.irfft(S, n=SIZE)
    return out


def finalize(frames):
    """MANDATORY last step. Sanitise, de-DC, peak-normalise per frame, fix dtype/shape."""
    a = np.asarray(frames, dtype=float)
    if a.shape != (FRAMES, SIZE):
        raise ValueError(f"expected {(FRAMES, SIZE)}, got {a.shape}")
    a = np.nan_to_num(a, nan=0.0, posinf=0.0, neginf=0.0)
    a = a - a.mean(axis=1, keepdims=True)
    pk = np.abs(a).max(axis=1, keepdims=True)
    a = np.divide(a, pk, out=np.zeros_like(a), where=pk > 1e-12)
    return a.astype(np.float32)


# ── measurement ───────────────────────────────────────────────────────────────────────
def _frame_db(fr):
    S = np.abs(np.fft.rfft(fr))[1:NH]
    pk = S.max()
    if pk <= 0:
        return None, None
    return 20 * np.log10(np.maximum(S, 1e-12) / pk), S


def measure(frames):
    """The board. Same numbers we measured Serum 2 and PLUTO 2 with."""
    h60, h80, cents, dead = [], [], [], 0
    for fr in frames:
        db, S = _frame_db(fr)
        if db is None:
            dead += 1
            continue
        h60.append(int((db > -60).sum()))
        h80.append(int((db > -80).sum()))
        w = S ** 2
        if w.sum() > 0:
            cents.append(float((np.arange(1, NH) * w).sum() / w.sum()))
    if not h60:
        return dict(harm60=0, harm80=0, span=0.0, dead=dead, crest=0.0, zc=0.0)
    span = 12 * math.log2(max(cents) / max(min(cents), 1e-9)) if len(cents) > 1 else 0.0
    rms = np.sqrt((frames.astype(float) ** 2).mean(axis=1))
    crest = float(np.mean(np.abs(frames).max(axis=1) / np.maximum(rms, 1e-9)))
    zc = float(np.mean([(np.diff(np.signbit(f)) != 0).sum() for f in frames]))
    return dict(harm60=int(np.mean(h60)), harm80=int(np.mean(h80)),
                span=float(span), dead=dead, crest=crest, zc=zc)


# ── uniqueness fingerprint ────────────────────────────────────────────────────────────
# 16 frame positions x 48 log-spaced harmonic bands of per-frame-normalised dB. Frame ORDER
# is significant, because a swept table is heard in order — a reversed table is a different
# instrument. Distance is mean |dB|, so the threshold reads in decibels.
_EDGES = np.unique(np.round(np.logspace(0, math.log10(NH - 1), 49)).astype(int))
_FSEL  = np.linspace(0, FRAMES - 1, 16).astype(int)


def fingerprint(frames):
    rows = []
    for i in _FSEL:
        db, _ = _frame_db(frames[i])
        if db is None:
            rows.append(np.full(len(_EDGES) - 1, -90.0)); continue
        db = np.maximum(db, -90.0)
        rows.append(np.array([db[_EDGES[k] - 1:_EDGES[k + 1] - 1].mean()
                              if _EDGES[k + 1] > _EDGES[k] else -90.0
                              for k in range(len(_EDGES) - 1)]))
    return np.concatenate(rows)


def distance(fa, fb):
    return float(np.mean(np.abs(fa - fb)))


# ── output ────────────────────────────────────────────────────────────────────────────
def write_wav(path, frames):
    import struct
    data = np.asarray(frames, dtype='<f4').reshape(-1).tobytes()
    fmt = struct.pack('<HHIIHH', 3, 1, SR, SR * 4, 4, 32)
    ch = b'fmt ' + struct.pack('<I', len(fmt)) + fmt
    ch += b'fact' + struct.pack('<II', 4, len(data) // 4)
    ch += b'data' + struct.pack('<I', len(data)) + data
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + len(ch)) + b'WAVE' + ch)


# ── quality bars a generator should self-check before returning ───────────────────────
#   harm60 >= 60      not thin. Serum 2's factory mean is 278; aim 150-450 for rich tables,
#                     and only the FOUNDATION category may sit low (a sine is 1 by definition).
#   span   >= 8 st    the frame axis must actually travel. Serum's median is 19.9; aim 20-60.
#   dead   == 0       no silent frames.
def selfcheck(name, frames, min_h60=60, min_span=8.0):
    m = measure(frames)
    ok = m['dead'] == 0 and m['harm60'] >= min_h60 and m['span'] >= min_span
    flag = "ok " if ok else "LOW"
    return ok, f"{flag} {name:<30} harm60 {m['harm60']:>4}  span {m['span']:>6.1f} st  dead {m['dead']}"


# ══ fb612 — THE SHIPPING NAME ═════════════════════════════════════════════════════════════════
#  Max: "everything respective capitals, not ALL CAPS. preset names 'Terra - (Name)' just so we
#  have organization, the dash will give us that."
#  The generators keep their ALL-CAPS identifiers ("TERRA BIT LADDER") because those are what the
#  TABLES lists and every log line use. This is the one place that turns an identifier into the
#  name a user reads, so gate.py (which renders the bank) and mkbank.py (which converts it for
#  shipping) cannot drift apart.
SHIP_KEEP_UPPER = {"CZ", "FB", "PD", "PWM", "VCO", "XOR", "FM", "Y"}   # initialisms, not words
SHIP_SPLIT = {          # all-caps single tokens that are really two words; .capitalize() alone
    "BITFLIP":  "Bit Flip",     # gives "Pidigits", "Primegap", "Xorfold"
    "GRAYCODE": "Gray Code",
    "PIDIGITS": "Pi Digits",
    "PRIMEGAP": "Prime Gap",
    "XORFOLD":  "XOR Fold",
    "RULE30":   "Rule 30",
    "RINGMOD":  "Ring Mod",
}

def shipping_name(stem):
    """'TERRA BIT LADDER' -> 'Terra - Bit Ladder'.  Pure; duplicates are the caller's problem."""
    if not stem.startswith("TERRA "):
        raise ValueError("not a Terra table identifier: %r" % (stem,))
    out = []
    for w in stem[len("TERRA "):].split():
        if   w in SHIP_SPLIT:      out.append(SHIP_SPLIT[w])
        elif w in SHIP_KEEP_UPPER: out.append(w)
        elif w.isdigit():          out.append(w)
        else:                      out.append(w.capitalize())
    return "Terra - " + " ".join(out)
