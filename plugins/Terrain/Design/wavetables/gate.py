#!/usr/bin/env python3
"""
gate.py — build every generated table, MEASURE it, prove no two sound alike, keep the best 120.

The uniqueness claim is the one the owner cares most about: "NO wavetable should sound exactly
alike." A claim like that needs a number and a threshold, and a threshold needs a justification —
so this script CALIBRATES the metric instead of asserting a magic constant:

  * the distance metric is mean |dB| over 16 frame positions x 48 log-spaced harmonic bands, each
    frame normalised to its own peak. Frame ORDER is significant, because a swept table is heard
    in order.
  * calibration A (floor): the SAME table compared against a version of itself with a small
    spectral tilt applied — a change that is real but musically trivial. Anything at or below
    this distance is "the same table wearing a hat".
  * calibration B (ceiling): a pure sine against a full saw — about as different as two periodic
    waveforms get.
  Everything is reported against those two anchors, so the threshold is defensible rather than
  invented.
"""
import sys, os, glob, importlib.util, math, json, csv
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import wtlib

HERE   = os.path.dirname(os.path.abspath(__file__))
OUT    = os.path.join(HERE, "bank")
TARGET = 120

# The collision threshold is MEASURED, not invented: two tables must be at least as far apart as
# a saw is from a gently re-tilted saw. calibrate() computes it at run time and main() uses it.
COLLIDE = None   # set from calibration
CLOSE   = None   # 1.5x the floor — "related but clearly distinguishable"

# Per-category quotas. WITHOUT THESE the global score cut deleted all 12 PHYSICAL tables, because
# modal and waveguide sources are darker than spectral ones and the score rewards brightness —
# i.e. the selection was throwing away precisely the tables no other synth can ship. Quotas also
# guarantee FOUNDATION exists, which is the "few basic ones, in their own folder" the owner asked
# for. Any category that cannot fill its quota releases the remainder to the others.
QUOTA = {"FOUNDATION": 16, "HARMONIC": 12, "PHYSICAL": 12, "DIGITAL": 16,
         "SPECTRAL": 16, "CHAOS": 16, "CINEMATIC": 16, "VOCAL": 16}


def load_modules():
    mods = []
    for path in sorted(glob.glob(os.path.join(HERE, "gen_*.py"))):
        name = os.path.splitext(os.path.basename(path))[0]
        spec = importlib.util.spec_from_file_location(name, path)
        m = importlib.util.module_from_spec(spec)
        try:
            spec.loader.exec_module(m)
        except Exception as e:
            print(f"  !! {name} FAILED TO IMPORT: {e}")
            continue
        if not hasattr(m, "TABLES"):
            print(f"  !! {name} has no TABLES — skipped")
            continue
        mods.append((name, m))
    return mods


def category_of(mod, name):
    if hasattr(mod, "CATEGORY") and name in mod.CATEGORY:
        return mod.CATEGORY[name]
    n = mod.__name__.replace("gen_", "").upper()
    return n.split("_")[0]


def calibrate():
    """Two anchors so the threshold means something."""
    n_ax = wtlib.n_ax
    saw = wtlib.cycles_from_mags(np.tile(1.0 / n_ax, (wtlib.FRAMES, 1)))
    saw = wtlib.finalize(saw)
    sine = np.zeros((wtlib.FRAMES, wtlib.NH)); sine[:, 0] = 1.0
    sine = wtlib.finalize(wtlib.cycles_from_mags(sine))
    tilt = wtlib.finalize(wtlib.cycles_from_mags(
        np.tile((1.0 / n_ax) * n_ax ** -0.15, (wtlib.FRAMES, 1))))
    floor = wtlib.distance(wtlib.fingerprint(saw), wtlib.fingerprint(tilt))
    ceil_ = wtlib.distance(wtlib.fingerprint(saw), wtlib.fingerprint(sine))
    return floor, ceil_


