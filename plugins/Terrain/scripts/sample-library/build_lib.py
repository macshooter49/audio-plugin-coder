# tp12 — Terrain's factory one-shot library: Max's kits → trimmed, keyed to C, normalised, deduped, renamed FLAC.
import os, sys, re, json, math, subprocess, tempfile, hashlib
import numpy as np, soundfile as sf, librosa
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(__file__)); from rules import exclude, classify
PC={'C':0,'C#':1,'DB':1,'D':2,'D#':3,'EB':3,'E':4,'F':5,'F#':6,'GB':6,'G':7,'G#':8,'AB':8,'A':9,'A#':10,'BB':10,'B':11}
NOSHIFT={'Drums','FX','Misc'}
def label_pc(name):
    b=os.path.splitext(os.path.basename(name))[0]
    m=re.search(r'\((?:\d+\s*(?:bpm)?\s*)?([A-Ga-g])([#b]?)\s*(?:min|maj|minor|major|m)?\s*\)', b, re.I) \
      or re.search(r'(?:^|[\s_\-])([A-G])([#b]?)(?:min|maj|m)?$', b)
    if not m: return None
    return PC.get((m.group(1)+m.group(2)).upper())
def load(p):
    x, sr = sf.read(p, dtype='float32', always_2d=True)
    if x.shape[1] > 2: x = x[:, :2]
    if sr not in (44100, 48000):
        x = librosa.resample(x.T, orig_sr=sr, target_sr=48000, res_type='soxr_hq').T.astype(np.float32); sr = 48000
    return x, sr
def trim(x, sr):
    a = np.max(np.abs(x), axis=1); pk = float(a.max()) if a.size else 0
    if pk <= 1e-6: return None
    thr = max(pk * 10 ** (-45 / 20), 10 ** (-66 / 20))
    i0 = int(np.argmax(a > thr)); i0 = max(0, i0 - int(0.0015 * sr))
    tail = np.where(a > 10 ** (-84 / 20))[0]; i1 = int(tail[-1]) + 1 if tail.size else len(a)
    y = x[i0:i1].copy(); n = min(len(y), int(0.001 * sr))
    if n > 1: y[:n] *= np.linspace(0, 1, n, dtype=np.float32)[:, None]
    m = min(len(y), int(0.004 * sr))
    if m > 1: y[-m:] *= np.linspace(1, 0, m, dtype=np.float32)[:, None]
    return y
def feats(y, sr):
    mono = librosa.resample(y.mean(axis=1), orig_sr=sr, target_sr=22050, res_type='soxr_hq'); s = 22050
    dur = len(mono) / s
    hop = 256; env = librosa.feature.rms(y=mono, frame_length=1024, hop_length=hop)[0] + 1e-9
    w = env[: max(2, int(1.0 * s / hop))]; ipk = int(np.argmax(w)); att = int(np.argmax(w >= 0.5 * w[ipk])) * hop / s   # onset → half the first second's peak: the real attack, not the loudest moment
    edb = 20 * np.log10(env / env[ipk])
    after = np.where(edb[ipk:] < -20)[0]; dec = (after[0] * hop / s) if after.size else dur - att
    a, b = int(len(env) * .4), int(len(env) * .6); sus = float(env[a:b].mean() / env[ipk]) if b > a else 0
    seg = mono[: int(min(dur, 2.5) * s)]
    f0, vf, vp = librosa.pyin(seg, fmin=30, fmax=2100, sr=s, frame_length=2048, hop_length=512)
    ok = vf & (vp > 0.6); vfrac = float(ok.mean()) if ok.size else 0
    midi = float(np.median(librosa.hz_to_midi(f0[ok]))) if ok.sum() >= 4 else None
    ch = librosa.feature.chroma_cqt(y=mono[: int(min(dur, 3.0) * s)], sr=s, hop_length=512).mean(axis=1)
    cpc = int(np.argmax(ch)); cprom = float(ch.max() / (ch.mean() + 1e-9))
    cen = float(librosa.feature.spectral_centroid(y=seg, sr=s).mean()); flat = float(librosa.feature.spectral_flatness(y=seg).mean())
    fp = librosa.resample(mono[: int(min(dur, 4.0) * s)], orig_sr=s, target_sr=2000)
    fe = librosa.feature.rms(y=fp, frame_length=40, hop_length=40)[0]; fe = fe / (np.linalg.norm(fe) + 1e-9)
    return dict(dur=dur, att=att, dec=dec, sus=sus, vfrac=vfrac, midi=midi, cpc=cpc, cprom=cprom, cen=cen, flat=flat, fp=fe.tolist())
def audio_cat(f):
    if f['midi'] is not None and f['midi'] < 43 and f['vfrac'] > .3: return 'Bass'
    if f['flat'] > .25 and f['vfrac'] < .15 and f['cprom'] < 1.5: return 'FX'
    if f['att'] > .08 or (f['sus'] > .45 and f['dur'] > 2.5): return 'Pad'
    if f['att'] < .03 and f['dec'] < .6: return 'Pluck'
    if f['att'] < .03 and f['cen'] > 1500: return 'Bell'
    if f['att'] < .03: return 'Keys'
    if f['vfrac'] < .1 and f['cprom'] < 1.4: return 'Misc'
    return 'Synth'
