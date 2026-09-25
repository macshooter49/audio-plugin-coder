"""measure.py — listen-by-numbers metrics shared by validate.py.

Used on the model renders AND on the DO-NOT-SHIP reference recordings, which are only ever MEASURED here (a few
scalar numbers per file) — never copied, filtered, convolved or shipped.
"""
import numpy as np


def mono(x):
    return x.mean(axis=1) if x.ndim == 2 else x


def f_of(midi):
    return 440.0 * 2 ** ((midi - 69) / 12.0)


def onset(x, thr_db=-40):
    a = np.abs(x)
    pk = a.max() + 1e-12
    i = np.nonzero(a > pk * 10 ** (thr_db / 20))[0]
    return int(i[0]) if len(i) else 0


def spectrum(x, sr, t0, t1):
    o = onset(x)
    seg = x[o + int(t0 * sr): o + int(t1 * sr)]
    if len(seg) < 256:
        seg = x[o: o + int(0.2 * sr)]
    N = 1 << int(np.ceil(np.log2(len(seg) * 4)))
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), N))
    return S, np.fft.rfftfreq(N, 1 / sr)


def peak_db(S, fr, f, tol=0.03):
    m = (fr > f * (1 - tol)) & (fr < f * (1 + tol))
    return 20 * np.log10(S[m].max() + 1e-12) if m.any() else -240.0


def harmonics(x, sr, f0, t0=0.15, t1=0.45, n=6):
    """Levels of harmonics 1..n (dB re H1) in a Hann window [t0, t1] s after onset."""
    S, fr = spectrum(x, sr, t0, t1)
    out = np.array([peak_db(S, fr, k * f0) if k * f0 < sr / 2 - 100 else -240.0 for k in range(1, n + 1)])
    return out - out[0]


def centroid(x, sr, t0, t1):
    o = onset(x)
    seg = x[o + int(t0 * sr): o + int(t1 * sr)]
    if len(seg) < 64:
        return 0.0
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    fr = np.fft.rfftfreq(len(seg), 1 / sr)
    return float((S * fr).sum() / (S.sum() + 1e-12))


def env_db(x, sr, win=0.02):
    h = int(win * sr)
    n = len(x) // h
    e = np.sqrt((x[: n * h].reshape(n, h) ** 2).mean(axis=1) + 1e-20)
    return 20 * np.log10(e), h


def t60(x, sr):
    """Decay time to −60 dB from a line fit of the envelope between −6 and −36 dB below the peak."""
    e, h = env_db(x, sr)
    p = int(np.argmax(e))
    pk = e[p]
    t = np.arange(len(e)) * h / sr
    idx = np.arange(len(e))
    m = (idx > p) & (e < pk - 6) & (e > pk - 36)
    if m.sum() < 4:
        m = (idx > p) & (e > pk - 50)
    if m.sum() < 4:
        return float("nan")
    s = np.polyfit(t[m], e[m], 1)[0]
    return float(-60.0 / s) if s < 0 else float("inf")


def summary(x, sr, midi):
    x = mono(np.asarray(x, dtype=np.float64))
    f0 = f_of(midi)
    h = harmonics(x, sr, f0)
    return {"H2": round(float(h[1]), 1), "H3": round(float(h[2]), 1), "H4": round(float(h[3]), 1),
            "cA": round(centroid(x, sr, 0.0, 0.03) / f0, 2), "cS": round(centroid(x, sr, 0.3, 0.8) / f0, 2),
            "T60": round(t60(x, sr), 2)}
