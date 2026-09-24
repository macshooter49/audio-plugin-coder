"""common.py — shared physics + I/O for Terrain's offline electric-piano models (Tools/organics/epmodel).

Everything here is generated from first principles: modal resonators (damped sinusoids, closed form), a contact-force
pulse for the hammer/tangent/pad, pickup transfer curves, and small filters. No third-party audio is read, filtered,
convolved or copied anywhere in the renderer. Owned outright by Waves Crate.

Conventions: 48 kHz, mono (a DI signal), float64 internally, 24-bit WAV out.
"""
from __future__ import annotations

import math
import os

import numpy as np
import soundfile as sf
from scipy import signal

SR = 48000
RNG_SEED = 20260924


def midi_f(m: float) -> float:
    return 440.0 * 2.0 ** ((m - 69.0) / 12.0)


def interp_key(key: float, table) -> float:
    """Piecewise-linear interpolation of log(value) over MIDI key: table = [(key, value), …] (value > 0)."""
    ks = np.array([k for k, _ in table], float)
    vs = np.log(np.array([v for _, v in table], float))
    return float(np.exp(np.interp(key, ks, vs)))


def interp_lin(key: float, table) -> float:
    ks = np.array([k for k, _ in table], float)
    vs = np.array([v for _, v in table], float)
    return float(np.interp(key, ks, vs))


def vel01(vel: int) -> float:
    return max(1, min(127, vel)) / 127.0


# ------------------------------------------------------------------------------------------ excitation
def contact_pulse(tc: float, sr: int = SR, shape: float = 2.0) -> np.ndarray:
    """Contact force of a felt/neoprene/rubber tip: a gamma pulse t^a·e^(−t/b), unit area.
    Its spectrum 1/(1+(ωb)²)^((a+1)/2) has no nulls (a real felt tip is not a rectangle), and a shorter contact
    (harder hit / harder tip) pushes the corner up — the velocity → brightness law."""
    b = max(tc / (shape + 2.0), 0.5 / sr)
    n = max(4, int(math.ceil((shape + 12.0) * b * sr)))
    t = np.arange(n) / sr
    p = t ** shape * np.exp(-t / b)
    return p / p.sum()


def modal(n: int, freqs, amps, t60s, phases=None, glide=None, sr: int = SR, want_v: bool = True):
    """Sum of damped sinusoids x(t) = Σ a·e^(−t/τ)·sin(φ(t)), with its exact time derivative v(t) (1/s units).
    glide = (depth, tau): a per-note pitch glide f(t) = f·(1 + depth·e^(−t/tau)) (tension modulation)."""
    t = np.arange(n) / sr
    x = np.zeros(n)
    v = np.zeros(n) if want_v else None
    if phases is None:
        phases = np.zeros(len(freqs))
    if glide is not None:
        gd, gt = glide
        warp = t + gd * gt * (1.0 - np.exp(-t / gt))
        fmul = 1.0 + gd * np.exp(-t / gt)
    else:
        warp, fmul = t, 1.0
    for f, a, T, ph in zip(freqs, amps, t60s, phases):
        if a == 0.0 or f >= 0.47 * sr:
            continue
        tau = T / 6.907755
        e = a * np.exp(-t / tau)
        w = 2 * math.pi * f
        arg = w * warp + ph
        s = np.sin(arg)
        x += e * s
        if want_v:
            v += e * (w * fmul * np.cos(arg) - s / tau)
    return x, v


