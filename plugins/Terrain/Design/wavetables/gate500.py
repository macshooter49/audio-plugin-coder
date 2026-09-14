#!/usr/bin/env python3
"""
gate500.py — choose the 334 NEW factory tables.   500 = 46 built-ins + 120 fixed FLACs + 334 new.

    python3 gate500.py                        # every gen2_*.py beside this file
    python3 gate500.py mod_a.py mod_b.py      # explicit modules (anywhere, e.g. a demo)
    python3 gate500.py --report               # + each selected table's 3 nearest neighbours in the whole 500
    options: --jobs N  --timeout S  --fresh  --out DIR  --no-write

  1. calibrates the collision floor exactly as gate.py does — gate.calibrate() is imported, not copied
  2. loads the FIXED set (fixed_set.py: the 120 shipped FLACs + built-in dumps when present; rebuilt if stale)
  3. imports every module and validates the contract. HARD errors (exit 1): an import failure, a table
     with no CATEGORY or one that is not one of the 13, a "TERRA " prefix, lower case, an identifier
     defined twice (inside a module or across modules)
  4. renders every candidate with wtkit's renderer (parallel, per-table timeout, cached by module mtime)
     and drops the ones that fail wtkit's bars (shape/finite/peak/dc/dead/seam/static)
  5. PER CATEGORY, to the quotas in wtkit.QUOTA_NEW, ranks candidates by quality score and accepts
     greedily. Categories take turns (one acceptance each per round) so no category claims the
     spectral space first. A candidate is accepted only if it is >= the floor from EVERY fixed table
     and EVERY table already accepted
  6. names = wtlib.shipping_name(IDENT); FAIL if an accepted name collides, case-insensitively, with
     another accepted name, a fixed table's plain name or a built-in name
  7. writes <wt500>/bank_new/<Category>/<Name>.wav + MANIFEST.csv, prints filled/quota per category
  Exit 0 only when exactly 334 are selected and nothing above failed.

THE SCORE is gate.py's — 20*log10(harm60) + min(span, 80) — used only to ORDER candidates inside
their own category. Basic Shapes and Physical are structurally dark (a sine is one harmonic; modal
sources roll off), so in those two the brightness term stops paying at DARK_CAP harmonics: a dark
table there competes on travel, never on brightness. A global brightness ranking once deleted all
twelve Physical tables (README, "load-bearing" 2); quotas plus this cap keep that from coming back.
"""
import sys, os, glob, csv, math, time, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import numpy as np
import wtlib
import wtkit
import fixed_set
import gate
from wtkit import QUOTA_NEW, CATEGORIES, DARK_OK

TARGET = 334
DARK_CAP = 24
assert sum(QUOTA_NEW.values()) == TARGET


def score(r):
    h = max(int(r.get('harm60', 0)), 1)
    if r['category'] in DARK_OK:
        h = min(h, DARK_CAP)
    return 20.0 * math.log10(h) + min(float(r.get('span', 0.0)), 80.0)


