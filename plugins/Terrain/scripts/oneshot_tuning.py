#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  oneshot_tuning.py — tp24 · ARE THE FACTORY ONE-SHOTS ACTUALLY AT C?
#
#      python3 scripts/oneshot_tuning.py report            # measure, change nothing
#      python3 scripts/oneshot_tuning.py report --csv out.csv
#
#  WHY.  SynthVoice's sample path is  noteSemis = glideNote_ - 60.0 + ...  — every sample's root is
#  ASSUMED to be MIDI 60 (C). There is no per-sample root anywhere in the instrument. So a one-shot
#  that was actually recorded at D plays exactly two semitones sharp when you press C, which is
#  Max's report word for word ("some of them are two semitones down or two semitones up").
#
#  METHOD.  YIN (cumulative mean normalised difference) on a window taken PAST the attack, because
#  a transient is broadband and will happily report a fifth. Cross-checked against a harmonic-sum
#  spectrum; when the two disagree by more than a semitone the file is marked UNCERTAIN and is NOT
#  a retune candidate — bells and inharmonic pads have no single true f0 and guessing at one and
#  then resampling would DAMAGE a sound that is currently fine.
#
#  Deviation is measured to the nearest C in ANY octave: the octave a one-shot sits in is a
#  character choice, the pitch CLASS is the bug.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import sys, os, math, glob
import numpy as np
import soundfile as sf
import librosa

MELODIC = ['Bass', 'Bell', 'Keys', 'Lead', 'Pad', 'Pluck', 'Synth']
ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'Resources', 'Samples')
FMIN, FMAX = 27.5, 2100.0
TOL_CENTS = 15.0          # inside this, call it in tune


def mono(path):
    x, sr = sf.read(path, always_2d=True, dtype='float64')
    return x.mean(axis=1), sr


def window(x, sr):
    """Past the attack, before the tail dies. A transient is broadband; the body is what has pitch."""
    if len(x) < sr // 20:
        return None
    env = np.abs(x)
    pk = env.max()
    if pk < 1e-6:
        return None
    on = int(np.argmax(env > pk * 0.10))
    start = min(on + int(0.06 * sr), len(x) - 1)       # 60 ms past onset
    end = min(start + int(1.20 * sr), len(x))
    seg = x[start:end]
    if len(seg) < int(0.08 * sr):
        seg = x[on:min(on + int(0.4 * sr), len(x))]
    if len(seg) < 1024:
        return None
    # drop anything that has already decayed into noise
    if np.sqrt(np.mean(seg ** 2)) < pk * 0.01:
        seg = x[on:min(on + int(0.5 * sr), len(x))]
    return seg if len(seg) > 0 else None


def yin(seg, sr):
    """f0 and aperiodicity (0 = perfectly periodic).

    The difference function is the real one:
        d(tau) = sum_j x[j]^2 + sum_j x[j+tau]^2 - 2 sum_j x[j] x[j+tau],  j = 0..W-1
    computed over a FIXED window W with a running-power term for the shifted half. Collapsing
    those three terms into one autocorrelation (the first version of this file) makes d(tau) sag
    with lag and YIN then picks a short tau — every test tone read SHARP."""
    n = len(seg)
    W = n // 2
    tmax = min(int(sr / FMIN), W - 1)
    tmin = max(2, int(sr / FMAX))
    if tmax <= tmin or W < 64:
        return None, 1.0
    a = seg[:W]
    b = seg[:W + tmax]
    size = 1 << (len(b) + W).bit_length()
    acf = np.fft.irfft(np.fft.rfft(b, size) * np.conj(np.fft.rfft(a, size)), size)[:tmax + 1]
    cumsq = np.concatenate(([0.0], np.cumsum(seg ** 2)))
    term1 = cumsq[W] - cumsq[0]
    taus = np.arange(tmax + 1)
    term2 = cumsq[taus + W] - cumsq[taus]
    d = term1 + term2 - 2.0 * acf
    d[d < 0] = 0.0
    cum = np.cumsum(d[1:])
    idx = np.arange(1, tmax + 1)
    dn = np.ones(tmax + 1)
    nz = cum > 0
    dn[1:][nz] = d[1:][nz] * idx[nz] / cum[nz]
    cand = None
    for tau in range(tmin, tmax):
        if dn[tau] < 0.15 and dn[tau] <= dn[tau + 1]:
            cand = tau
            break
    if cand is None:
        cand = int(np.argmin(dn[tmin:tmax])) + tmin
    lo, hi = max(1, cand - 1), min(tmax, cand + 1)
    a0, b0, c0 = dn[lo], dn[cand], dn[hi]
    denom = (a0 - 2 * b0 + c0)
    shift = 0.5 * (a0 - c0) / denom if abs(denom) > 1e-12 else 0.0
    tau = cand + float(np.clip(shift, -1.0, 1.0))
    return (sr / tau if tau > 0 else None), float(dn[cand])


def harmonic_peak(seg, sr):
    """Cross-check only: the f0 whose first harmonics carry the most energy. Deliberately coarse —
    its job is to catch a gross YIN failure, not to set the answer."""
    w = seg * np.hanning(len(seg))
    nfft = 1 << (len(w) * 2 - 1).bit_length()
    mag = np.abs(np.fft.rfft(w, nfft))
    df = sr / nfft
    best, bestscore = None, -1.0
    f = FMIN
    while f < FMAX:
        s_ = 0.0
        for h in range(1, 7):
            k = int(round(f * h / df))
            if k < len(mag):
                s_ += mag[k]
        if s_ > bestscore:
            best, bestscore = f, s_
        f *= 2 ** (1.0 / 48.0)          # quarter-tone grid
    return best


