import sys, json, os, numpy as np, soundfile as sf, librosa
from multiprocessing import Pool
root = sys.argv[1]; man = json.load(open(os.path.join(root, 'manifest.json')))['items']
def chk(it):
    if it['shift'] is None: return None
    p = os.path.join(root, it['cat'], it['name'] + '.flac'); y, sr = sf.read(p, dtype='float32', always_2d=True); m = librosa.resample(y.mean(1), orig_sr=sr, target_sr=22050)
    lead = int(np.argmax(np.abs(m) > np.abs(m).max() * 10 ** (-45 / 20))) / 22050
    f0, vf, vp = librosa.pyin(m[: int(2.5 * 22050)], fmin=30, fmax=2100, sr=22050, frame_length=2048, hop_length=512); ok = vf & (vp > .6)
    pk = float(np.abs(y).max())
    if ok.sum() < 4: return (it['name'], it['keyhow'], None, lead, pk)
    md = float(np.median(librosa.hz_to_midi(f0[ok]))); dev = ((md + 6) % 12) - 6
    return (it['name'], it['keyhow'], round(dev, 2), round(lead * 1000, 1), round(20 * np.log10(pk), 2))
with Pool(8) as p: R = [r for r in p.map(chk, man) if r]
meas = [r for r in R if r[2] is not None]; off = [r for r in meas if abs(r[2]) > .35]
print('shifted', len(R), 'measurable', len(meas), 'within ±35 cents of C', len(meas) - len(off))
print('worst', sorted(meas, key=lambda r: -abs(r[2]))[:8])
print('lead-in ms max', max(r[3] for r in meas) if meas else None, 'peaks dBFS', sorted(set(r[4] for r in meas))[:3], '…', sorted(set(r[4] for r in meas))[-3:])
