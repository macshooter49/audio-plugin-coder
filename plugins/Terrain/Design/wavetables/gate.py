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
# fb606 — the bank is ~120 MB of wav and must not land in the worktree. Point TERRAIN_WT_OUT
# at the real destination (<terrainDataDir()>/Wavetables/Factory) and gate.py writes straight
# there; with nothing set it still falls back to ./bank so the documented run works unchanged.
OUT    = os.environ.get("TERRAIN_WT_OUT") or os.path.join(HERE, "bank")
TARGET = 120

# The collision threshold is MEASURED, not invented: two tables must be at least as far apart as
# a saw is from a gently re-tilted saw. calibrate() computes it at run time and main() uses it.
COLLIDE = None   # set from calibration
CLOSE   = None   # 1.5x the floor — "related but clearly distinguishable"

# ══════════════════════════════════════════════════════════════════════════════════════════
# PER-CATEGORY QUOTAS — fb606, re-balanced from eight categories to the merged TEN.
#
# ⚠️ WITHOUT THESE the global score cut deleted ALL TWELVE PHYSICAL TABLES. The score rewards
# brightness and modal/waveguide sources are darker by nature, so an unquota'd selection throws
# away precisely the tables no other synth can ship. That failure is the reason this table
# exists and it is just as live with ten categories as it was with eight: Physical and Basic
# Shapes are BOTH structurally dark (a sine is one harmonic by definition) and would BOTH be
# cut by a global ranking. Do not replace this with a score threshold.
#
# How the ten are sized. Each quota is set from the candidate pool behind it, so no category is
# asked for tables that do not exist and none of them ships its weakest material just to hit a
# round number. Candidates -> quota:
#     Basic Shapes 12->10 · Analog 14->12 · Digital 22->14 · Vocal 24->14 · Metallic 12->12
#     Spectral 22->14 · Chaos 24->14 · Cinematic 12->10 · Harmonic 12->10 · Physical 12->10
# The quotas sum to exactly TARGET, so nothing is left to the "spare" pass unless a category
# under-fills — and when one does, the remainder is released to the others rather than
# shipping a short bank. Metallic is quota==pool: those twelve struck/rung tables have no
# competition to survive, they simply all ship. Physical is deliberately quota < pool so it
# can absorb the one modal collision the projection grid still cannot resolve (see
# bank2_probe.cpp, BARS_GLASS) and STILL fill its ten slots.
# ══════════════════════════════════════════════════════════════════════════════════════════
QUOTA = {"Basic Shapes": 10, "Analog":    12, "Digital":   14, "Vocal":    14,
         "Metallic":     12, "Spectral":  14, "Chaos":     14, "Cinematic": 10,
         "Harmonic":     10, "Physical":  10}
