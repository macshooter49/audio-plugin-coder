#!/usr/bin/env python3
"""fb638 — NO DROPOUTS WHEN YOU SCAN THE TABLE.

Terrain's Wavetable::lookup crossfades BETWEEN adjacent frames as WT POS moves. If two neighbouring frames are anti-correlated
(one is roughly the other upside down, or shifted half a cycle), the crossfade cancels: the midpoint plays quiet — a hole in
the level when an LFO sweeps the table. The Processed critic measured it (Edge Fold Square -18 dB, Ring Modulated Saw 67 dips).

The cure changes NOTHING a fingerprint can see: each frame is rotated in time (a circular shift) and/or flipped in polarity so
it lines up with the frame before it. The magnitude spectrum of every frame is unchanged, so the table's identity, its
distance to every other table and the selection are all untouched — only the cancellation goes away.

    python3 align_frames.py <bank_dir>            # fix in place every table whose worst midpoint dip is below -6 dB
    python3 align_frames.py <bank_dir> --check    # report only; exit 1 if any table still fails the bar
BAR: worst midpoint dip >= -9 dB and median midpoint dip >= -4.5 dB (two unrelated noise frames sit at -3 dB by nature).
"""
import os, sys, glob, numpy as np, soundfile as sf

WORST_BAR, MEDIAN_BAR, TRY_BELOW = -9.0, -4.5, -6.0

def dips(F):
    rms = np.sqrt((F ** 2).mean(1))
    mid = np.sqrt(((0.5 * (F[:-1] + F[1:])) ** 2).mean(1))
    ref = np.sqrt(0.5 * (rms[:-1] ** 2 + rms[1:] ** 2))
    return 20 * np.log10(np.maximum(mid, 1e-12) / np.maximum(ref, 1e-12))

def align(F):
    """Each frame -> the circular shift and polarity that best correlates with the (already aligned) frame before it."""
    G = F.copy()
    for i in range(1, len(G)):
        a, b = G[i - 1], G[i]
        xc = np.fft.irfft(np.fft.rfft(a) * np.conj(np.fft.rfft(b)), n=len(a))   # xc[k] = sum a[n] b[n-k]
        k = int(np.argmax(np.abs(xc)))
        s = np.roll(b, k)
        G[i] = s if xc[k] >= 0 else -s
    return G

def main():
    bank = sys.argv[1]; check = '--check' in sys.argv
    fails, fixed = [], []
    for p in sorted(glob.glob(os.path.join(bank, '*', '*.wav'))):
        x, sr = sf.read(p, dtype='float64'); F = x.reshape(-1, 2048)
        d = dips(F)
        if not check and d.min() < TRY_BELOW:
            G = align(F); dg = dips(G)
            if dg.min() > d.min() + 1.0:            # keep the alignment only if it clearly helps
                sf.write(p, G.reshape(-1).astype(np.float32), sr, subtype='FLOAT')
                fixed.append((os.path.relpath(p, bank), d.min(), dg.min(), np.median(d), np.median(dg))); d = dg
        if d.min() < WORST_BAR or np.median(d) < MEDIAN_BAR:
            fails.append((os.path.relpath(p, bank), d.min(), np.median(d)))
    for r, a, b, ma, mb in fixed:
        print('  aligned %-40s worst %6.1f -> %6.1f dB   median %5.2f -> %5.2f' % (r, a, b, ma, mb))
    for r, w, m in fails:
        print('  ✗ STILL DROPS OUT %-34s worst %6.1f dB  median %5.2f dB' % (r, w, m))
    print('align_frames: %d aligned · %d fail the bar (worst >= %.0f dB, median >= %.1f dB)' % (len(fixed), len(fails), WORST_BAR, MEDIAN_BAR))
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
