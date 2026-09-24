"""GCC-PHAT match for the noise samples (breath _b_, key _k_): a whitened cross-correlation that
finds the same noise realisation even through MP3 coding. Peak-to-sidelobe ratio (PSR) is the score."""
import os, json, subprocess, sys
import numpy as np, soundfile as sf
from scipy.signal import resample_poly
from concurrent.futures import ProcessPoolExecutor

D = os.path.dirname(os.path.abspath(__file__))
SR = 22050
SAMPLES = os.path.expanduser('~/Developer/VST-Plugins/organics-library/raw/sfzinstruments/MTG.SoloSax/MTG Solo Saxophones/Samples')
S = json.load(open(os.path.join(D, 'sounds.json')))
PACK = {'alt': '20239', 'ten': '20247', 'sop': '20251', 'bar': '20253'}
PREV = None


def load_prev(sid):
    raw = subprocess.run(['ffmpeg', '-v', 'quiet', '-i', os.path.join(D, 'prev', sid + '.mp3'), '-ac', '1', '-ar', str(SR), '-f', 'f32le', '-'],
                         capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def init():
    global PREV
    PREV = {sid: load_prev(sid) for sid in S}


def phat(y, x):
    n = 1
    while n < len(y) + len(x):
        n *= 2
    Y = np.fft.rfft(y, n)
    X = np.fft.rfft(x, n)
    R = Y * np.conj(X)
    # band-limit to 300 Hz - 7 kHz where MP3 keeps the noise
    f = np.fft.rfftfreq(n, 1 / SR)
    w = ((f > 300) & (f < 7000)).astype(float)
    R = R / (np.abs(R) + 1e-12) * w
    c = np.fft.irfft(R, n)[:len(y)]
    return c


def best(fn):
    x, sr = sf.read(os.path.join(SAMPLES, fn), always_2d=True)
    x = x.mean(1)
    g = np.gcd(sr, SR)
    x = resample_poly(x, SR // g, sr // g)
    pid = PACK[fn[:3]]
    sc = []
    for sid, v in S.items():
        if v['pack'] != pid:
            continue
        c = phat(PREV[sid], x)
        k = int(np.argmax(c))
        pk = c[k]
        side = np.delete(c, range(max(0, k - 20), k + 21))
        psr = (pk - side.mean()) / (side.std() + 1e-12)
        sc.append((float(psr), sid, k / SR))
    sc.sort(reverse=True)
    return fn, sc[0], sc[1]


if __name__ == '__main__':
    pat = sys.argv[1] if len(sys.argv) > 1 else '_b_'
    files = sorted(f for f in os.listdir(SAMPLES) if any(p in f for p in pat.split(',')))
    with ProcessPoolExecutor(max_workers=8, initializer=init) as ex:
        res = list(ex.map(best, files, chunksize=2))
    out = {fn: dict(sid=b[1], psr=round(b[0], 1), at=round(b[2], 3), second_sid=s[1], second_psr=round(s[0], 1)) for fn, b, s in res}
    json.dump(out, open(os.path.join(D, 'match_phat.json'), 'w'), indent=1)
    ps = np.array([v['psr'] for v in out.values()])
    s2 = np.array([v['second_psr'] for v in out.values()])
    print('files', len(out), 'psr min/median', ps.min(), np.median(ps), 'second median', np.median(s2))
    for fn, v in sorted(out.items(), key=lambda kv: kv[1]['psr'])[:12]:
        print(fn, v)