def main():
    print("\n══ TERRAIN FACTORY WAVETABLE BANK — build · measure · prove distinct ══\n")
    global COLLIDE, CLOSE
    floor, ceil_ = calibrate()
    COLLIDE = floor
    CLOSE   = floor * 1.5
    print(f"CALIBRATION   saw vs gently-tilted saw (musically trivial) = {floor:6.2f} dB")
    print(f"              saw vs pure sine  (about as far as it gets)  = {ceil_:6.2f} dB")
    print(f"              -> collision threshold = the floor, {COLLIDE:.2f} dB · 'close' band to {CLOSE:.2f} dB\n")

    mods = load_modules()
    print(f"generators: {', '.join(n for n, _ in mods)}\n")

    entries = []
    for modname, mod in mods:
        for name, fn in mod.TABLES:
            try:
                fr = fn()
            except Exception as e:
                print(f"  !! {name} raised: {e}")
                continue
            if fr.shape != (wtlib.FRAMES, wtlib.SIZE):
                print(f"  !! {name} wrong shape {fr.shape} — skipped"); continue
            m = wtlib.measure(fr)
            entries.append(dict(name=name, cat=category_of(mod, name), frames=fr,
                                fp=wtlib.fingerprint(fr), mod=modname, **m))
    print(f"built {len(entries)} candidate tables\n")
    if not entries:
        print("NOTHING BUILT — no generator produced a table."); return 1

    # quality score: brightness (log-scaled, saturating) + travel. Used only to pick a survivor
    # when two tables collide, never to gate.
    for e in entries:
        e['score'] = math.log10(max(e['harm60'], 1)) * 20 + min(e['span'], 80)

    # ── pairwise ──────────────────────────────────────────────────────────────────────
    F = np.array([e['fp'] for e in entries])
    n = len(entries)
    D = np.zeros((n, n))
    for i in range(n):
        D[i] = np.abs(F - F[i]).mean(axis=1)
        D[i, i] = np.inf

    dropped, reasons = set(), {}
    while True:
        live = [i for i in range(n) if i not in dropped]
        if len(live) < 2: break
        sub = D[np.ix_(live, live)]
        k = int(np.argmin(sub)); i, j = live[k // len(live)], live[k % len(live)]
        if D[i, j] > COLLIDE: break
        lose = i if entries[i]['score'] < entries[j]['score'] else j
        keep = j if lose == i else i
        dropped.add(lose)
        reasons[entries[lose]['name']] = f"collided with {entries[keep]['name']} at {D[i,j]:.2f} dB"

    survivors = [i for i in range(n) if i not in dropped]

    # quota selection, per category, best-scoring first
    bycat_idx = {}
    for i in survivors: bycat_idx.setdefault(entries[i]['cat'], []).append(i)
    for c in bycat_idx: bycat_idx[c].sort(key=lambda i: -entries[i]['score'])
    chosen, spare = [], []
    for c, idxs in bycat_idx.items():
        q = QUOTA.get(c, 0)
        chosen += idxs[:q]; spare += idxs[q:]
    spare.sort(key=lambda i: -entries[i]['score'])
    while len(chosen) < TARGET and spare: chosen.append(spare.pop(0))
    if len(chosen) > TARGET:
        chosen.sort(key=lambda i: -entries[i]['score']); chosen = chosen[:TARGET]
    for i in survivors:
        if i not in chosen: reasons[entries[i]['name']] = "over category quota / target"
    survivors = chosen

    survivors.sort(key=lambda i: (entries[i]['cat'], entries[i]['name']))
    S = [entries[i] for i in survivors]

    # ── final uniqueness proof over the SURVIVING set ─────────────────────────────────
    FS = np.array([e['fp'] for e in S])
    m = len(S)
    DS = np.zeros((m, m))
    for i in range(m):
        DS[i] = np.abs(FS - FS[i]).mean(axis=1); DS[i, i] = np.inf
    mn = DS.min()
    ii, jj = np.unravel_index(np.argmin(DS), DS.shape)
    tri = DS[np.triu_indices(m, 1)]

    os.makedirs(OUT, exist_ok=True)
    bycat = {}
    for e in S:
        d = os.path.join(OUT, e['cat']); os.makedirs(d, exist_ok=True)
        wtlib.write_wav(os.path.join(d, e['name'] + ".wav"), e['frames'])
        bycat.setdefault(e['cat'], []).append(e)

    print(f"{'category':<12} {'tables':>6} {'mean harm60':>12} {'median span':>12}")
    print("-" * 46)
    for c in sorted(bycat):
        v = bycat[c]
        print(f"{c:<12} {len(v):>6} {int(np.mean([x['harm60'] for x in v])):>12} "
              f"{np.median([x['span'] for x in v]):>11.1f}")
    print("-" * 46)
    print(f"{'TOTAL':<12} {m:>6} {int(np.mean([x['harm60'] for x in S])):>12} "
          f"{np.median([x['span'] for x in S]):>11.1f}")
    print(f"\nSerum 2 factory: 371 tables · mean harm60 278 · median span 19.9 st")

    print(f"\n══ UNIQUENESS ══  {m*(m-1)//2:,} pairs compared")
    print(f"   closest pair      {mn:6.2f} dB   {S[ii]['name']}  vs  {S[jj]['name']}")
    print(f"   5th percentile    {np.percentile(tri,5):6.2f} dB")
    print(f"   median            {np.percentile(tri,50):6.2f} dB")
    print(f"   calibration floor {floor:6.2f} dB  (saw vs gently-tilted saw)")
    close_pairs = int((tri <= CLOSE).sum())
    print(f"   pairs within the 'close' band (<= {CLOSE} dB): {close_pairs}")
    print(f"   VERDICT: {'PASS — every pair is further apart than a trivial re-tilt' if mn > COLLIDE else 'FAIL — a collision survived'}")

    if dropped:
        print(f"\n══ DROPPED ({len(dropped)}) ══")
        for nm, why in sorted(reasons.items()): print(f"   {nm:<32} {why}")

    # import-compatibility check, run as the plugin's own condition
    bad = 0
    for e in S:
        ns = e['frames'].size
        if not (ns >= 4096 and ns % 2048 == 0 and ns // 2048 <= 256): bad += 1
    print(f"\n══ IMPORT CHECK (PluginProcessor.cpp:626 condition) ══")
    print(f"   {m-bad}/{m} import as {wtlib.FRAMES}-frame wavetables, float32 mono 44100 Hz"
          f"   {'ALL PASS' if bad == 0 else str(bad)+' FAILED'}")

    with open(os.path.join(OUT, "MANIFEST.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["category", "name", "frames", "harm60", "harm80", "span_st", "crest", "zero_crossings"])
        for e in S: w.writerow([e['cat'], e['name'], wtlib.FRAMES, e['harm60'], e['harm80'],
                                round(e['span'], 2), round(e['crest'], 2), round(e['zc'], 1)])
    print(f"\n   wrote {m} wav files + MANIFEST.csv to {OUT}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