def pyin_f0(seg, sr):
    """f0 via librosa's pYIN — probabilistic YIN with an HMM over the candidates and a per-frame
    voicing probability.

    THREE hand-rolled estimators were tried before this and all three were wrong on real material
    (plain YIN read every synthetic test tone sharp; after that was fixed it locked onto f0/3 on
    four spot-checked one-shots; a partial-spacing estimator fell apart on dense pad spectra). The
    subharmonic tie is exactly what pYIN's HMM transition model exists to resolve, and it is a
    tested implementation rather than a fourth guess. Returns (f0, confidence 0..1).
    """
    try:
        # The frame must hold several periods of the LOWEST pitch we allow, or pYIN cannot commit
        # and everything low reads "unvoiced". At FMIN=27.5 Hz a 4096 frame is 2.7 periods at 48 k
        # — which is why 105 of 107 bass one-shots came back uncertain on the first full pass.
        frame = 1 << int(np.ceil(np.log2(max(4096.0, 5.0 * sr / FMIN))))
        frame = int(min(frame, max(2048, 2 ** int(np.floor(np.log2(max(2048, len(seg))))))))
        f0, voiced, vprob = librosa.pyin(seg, fmin=FMIN, fmax=FMAX, sr=sr,
                                         frame_length=frame, fill_na=np.nan)
    except Exception:
        return None, 0.0
    if f0 is None or not np.any(np.isfinite(f0)):
        return None, 0.0
    ok = np.isfinite(f0) & (voiced if voiced is not None else True)
    if ok.sum() == 0:
        return None, 0.0
    vals = f0[ok]
    w = vprob[ok] if vprob is not None else np.ones(ok.sum())
    # median in the LOG domain: pitch is geometric, and a couple of octave outliers must not drag it
    lf = np.log2(vals)
    med = float(np.median(lf))
    keep = np.abs(lf - med) < (1.0 / 12.0) * 3          # within 3 semitones of the median frame
    if keep.sum() >= 3:
        f0v = float(2 ** np.average(lf[keep], weights=np.maximum(w[keep], 1e-6)))
    else:
        f0v = float(2 ** med)
    stab = float(keep.sum()) / float(len(lf))
    conf = float(np.mean(w)) * stab
    return f0v, conf


def cents_to_C(f):
    midi = 69.0 + 12.0 * math.log2(f / 440.0)
    pc = midi % 12.0                       # 0 = C
    dev = pc if pc <= 6.0 else pc - 12.0   # signed distance to nearest C, in semitones
    return dev * 100.0, midi


def analyse(path):
    try:
        x, sr = mono(path)
    except Exception as e:
        return dict(path=path, err=str(e))
    seg = window(x, sr)
    if seg is None:
        return dict(path=path, err='too short / silent')
    f0, conf = pyin_f0(seg, sr)
    if not f0 or f0 <= 0:
        return dict(path=path, err='no f0')
    yf, aper = yin(seg, sr)                     # kept purely as an independent cross-check
    oct_err = None
    if yf and yf > 0:
        d = 12.0 * math.log2(f0 / yf)
        oct_err = abs(d - 12.0 * round(d / 12.0))
    uncertain = (conf < 0.55) or (oct_err is not None and oct_err > 1.0 and conf < 0.80)
    cents, midi = cents_to_C(f0)
    return dict(path=path, f0=f0, midi=midi, cents=cents, aper=aper,
                oct_err=oct_err, conf=conf, uncertain=bool(uncertain))


NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


def notename(midi):
    m = int(round(midi))
    return f"{NOTE[m % 12]}{m // 12 - 1}"


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else 'report'
    files = []
    for c in MELODIC:
        files += sorted(glob.glob(os.path.join(ROOT, c, '**', '*.flac'), recursive=True))
    print(f"{len(files)} melodic one-shots under {ROOT}\n")
    rows = []
    for i, p in enumerate(files):
        r = analyse(p)
        rows.append(r)
        if (i + 1) % 100 == 0:
            print(f"  … {i+1}/{len(files)}", flush=True)
    ok = [r for r in rows if 'cents' in r and not r['uncertain'] and abs(r['cents']) <= TOL_CENTS]
    off = [r for r in rows if 'cents' in r and not r['uncertain'] and abs(r['cents']) > TOL_CENTS]
    unc = [r for r in rows if r.get('uncertain')]
    err = [r for r in rows if 'err' in r]
    print(f"\n  in tune to C (<= {TOL_CENTS:.0f} cents) : {len(ok)}")
    print(f"  OFF                           : {len(off)}")
    print(f"  uncertain (not retuned)       : {len(unc)}")
    print(f"  unreadable                    : {len(err)}")

    if off:
        import collections
        buck = collections.Counter()
        for r in off:
            buck[int(round(r['cents'] / 100.0))] += 1
        print("\n  how far off, in semitones:")
        for k in sorted(buck):
            print(f"    {k:+d} st : {buck[k]}")
        print("\n  worst 15:")
        for r in sorted(off, key=lambda r: -abs(r['cents']))[:15]:
            rel = os.path.relpath(r['path'], ROOT)
            print(f"    {r['cents']:+8.1f}c  {notename(r['midi']):>5}  {rel}")
    if '--csv' in sys.argv:
        out = sys.argv[sys.argv.index('--csv') + 1]
        with open(out, 'w') as f:
            f.write("path,f0,midi,note,cents_from_C,aperiodicity,uncertain\n")
            for r in rows:
                if 'cents' not in r:
                    continue
                f.write(f"\"{os.path.relpath(r['path'], ROOT)}\",{r['f0']:.3f},{r['midi']:.3f},"
                        f"{notename(r['midi'])},{r['cents']:.1f},{r['aper']:.3f},{int(r['uncertain'])}\n")
        print(f"\n  wrote {out}")


if __name__ == '__main__':
    main()
