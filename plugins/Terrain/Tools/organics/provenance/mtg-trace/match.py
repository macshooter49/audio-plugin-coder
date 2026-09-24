"""Match every MTG.SoloSax sample file to the Freesound sound it was cut from (by waveform correlation
against the public HQ previews), so provenance is per-file, not per-pack."""
import os, json, subprocess, glob, re
import numpy as np, soundfile as sf
from scipy.signal import fftconvolve, resample_poly
from concurrent.futures import ProcessPoolExecutor

D = os.path.dirname(os.path.abspath(__file__))
SR = 8000
SAMPLES = os.path.expanduser('~/Developer/VST-Plugins/organics-library/raw/sfzinstruments/MTG.SoloSax/MTG Solo Saxophones/Samples')
S = json.load(open(os.path.join(D, 'sounds.json')))
PACK = {'alt': '20239', 'ten': '20247', 'sop': '20251', 'bar': '20253'}


def load_prev(sid):
    raw = subprocess.run(['ffmpeg', '-v', 'quiet', '-i', os.path.join(D, 'prev', sid + '.mp3'), '-ac', '1', '-ar', str(SR), '-f', 'f32le', '-'],
                         capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def load_smp(p):
    x, sr = sf.read(p, always_2d=True)
    x = x.mean(1)
    g = np.gcd(sr, SR)
    return resample_poly(x, SR // g, sr // g)


PREV = None


def init():
    global PREV
    PREV = {sid: load_prev(sid) for sid in S}


def best(fn):
    x = load_smp(os.path.join(SAMPLES, fn))
    x = x[:SR * 3]                     # first 3 s is plenty
    x = x - x.mean()
    ex = np.sqrt((x * x).sum()) + 1e-12
    pid = PACK[fn[:3]]
    scores = []
    for sid, v in S.items():
        if v['pack'] != pid:
            continue
        y = PREV[sid]
        if len(y) < len(x):
            y = np.pad(y, (0, len(x) - len(y)))
        c = fftconvolve(y, x[::-1], mode='valid')
        e = np.sqrt(np.convolve(y * y, np.ones(len(x)), mode='valid')) + 1e-12
        r = c / (ex * e)
        k = int(np.argmax(r))
        scores.append((float(r[k]), sid, k / SR))
    scores.sort(reverse=True)
    return fn, scores[0], scores[1]


if __name__ == '__main__':
    files = sorted(os.listdir(SAMPLES))
    with ProcessPoolExecutor(max_workers=8, initializer=init) as ex:
        res = list(ex.map(best, files, chunksize=4))
    out = {fn: dict(sid=b[1], r=round(b[0], 4), at=round(b[2], 3), second_sid=s[1], second_r=round(s[0], 4)) for fn, b, s in res}
    json.dump(out, open(os.path.join(D, 'match.json'), 'w'), indent=1)
    rs = np.array([v['r'] for v in out.values()])
    print('files', len(out), 'r min/median', rs.min(), np.median(rs))
    for fn, v in sorted(out.items(), key=lambda kv: kv[1]['r'])[:15]:
        print(fn, v)