# ------------------------------------------------------------------------------------------ filters
def biquad(y: np.ndarray, kind: str, fc: float, q: float = 0.7071, gain_db: float = 0.0, sr: int = SR) -> np.ndarray:
    """RBJ cookbook biquad: lp, hp, bp, peak, lowshelf, highshelf."""
    fc = min(fc, 0.45 * sr)
    w0 = 2 * math.pi * fc / sr
    c, s = math.cos(w0), math.sin(w0)
    al = s / (2 * q)
    A = 10 ** (gain_db / 40)
    if kind == "lp":
        b = [(1 - c) / 2, 1 - c, (1 - c) / 2]; a = [1 + al, -2 * c, 1 - al]
    elif kind == "hp":
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]; a = [1 + al, -2 * c, 1 - al]
    elif kind == "bp":
        b = [al, 0, -al]; a = [1 + al, -2 * c, 1 - al]
    elif kind == "peak":
        b = [1 + al * A, -2 * c, 1 - al * A]; a = [1 + al / A, -2 * c, 1 - al / A]
    elif kind == "lowshelf":
        sq = 2 * math.sqrt(A) * al
        b = [A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq)]
        a = [(A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq]
    elif kind == "highshelf":
        sq = 2 * math.sqrt(A) * al
        b = [A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq)]
        a = [(A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq]
    else:
        raise ValueError(kind)
    return signal.lfilter(np.array(b) / a[0], np.array(a) / a[0], y)


def softplus(z: np.ndarray, s: float) -> np.ndarray:
    return s * np.logaddexp(0.0, z / s)


def fade_out(y: np.ndarray, sec: float, sr: int = SR) -> np.ndarray:
    n = min(len(y), int(sec * sr))
    if n > 1:
        y[-n:] *= np.cos(np.linspace(0, math.pi / 2, n)) ** 2
    return y


def trim_tail(y: np.ndarray, floor_db: float = -66.0, sr: int = SR, min_s: float = 0.25) -> np.ndarray:
    """Cut where the 20 ms envelope stays below floor_db re peak, with a 60 ms fade."""
    h = int(0.02 * sr)
    n = len(y) // h
    if n < 2:
        return y
    e = 20 * np.log10(np.sqrt((y[: n * h].reshape(n, h) ** 2).mean(axis=1)) + 1e-20)
    e -= e.max()
    above = np.nonzero(e > floor_db)[0]
    end = min(len(y), (int(above[-1]) + 2) * h if len(above) else len(y))
    end = max(end, int(min_s * sr))
    return fade_out(y[:end].copy(), 0.06, sr)


def pre_roll(y: np.ndarray, ms: float = 2.0, sr: int = SR) -> np.ndarray:
    return np.concatenate([np.zeros(int(ms * 1e-3 * sr)), y])


# ------------------------------------------------------------------------------------------ mechanical noise
def knock(rng: np.random.Generator, dur: float, modes, tc: float, burst_db: float = -12.0,
          burst_band=(1500.0, 7000.0), burst_ms: float = 4.0, sr: int = SR) -> np.ndarray:
    """A mechanical knock: a contact pulse driving a few damped structural modes (wood key, rail, frame; each mode
    randomly detuned ±4 % so no two hits are identical) plus the contact's own short broadband scrape."""
    n = int(dur * sr)
    fr = [f * (1 + rng.uniform(-0.04, 0.04)) for f, _, _ in modes]
    am = [a * (1 + rng.uniform(-0.2, 0.2)) for _, a, _ in modes]
    t6 = [t * (1 + rng.uniform(-0.15, 0.15)) for _, _, t in modes]
    ph = rng.uniform(0, 2 * math.pi, len(modes))
    _, v = modal(n, fr, am, t6, ph)
    v = np.convolve(v, contact_pulse(tc), mode="full")[:n]
    v /= np.abs(v).max() + 1e-12
    b = rng.standard_normal(n)
    b = biquad(biquad(b, "hp", burst_band[0], 0.7), "lp", burst_band[1], 0.7)
    t = np.arange(n) / sr
    b *= np.exp(-t / (burst_ms * 1e-3)) * (1 - np.exp(-t / 0.0003))
    b *= (10 ** (burst_db / 20)) / (np.abs(b).max() + 1e-12)
    y = v + b
    y = biquad(y, "hp", 30.0, 0.7)
    return trim_tail(y / (np.abs(y).max() + 1e-12), -60.0, sr, min_s=0.05)


# ------------------------------------------------------------------------------------------ I/O
def write_wav(path: str, y: np.ndarray, peak_db: float = -1.0, sr: int = SR) -> float:
    """Peak-normalise to peak_db and write 24-bit; returns the gain applied in dB (so the SFZ volume can undo it)."""
    pk = float(np.abs(y).max())
    g = 10 ** (peak_db / 20) / pk if pk > 0 else 1.0
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sf.write(path, (y * g).astype(np.float64), sr, subtype="PCM_24", format="WAV")
    return 20 * math.log10(g)


def rms_db(y: np.ndarray, t0: float = 0.0, t1: float = 0.3, sr: int = SR) -> float:
    a = np.abs(y)
    on = int(np.argmax(a > a.max() * 0.01))
    seg = y[on + int(t0 * sr): on + int(t1 * sr)]
    return 20 * math.log10(math.sqrt(float(np.mean(seg ** 2))) + 1e-20)
