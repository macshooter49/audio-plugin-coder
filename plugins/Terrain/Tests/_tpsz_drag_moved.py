"""Per still of a resize film: does the page look like the reference (the first rest still, at the start
size) scaled to this window's width?  Animated regions (anything that changes between the stills at the
start size - scopes, playheads, meters) are masked out, and both images are blurred a little so
anti-aliasing at a different raster size does not count.  moved = % of static 8x8 blocks whose mean
difference exceeds 14/255 - i.e. something is not where the scaled design puts it."""
import sys, csv, numpy as np
from PIL import Image, ImageFilter
d = sys.argv[1]; o = int(sys.argv[2]); heat = len(sys.argv) > 3
rows = [r for r in csv.reader(open(d + '/steps.tsv'), delimiter='\t')][1:]
snaps = [(float(r[1]), int(r[2]), int(r[3].split()[1])) for r in rows if int(r[0]) == o and r[3].startswith('snap')]
def load(i): return Image.open(f'{d}/o{o}_s{i:03d}.png').convert('L')
RW0 = snaps[0][1]
start = []
for (ms, wd, i) in snaps:
    if wd != RW0: break
    start.append((ms, i))
tEnd = start[-1][0]
startLate = [i for (ms, i) in start if ms > tEnd - 700]     # the settled end of the start hold
ref = load(startLate[-1]); RW = ref.size[0]
TB = ref.size[1] - round(RW * 672 / 820)
strip = lambda w: max(1, round(16 * w / 820))
def webarea(im, w): return im.crop((0, TB, w, im.size[1] - strip(w)))
BL = ImageFilter.GaussianBlur(1.2)
R = webarea(ref, RW)
Rb = np.asarray(R.filter(BL), np.float32)
A = np.zeros(Rb.shape, np.float32)
for i in startLate[:-1]:
    X = np.asarray(webarea(load(i), RW).filter(BL), np.float32)
    if X.shape == Rb.shape: A = np.maximum(A, np.abs(X - Rb))
M = Image.fromarray(((A > 10) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(9))
out = []; lastChange = None; prevW = None
for (ms, wd, idx) in snaps:
    if wd != prevW: lastChange = ms; prevW = wd
    rest = (ms - lastChange) > 550
    im = load(idx); W = im.size[0]
    X = webarea(im, W)
    Rs = R.resize(X.size, Image.BILINEAR).filter(BL)
    Ms = np.asarray(M.resize(X.size, Image.NEAREST)) > 0
    D = np.abs(np.asarray(Rs, np.float32) - np.asarray(X.filter(BL), np.float32))
    D[Ms] = 0
    bh, bw = D.shape[0] // 8, D.shape[1] // 8
    B = D[:bh * 8, :bw * 8].reshape(bh, 8, bw, 8).mean(axis=(1, 3))
    Mb = Ms[:bh * 8, :bw * 8].reshape(bh, 8, bw, 8).mean(axis=(1, 3)) > 0.5
    static = ~Mb
    moved = 100.0 * ((B > 14) & static).sum() / max(1, static.sum())
    out.append((idx, ms, W, rest, float(D[~Ms].mean()) if (~Ms).any() else 0.0, moved))
    if heat and (rest or moved > 3):
        Image.fromarray(np.clip(D * 3, 0, 255).astype(np.uint8)).save(f'{d}/heat_o{o}_s{idx:03d}_{W}.png')
print('# masked (animated) share of the web area: %.1f %%' % (100.0 * (np.asarray(M) > 0).mean()))
for idx, ms, W, rest, err, moved in out:
    print('%3d %7.0f ms  w=%4d %s  err %5.2f  moved %5.1f %%' % (idx, ms, W, 'REST' if rest else '    ', err, moved))