assert sum(QUOTA.values()) == 120, "quotas must sum to TARGET"


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
    """fb606 — every generator DECLARES its merged-ten folder; guessing is a hard error.

    This used to fall back to the module name upper-cased, which is exactly how the bank ended
    up with folders the browser had never heard of. If a table has no CATEGORY entry that is a
    bug in the generator, and a loud one beats a table silently filed under "GEN_SOMETHING".
    """
    if hasattr(mod, "CATEGORY") and name in mod.CATEGORY:
        c = mod.CATEGORY[name]
        if c not in QUOTA:
            raise KeyError(f"{mod.__name__}: '{name}' claims category '{c}', "
                           f"which is not one of the merged ten {sorted(QUOTA)}")
        return c
    raise KeyError(f"{mod.__name__}: '{name}' has no CATEGORY entry — add it to that module's "
                   f"CATEGORY dict. The merged ten are {sorted(QUOTA)}.")


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

    # fb606 — SWEEP THE OLD TAXONOMY. This directory is the live factory folder, so a rerun that
    # renames FOUNDATION to "Basic Shapes" must not leave FOUNDATION sitting next to it — the
    # browser would show both and the owner asked for no folder that isn't one of the ten.
    swept = []
    for d in sorted(os.listdir(OUT)):
        p = os.path.join(OUT, d)
        if os.path.isdir(p) and d not in QUOTA:
            for f in glob.glob(os.path.join(p, "*.wav")): os.remove(f)
            try: os.rmdir(p); swept.append(d)
            except OSError: print(f"   sweep: {d} is not one of the ten but is NOT EMPTY of "
                                  f"non-wav files — left in place, look at it")
    print("   sweep of folders outside the merged ten: "
          + (f"FIRED — removed {', '.join(swept)}" if swept else "did not fire (nothing stale)"))

    # fb606 — FILENAME COLLISION DETECTOR. Four table names exist twice in the candidate pool
    # (SIERPINSKI, DUST, SHATTER, SIEVE each appear in two generators). Today the merged
    # taxonomy keeps every such pair in DIFFERENT folders, so nothing collides — but re-file one
    # of them and the second write would silently overwrite the first and the bank would be 119
    # tables while every count still said 120. Check it every run and say so either way.
    seen, clash = {}, []
    for e in S:
        k = (e['cat'], e['name'])
        if k in seen: clash.append(f"{e['cat']}/{e['name']} (from {seen[k]} and {e['mod']})")
        seen[k] = e['mod']
    print("   filename-collision check: "
          + (f"FIRED — {len(clash)} CLASH(ES): {'; '.join(clash)}" if clash
             else f"did not fire — all {len(S)} names unique within their folder"))

    # fb612 — the file on disk carries the SHIPPING name ("Terra - Bit Ladder"), not the
    # generator's identifier ("TERRA BIT LADDER"). Two tables genuinely share an identifier across
    # categories (SHATTER in Chaos/Vocal, SIERPINSKI in Chaos/Spectral — different sounds, verified
    # max|diff| 1.42 and 1.94), and in a shipping library a duplicate name is unpickable, so the
    # non-primary one carries its category. The rule lives in wtlib.shipping_name so this and
    # mkbank.py cannot drift.
    SHIP_PRIMARY = {"TERRA SHATTER": "Chaos", "TERRA SIERPINSKI": "Chaos"}
    _ident = {}
    for e in S: _ident.setdefault(e['name'], []).append(e['cat'])
    _dupes = {n for n, c in _ident.items() if len(c) > 1}
    bycat = {}
    for e in S:
        d = os.path.join(OUT, e['cat']); os.makedirs(d, exist_ok=True)
        ship = wtlib.shipping_name(e['name'])
        if e['name'] in _dupes and SHIP_PRIMARY.get(e['name']) != e['cat']: ship += " " + e['cat']
        e['ship'] = ship
        wtlib.write_wav(os.path.join(d, ship + ".wav"), e['frames'])
        bycat.setdefault(e['cat'], []).append(e)

    # fb606 — NO EMPTY CATEGORIES. "delete anything that doesn't have a table inside of it."
    empty = [c for c in QUOTA if c not in bycat]
    print("   empty-category check: "
          + (f"FIRED — DROPPED {', '.join(empty)} (zero tables)" if empty
             else f"did not fire — all {len(QUOTA)} categories have content"))
    for c in empty:
        p = os.path.join(OUT, c)
        if os.path.isdir(p):
            for f in glob.glob(os.path.join(p, "*.wav")): os.remove(f)
            try: os.rmdir(p)
            except OSError: pass

    print(f"{'category':<14} {'tables':>6} {'quota':>6} {'mean harm60':>12} {'median span':>12}")
    print("-" * 54)
    for c in sorted(bycat):
        v = bycat[c]
        print(f"{c:<14} {len(v):>6} {QUOTA.get(c, 0):>6} "
              f"{int(np.mean([x['harm60'] for x in v])):>12} "
              f"{np.median([x['span'] for x in v]):>11.1f}")
    print("-" * 54)
    print(f"{'TOTAL':<14} {m:>6} {sum(QUOTA.values()):>6} "
          f"{int(np.mean([x['harm60'] for x in S])):>12} "
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