def main():
    ap = argparse.ArgumentParser(prog="gate500.py")
    ap.add_argument("modules", nargs="*", help="generator modules (default: gen2_*.py beside this file)")
    ap.add_argument("--report", action="store_true", help="print 3 nearest neighbours in the whole 500 per selected table")
    ap.add_argument("--jobs", type=int, default=None)
    ap.add_argument("--timeout", type=float, default=wtkit.DEFAULT_TIMEOUT)
    ap.add_argument("--fresh", action="store_true", help="ignore the render cache")
    ap.add_argument("--out", default=None, help="bank dir (default <wt500>/bank_new)")
    ap.add_argument("--no-write", action="store_true")
    ap.add_argument("--exclude", default=None,
                    help="fb638 — a file of IDENTIFIERS (one per line, # comments) the critique vetoed; never selected")
    ap.add_argument("--prefer", default=None,
                    help="fb638 — a file of IDENTIFIERS the critique chose as swap-ins; each is tried FIRST in its category "
                         "(still subject to every bar and to --spacing)")
    ap.add_argument("--spacing", type=float, default=0.0,
                    help="fb638 — demand at least this many dB between a new table and EVERYTHING (fixed, references,\n                    other new tables); never below the calibrated floor. Max: \"I don't want one wave table to sound like any other.\"")
    a = ap.parse_args()
    t_all = time.time()
    out = a.out or os.path.join(wtkit.wt500_dir(), "bank_new")

    print("\n══ GATE 500 — the 334 new factory tables · build · measure · prove distinct · select ══\n")
    floor, ceil_ = gate.calibrate()
    print("CALIBRATION  saw vs gently re-tilted saw = %.2f dB -> the collision FLOOR (gate.calibrate())" % floor)
    print("             saw vs pure sine            = %.2f dB\n" % ceil_)

    fixed = fixed_set.load(log=print)
    FX = fixed['fps']
    nfix = len(fixed['names'])
    bn = fixed_set.builtin_names()
    print("fixed set: %s" % fixed['summary'])
    if "builtin" not in fixed['kinds']:
        print("   NOTE: no built-in dumps (%s) — distances are checked against the %d shipped FLACs only;"
              % (fixed_set.paths()['index'], nfix))
    print("built-in names: %d from %s\n" % (len(bn['names']), bn['source']))

    paths = [wtkit.resolve_module(p) for p in a.modules] or sorted(glob.glob(os.path.join(HERE, "gen2_*.py")))
    paths = list(dict.fromkeys(paths))              # the same file named twice is one module, not a duplicate
    hard, allrows = [], []
    if not paths:
        hard.append("no generator modules: no gen2_*.py beside gate500.py and none given")
    for p in paths:
        info = wtkit.module_info(p)
        for e in info['errors']:
            hard.append("%s: %s" % (info['module'], e))
        for w in info['warnings']:
            print("   warning %s: %s" % (info['module'], w))
        if info['fatal']:
            print("   %-26s FATAL — %s" % (info['module'], info['errors'][0]))
            continue
        R = wtkit.render_module(p, jobs=a.jobs, timeout=a.timeout, fresh=a.fresh, info=info, log=None)
        nb = sum(1 for r in R['rows'] if not r['ok'])
        print("   %-26s %3d tables  %6.1f s%s   bars failed: %d" % (info['module'], len(R['rows']), R['secs'],
              " (cached)" if R['cached'] else "          ", nb))
        for r in R['rows']:
            r['module'] = info['module']
            allrows.append(r)

    byid = {}
    for r in allrows:
        byid.setdefault(r['ident'], set()).add(r['module'])
    for k, v in sorted(byid.items()):
        if len(v) > 1:
            hard.append("identifier '%s' is defined in %d modules: %s" % (k, len(v), ", ".join(sorted(v))))

    barfail = [r for r in allrows if not r['ok']]
    cands = [r for r in allrows if r['ok'] and r.get('fp') is not None and r['category'] in QUOTA_NEW]
    if a.exclude:
        veto = {ln.split('#')[0].strip() for ln in open(a.exclude) if ln.split('#')[0].strip()}
        unknown = sorted(veto - {r['ident'] for r in allrows})
        if unknown: print('   NOTE: %d vetoed identifiers match no candidate: %s' % (len(unknown), unknown[:6]))
        n0 = len(cands); cands = [r for r in cands if r['ident'] not in veto]
        print('   critique veto: %d candidates excluded (%s)' % (n0 - len(cands), a.exclude))
    for r in cands:
        r['score'] = score(r)
        j, d = wtkit.nearest(r['fp'], FX)
        r['nnfix'] = (j, d)
    print("\n%d candidates rendered · %d pass the bars · %d fail them\n" % (len(allrows), len(cands), len(barfail)))

    # ── round-robin greedy selection ─────────────────────────────────────────────────────────
    pref = []
    if a.prefer:
        pref = [ln.split('#')[0].strip() for ln in open(a.prefer) if ln.split('#')[0].strip()]
        print('   critique preference: %d identifiers tried first (%s)' % (len(pref), a.prefer))
    prank = {k: i for i, k in enumerate(pref)}
    pools = {c: sorted([r for r in cands if r['category'] == c],
                       key=lambda r: (0 if r['ident'] in prank else 1, prank.get(r['ident'], 0), -r['score'], r['ident']))
             for c in CATEGORIES}
    ptr = {c: 0 for c in CATEGORIES}
    acc, accF, rejected = [], np.zeros((0, FX.shape[1] if len(FX) else 768)), []
    filled = {c: 0 for c in CATEGORIES}
    progress = True
    while progress:
        progress = False
        for c in CATEGORIES:
            if filled[c] >= QUOTA_NEW[c]:
                continue
            while ptr[c] < len(pools[c]):
                r = pools[c][ptr[c]]; ptr[c] += 1
                j, d = r['nnfix']
                if j is not None and d < max(floor, a.spacing):
                    r['fate'] = "%.2f dB from fixed %s '%s' (%s)" % (d, fixed['kinds'][j], fixed['names'][j], fixed['cats'][j])
                    rejected.append(r); continue
                if len(acc):
                    k, dk = wtkit.nearest(r['fp'], accF)
                    if dk < max(floor, a.spacing):
                        r['fate'] = "%.2f dB from accepted '%s' (%s, %s)" % (dk, acc[k]['name'], acc[k]['category'], acc[k]['module'])
                        rejected.append(r); continue
                acc.append(r); accF = np.vstack([accF, r['fp'][None, :]])
                filled[c] += 1; progress = True
                break

    # ── names ────────────────────────────────────────────────────────────────────────────────
    shipped_cf = {}
    for nm, k, c in zip(fixed['names'], fixed['kinds'], fixed['cats']):
        if k == "shipped":
            shipped_cf.setdefault(nm.casefold(), (nm, c))
    b_cf = {}
    for nm in bn['names']:
        b_cf.setdefault(nm.casefold(), nm)
    name_fail, seen = [], {}
    for r in acc:
        key = r['name'].casefold()
        tag = "'%s' (%s/%s, %s)" % (r['name'], r['category'], r['ident'], r['module'])
        if key in seen:
            name_fail.append("%s duplicates accepted '%s' (%s/%s, %s)" % (tag, seen[key]['name'], seen[key]['category'],
                             seen[key]['ident'], seen[key]['module']))
        else:
            seen[key] = r
        if key in shipped_cf:
            name_fail.append("%s collides with fixed table '%s' (%s)" % (tag, shipped_cf[key][0], shipped_cf[key][1]))
        if key in b_cf:
            name_fail.append("%s collides with built-in '%s'" % (tag, b_cf[key]))
    pre = sorted({shipped_cf[k][0] + " (" + shipped_cf[k][1] + ") = built-in '" + b_cf[k] + "'" for k in shipped_cf if k in b_cf})

    # ── whole-set neighbours ─────────────────────────────────────────────────────────────────
    W = np.vstack([FX, accF]) if len(acc) else FX
    labels = [(fixed['names'][i], fixed['kinds'][i], fixed['cats'][i]) for i in range(nfix)] + \
             [(r['name'], "new", r['category']) for r in acc]
    for i, r in enumerate(acc):
        d = np.abs(W - r['fp']).mean(axis=1)
        d[nfix + i] = np.inf
        order = np.argsort(d)[:3]
        r['nn3'] = [(labels[o][0], labels[o][1], labels[o][2], float(d[o])) for o in order]

    # ── write ────────────────────────────────────────────────────────────────────────────────
    if not a.no_write:
        os.makedirs(out, exist_ok=True)
        for dn in os.listdir(out):
            p = os.path.join(out, dn)
            if os.path.isdir(p):
                for f in glob.glob(os.path.join(p, "*.wav")):
                    os.remove(f)
                try:
                    os.rmdir(p)
                except OSError:
                    print("   note: %s holds non-wav files — left in place" % p)
        for r in acc:
            dd = os.path.join(out, r['category'])
            os.makedirs(dd, exist_ok=True)
            wtlib.write_wav(os.path.join(dd, r['name'] + ".wav"), np.load(r['npy_path']))
        with open(os.path.join(out, "MANIFEST.csv"), "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["category", "name", "identifier", "module", "harm60", "span_st", "seam", "score",
                        "nn_name", "nn_set", "nn_category", "nn_distance_db"])
            for r in sorted(acc, key=lambda r: (CATEGORIES.index(r['category']), r['name'])):
                nn = r['nn3'][0] if r['nn3'] else ("", "", "", float("nan"))
                w.writerow([r['category'], r['name'], r['ident'], r['module'], r['harm60'], round(r['span'], 2),
                            round(r['seam'], 3), round(r['score'], 2), nn[0], nn[1], nn[2], round(nn[3], 2)])

    # ── report ───────────────────────────────────────────────────────────────────────────────
    print("%-14s %13s %6s %7s %9s %7s %9s %9s" % ("category", "filled/quota", "pool", "bars✗", "collided", "short",
                                                "mean h60", "med span"))
    print("-" * 82)
    short = {}
    for c in CATEGORIES:
        mine = [r for r in acc if r['category'] == c]
        pool = [r for r in allrows if r['category'] == c]
        nbf = sum(1 for r in pool if not r['ok'])
        ncol = sum(1 for r in rejected if r['category'] == c)
        s = QUOTA_NEW[c] - len(mine)
        if s:
            short[c] = s
        print("%-14s %6d/%-6d %6d %7d %9d %7s %9s %9s" % (c, len(mine), QUOTA_NEW[c], len(pool), nbf, ncol,
              ("SHORT %d" % s) if s else "-", int(np.mean([r['harm60'] for r in mine])) if mine else "-",
              ("%.1f" % np.median([r['span'] for r in mine])) if mine else "-"))
    print("-" * 82)
    print("%-14s %6d/%-6d %6d %7d %9d %7s" % ("TOTAL", len(acc), TARGET, len(allrows), len(barfail), len(rejected),
                                          ("SHORT %d" % (TARGET - len(acc))) if len(acc) != TARGET else "-"))

    if barfail:
        print("\n══ FAILED A BAR (%d) — not candidates ══" % len(barfail))
        for r in barfail:
            print("   %-12s %-30s %-16s %s" % ((r['category'] or "?")[:12], r['ident'][:30], r['module'][:16], r['errors'][0]))
    if rejected:
        print("\n══ COLLIDED (%d) — within the %.2f dB floor ══" % (len(rejected), floor))
        for r in rejected:
            print("   %-12s %-30s %-16s %s" % (r['category'], r['ident'][:30], r['module'][:16], r['fate']))
    done = {id(r) for r in acc} | {id(r) for r in rejected}      # identity: rows hold numpy arrays
    over = [r for r in cands if id(r) not in done]
    if over:
        print("\n   %d candidates were clean but over their category's quota (lower score)" % len(over))

    if acc:
        mins = sorted(((r['nn3'][0][3], r['name'], r['nn3'][0][0]) for r in acc if r['nn3']))
        if mins:
            print("\n══ UNIQUENESS ══  closest selected table to anything in the library: %.2f dB  (%s vs %s)" % mins[0])
            print("   floor %.2f dB · 'close' band <= %.2f dB: %d selected tables" % (floor, 1.5 * floor,
                  sum(1 for m in mins if m[0] <= 1.5 * floor)))
    if pre:
        print("\n   NOTICE — pre-existing name clashes between the fixed FLACs and the built-ins once 'Terra' is dropped"
              " (not new tables; for the rename to resolve): %s" % "; ".join(pre))
    if a.report and acc:
        print("\n══ REPORT — 3 nearest neighbours across the whole library (%d fixed + %d new) ══" % (nfix, len(acc)))
        for r in sorted(acc, key=lambda r: (CATEGORIES.index(r['category']), r['name'])):
            print("   %-12s %-26s %s" % (r['category'], r['name'][:26], "   ".join("%s [%s %s] %.2f" % (n, k, c, d)
                  for n, k, c, d in r['nn3'])))

    if not a.no_write:
        print("\n   wrote %d wav + MANIFEST.csv to %s%s" % (len(acc), out, "" if len(acc) == TARGET else "  (PARTIAL)"))
    fail = []
    if hard:
        fail.append("%d hard error(s)" % len(hard))
        print("\n══ HARD ERRORS (%d) ══" % len(hard))
        for e in hard:
            print("   ✗ " + e)
    if name_fail:
        fail.append("%d name collision(s)" % len(name_fail))
        print("\n══ NAME COLLISIONS (%d) ══" % len(name_fail))
        for e in name_fail:
            print("   ✗ " + e)
    if short:
        fail.append("shortfall in %d categories (%d tables): %s" % (len(short), sum(short.values()),
                    ", ".join("%s -%d" % (c, s) for c, s in short.items())))
    if len(acc) != TARGET:
        fail.append("%d selected, not %d" % (len(acc), TARGET))
    print("\n   VERDICT: %s   [%.1f s]\n" % ("PASS — exactly %d new tables, all distinct, all names free" % TARGET
                                           if not fail else "FAIL — " + " · ".join(fail), time.time() - t_all))
    return 0 if not fail else 1


if __name__ == "__main__":
    sys.exit(main())