def key_of(o, f, cat):
    if cat in NOSHIFT and o.get('lab') is None: return None, 'no-shift category'
    lab = o.get('lab')
    if lab is not None:
        if f['midi'] is not None and round(f['midi']) % 12 == lab and f['vfrac'] > .3: return f['midi'], 'label+pyin'
        return float(lab), 'label'
    if f['midi'] is not None and f['vfrac'] > .3: return f['midi'], 'pyin'
    if f['cprom'] >= 1.5: return float(f['cpc']), 'chroma'
    return None, 'unpitched'
def shift_of(x): return ((0 - x + 6) % 12) - 6
def analyze(o):
    try:
        x, sr = load(o['path']); y = trim(x, sr)
        if y is None or len(y) < sr * 0.03: return dict(o, bad='silent/short')
        f = feats(y, sr); o = dict(o, **f); o['lab'] = label_pc(o['rel'])
        cat = o.get('rule') or audio_cat(f); o['cat'] = cat
        k, how = key_of(o, f, cat); o['key'] = k; o['keyhow'] = how
        o['shift'] = None if k is None else round(shift_of(k), 3)
        return o
    except Exception as e: return dict(o, bad=str(e)[:80])
SUB={'kick':'Kick','snare':'Snare','rim':'Rim','clap':'Clap','open hat':'Open Hat','hat':'Hat','shaker':'Shaker','tom':'Tom','crash':'Crash','cymbal':'Cymbal'}
def drum_name(o):
    t = re.sub(r'[_\-\.]+', ' ', o['rel'].lower())
    for k, v in SUB.items():
        if re.search(r'\b' + k + r's?\b', t): return v
    return 'Perc'
def render(job):
    o, dst = job
    try:
        x, sr = load(o['path']); y = trim(x, sr)
        s = o.get('shift')
        if s is not None and abs(s) >= 0.05:
            with tempfile.TemporaryDirectory() as td:
                a, b = os.path.join(td, 'a.wav'), os.path.join(td, 'b.wav'); sf.write(a, y, sr, subtype='FLOAT')
                subprocess.run(['rubberband', '-3', '-F', '-q', '-p', f'{s:.4f}', a, b], check=True, capture_output=True)
                y, _ = sf.read(b, dtype='float32', always_2d=True)
            y = trim(y, sr) if trim(y, sr) is not None else y
        pk = float(np.max(np.abs(y))); y = y * (10 ** (-1 / 20) / pk) if pk > 0 else y
        os.makedirs(os.path.dirname(dst), exist_ok=True); sf.write(dst, y, sr, subtype='PCM_24', format='FLAC')
        return dst, None
    except Exception as e: return dst, str(e)[:120]
if __name__ == '__main__':
    inv, out, limit = sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 0
    items = [o for o in json.load(open(inv)) if not exclude(o)]
    for o in items: o['rule'] = classify(o)
    if limit: items = items[::max(1, len(items) // limit)][:limit]
    with Pool(8) as p: A = p.map(analyze, items, chunksize=4)
    bad = [a for a in A if a.get('bad')]; A = [a for a in A if not a.get('bad')]
    # duplicates: same length (±2 %) and the same envelope (cos > .995) → keep the first (higher sample rate first)
    A.sort(key=lambda a: (-a.get('sr', 0), a['kit'], a['rel'])); keep = []; dups = []
    for a in A:
        fa = np.array(a['fp']); d = next((k for k in keep if abs(k['dur'] - a['dur']) <= .02 * max(a['dur'], k['dur']) and len(k['fp']) == len(fa) and float(np.dot(np.array(k['fp']), fa)) > .995), None)
        (dups.append((a['kit'] + ':' + a['rel'], d['kit'] + ':' + d['rel'])) if d else keep.append(a))
    keep.sort(key=lambda a: (a['cat'], a['kit'], a['rel'].lower())); cnt = {}; jobs = []
    for a in keep:
        stem = drum_name(a) if a['cat'] == 'Drums' else a['cat']; cnt[stem] = cnt.get(stem, 0) + 1
        a['name'] = f"{stem} {cnt[stem]:02d}"; jobs.append((a, os.path.join(out, a['cat'], a['name'] + '.flac')))
    with Pool(8) as p: R = p.map(render, jobs, chunksize=2)
    errs = [r for r in R if r[1]]
    man = [{k: a.get(k) for k in ('name', 'cat', 'kit', 'rel', 'keyhow', 'key', 'shift', 'dur', 'att', 'dec', 'sus', 'vfrac', 'cen')} for a in keep]
    json.dump({'items': man, 'dups': dups, 'bad': [(b['rel'], b['bad']) for b in bad], 'errors': errs}, open(os.path.join(out, 'manifest.json'), 'w'), indent=1)
    from collections import Counter
    print('rendered', len(keep), 'dups', len(dups), 'bad', len(bad), 'errors', len(errs)); print(dict(Counter(a['cat'] for a in keep))); print(dict(Counter(a['keyhow'] for a in keep)))
    for e in errs[:5]: print('ERR', e)
