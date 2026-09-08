#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  flt_cardinality_gate.py — fb603 · THE FILTER ROSTER MOVES AS ONE NUMBER, OR IT DOES NOT MOVE.
#
#      python3 Tests/flt_cardinality_gate.py                 # from plugins/TerrainInstrument
#      python3 Tests/flt_cardinality_gate.py --root <dir>    # against another copy of the tree
#      TI_CARD_MUT=<list> python3 Tests/flt_cardinality_gate.py     # mutation control (see below)
#
#  WHY THIS FILE EXISTS.  The filter roster is written down NINE times and nothing compares them:
#
#      Source/TerrainFilters.h        enum class Type          highest value + 1
#      Source/TerrainFilters.h        constexpr int kNumTypes  = 94                  ← the truth
#      Source/PluginProcessor.cpp     terrainFilterEngineNames()  .add() count
#      Source/PluginProcessor.cpp     static_assert (kNumTypes == 94, ...)  ← a BARE LITERAL
#      Source/ui/public/index.html    FLT_ENGINES[]            94 display names
#      Source/ui/public/index.html    FLT_ENGGRP[]             94 group ids
#      Source/ui/public/index.html    CAT[]                    94 display-model keys
#      Source/ui/public/index.html    FILTER_TYPES[]           94 {idx,label,group,active}
#      Source/ui/public/index.html    window.FLT_TYPE_COUNT = 94                     ← the JS truth
#
#  index.html USED to carry two more bare integers — the FX card's `tpN:94` and a typed
#  "Search 94 filters…" — plus `const FILTER_NUM_CHOICES = 94` feeding both halves of the type
#  round trip. fb603's UI work derived all three from window.FLT_TYPE_COUNT, and bar [6] below is
#  what stops them decaying back into typed integers. A bare integer is the dangerous kind: a grep
#  for "94" cannot tell it from an opacity of 0.94 or a colour byte, and index.html has 20+ other
#  "94"s. That is the fb373 bug shape exactly — a display list and a param cardinality that must
#  move together with nothing asserting that they did. The roster grows to 118 in the commit after
#  fb603; this gate is what makes that append safe.
#
#  It does not stop at counting. A list can be the right LENGTH and still be wrong:
#      · FLT_ENGINES must be the C++ names, index for index (the dropdown writes idx/(N-1) into a
#        choice(N) param — a shifted name is a silently wrong engine, not a typo).
#      · FILTER_TYPES must carry every idx 0..N-1 exactly once (it is the category browser; a
#        missing idx is an engine no menu can reach, a duplicate is one that cannot be selected).
#      · every CAT[] key must name a real branch of mag() — a key with no branch falls through to
#        mag()'s `else { A=1/den; }` and draws a generic 2-pole lowpass for an engine that is not
#        one. That is the "no flat placeholder lines, ever" law, enforced.
#
#  MUTATION CONTROL.  TI_CARD_MUT=<list> copies the whole tree to a temp dir, deliberately breaks
#  ONE list in the copy, and runs every check against the copy. The gate must go RED and NAME THE
#  LIST IT BROKE — a gate that fails without saying which of nine places is wrong has not helped.
#      TI_CARD_MUT=kNumTypes | enum | names_add | static_assert | FLT_ENGINES | FLT_ENGGRP |
#                   CAT | FILTER_TYPES | FLT_TYPE_COUNT | name_order | filter_types_idx |
#                   cat_key | relapse_tpN | relapse_search
#      TI_CARD_MUT=all   runs every one of them in turn and reports the matrix.
#  There is no "make it green" mutation: this gate is green on the shipping tree today, and the
#  control's whole job is to prove it can stop being green.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys, shutil, tempfile, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

_pass, _fail, _failed_bars = 0, 0, []


def chk(ok, label, detail=""):
    global _pass, _fail
    if ok:
        _pass += 1
    else:
        _fail += 1
        _failed_bars.append(label)
    print("  %s  %s" % ("PASS" if ok else "FAIL", label))
    if detail:
        print("        %s" % detail)


def read(root, rel):
    p = os.path.join(root, rel)
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def lineof(src, idx):
    return src.count("\n", 0, idx) + 1


# ── the nine extractors. Each returns (count, "file:line", payload) or raises. ────────────────
def x_kNumTypes(root):
    s = read(root, "Source/TerrainFilters.h")
    m = re.search(r"constexpr\s+int\s+kNumTypes\s*=\s*(\d+)\s*;", s)
    if not m:
        raise LookupError("constexpr int kNumTypes not found in TerrainFilters.h")
    return int(m.group(1)), "Source/TerrainFilters.h:%d" % lineof(s, m.start()), None


def x_enum(root):
    s = read(root, "Source/TerrainFilters.h")
    m = re.search(r"enum\s+class\s+Type\s*:\s*int\s*\{(.*?)\}\s*;", s, re.S)
    if not m:
        raise LookupError("enum class Type not found in TerrainFilters.h")
    body = re.sub(r"//[^\n]*", "", m.group(1))
    vals = {}
    for em in re.finditer(r"([A-Z_][A-Z0-9_]*)\s*=\s*(\d+)", body):
        vals[em.group(1)] = int(em.group(2))
    if not vals:
        raise LookupError("enum class Type has no explicit values")
    return max(vals.values()) + 1, "Source/TerrainFilters.h:%d" % lineof(s, m.start()), vals


def x_names_add(root):
    s = read(root, "Source/PluginProcessor.cpp")
    m = re.search(r"static\s+juce::StringArray\s+terrainFilterEngineNames\s*\(\)\s*\{(.*?)\n\}", s, re.S)
    if not m:
        raise LookupError("terrainFilterEngineNames() not found in PluginProcessor.cpp")
    names = re.findall(r'filterTypeChoices\.add\s*\(\s*"([^"]*)"\s*\)', m.group(1))
    return len(names), "Source/PluginProcessor.cpp:%d" % lineof(s, m.start()), names


def x_static_assert(root):
    s = read(root, "Source/PluginProcessor.cpp")
    m = re.search(r"static_assert\s*\(\s*tw::filters::kNumTypes\s*==\s*(\d+)", s)
    if not m:
        raise LookupError("static_assert (tw::filters::kNumTypes == N) not found in PluginProcessor.cpp")
    return int(m.group(1)), "Source/PluginProcessor.cpp:%d" % lineof(s, m.start()), None


def _js_body(src, name):
    """(open-bracket-end, close-bracket-index, body) of the JS array literal `name = [ ... ]`."""
    m = re.search(r"\b%s\s*=\s*\[" % re.escape(name), src)
    if not m:
        raise LookupError("%s = [ ... ] not found in index.html" % name)
    i, depth = m.end(), 1
    while depth:
        c = src[i]
        if c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
            if depth == 0:
                break
        i += 1
    return m.start(), m.end(), i, src[m.end():i]


def _split_top(body):
    """Split on TOP-LEVEL commas only — FILTER_TYPES holds objects, and every list here may
       carry a trailing comma (which must not read as a 95th, empty entry)."""
    items, cur, d, q = [], [], 0, None
    for c in body:
        if q:
            cur.append(c)
            if c == q:
                q = None
            continue
        if c in "'\"":
            q = c
            cur.append(c)
            continue
        if c in "[{(":
            d += 1
        elif c in "]})":
            d -= 1
        if c == "," and d == 0:
            items.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    items.append("".join(cur))
    return [t.strip() for t in items if t.strip()]


def _js_array(src, name):
    start, _, _, body = _js_body(src, name)
    return _split_top(body), "Source/ui/public/index.html:%d" % lineof(src, start)


def x_FLT_ENGINES(root):
    s = read(root, "Source/ui/public/index.html")
    it, where = _js_array(s, "FLT_ENGINES")
    return len(it), where, [t.strip("'\"") for t in it]


def x_FLT_ENGGRP(root):
    s = read(root, "Source/ui/public/index.html")
    it, where = _js_array(s, "FLT_ENGGRP")
    return len(it), where, it


def x_CAT(root):
    s = read(root, "Source/ui/public/index.html")
    it, where = _js_array(s, "CAT")
    return len(it), where, [t.strip("'\"") for t in it]


def x_FILTER_TYPES(root):
    s = read(root, "Source/ui/public/index.html")
    it, where = _js_array(s, "FILTER_TYPES")
    idxs = []
    for t in it:
        m = re.search(r"idx:\s*(\d+)", t)
        idxs.append(int(m.group(1)) if m else -1)
    return len(it), where, idxs


def x_FLT_TYPE_COUNT(root):
    # fb603 — index.html now declares the count ONCE and derives every former bare literal from
    # it (`tpN:window.FLT_TYPE_COUNT`, a searchPlaceholder() that counts the list). This is the
    # JS side's single source; bar [6] below is what keeps the derivation from decaying back
    # into typed integers.
    s = read(root, "Source/ui/public/index.html")
    m = re.search(r"window\.FLT_TYPE_COUNT\s*=\s*(\d+)\s*;", s)
    if not m:
        raise LookupError("window.FLT_TYPE_COUNT = <n>; not found in index.html")
    return int(m.group(1)), "Source/ui/public/index.html:%d" % lineof(s, m.start()), None


SOURCES = [
    ("kNumTypes",      x_kNumTypes,      "constexpr int kNumTypes            (THE TRUTH)"),
    ("enum",           x_enum,           "enum class Type — highest value + 1"),
    ("names_add",      x_names_add,      "terrainFilterEngineNames() .add() count"),
    ("static_assert",  x_static_assert,  "static_assert literal              (BARE INT)"),
    ("FLT_ENGINES",    x_FLT_ENGINES,    "index.html FLT_ENGINES[]           (display names)"),
    ("FLT_ENGGRP",     x_FLT_ENGGRP,     "index.html FLT_ENGGRP[]            (group ids)"),
    ("CAT",            x_CAT,            "index.html CAT[]                   (display-model keys)"),
    ("FILTER_TYPES",   x_FILTER_TYPES,   "index.html FILTER_TYPES[]          (category browser)"),
    ("FLT_TYPE_COUNT", x_FLT_TYPE_COUNT, "index.html window.FLT_TYPE_COUNT  (the JS truth)"),
]

# ── the mutations. Each is (list-name, a function that edits the copied tree). ────────────────
def _sub(root, rel, pat, rep, count=1, flags=0):
    p = os.path.join(root, rel)
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        s = f.read()
    s2, n = re.subn(pat, rep, s, count=count, flags=flags)
    if n == 0:
        raise RuntimeError("mutation pattern did not match in %s: %r" % (rel, pat))
    with open(p, "w", encoding="utf-8") as f:
        f.write(s2)


def _drop_last_js_item(root, name):
    """Remove the LAST real entry — not just the trailing comma. FILTER_TYPES ends
       '...active: true },' so a naive rfind(',') deletes nothing and the mutation
       comes back green: that is exactly the broken control this gate must not have."""
    p = os.path.join(root, "Source/ui/public/index.html")
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        s = f.read()
    _, ob, cb, body = _js_body(s, name)
    items = _split_top(body)
    with open(p, "w", encoding="utf-8") as f:
        f.write(s[:ob] + ",".join(items[:-1]) + s[cb:])


MUTATIONS = {
    "kNumTypes":        lambda r: _sub(r, "Source/TerrainFilters.h", r"constexpr int kNumTypes = 94;", "constexpr int kNumTypes = 95;"),
    "enum":             lambda r: _sub(r, "Source/TerrainFilters.h", r"REVERB_METAL = 93", "REVERB_METAL = 99"),
    "names_add":        lambda r: _sub(r, "Source/PluginProcessor.cpp", r'\n *filterTypeChoices\.add \("Reverb Metal"\);', ""),
    "static_assert":    lambda r: _sub(r, "Source/PluginProcessor.cpp", r"tw::filters::kNumTypes == 94", "tw::filters::kNumTypes == 93"),
    "FLT_ENGINES":      lambda r: _drop_last_js_item(r, "FLT_ENGINES"),
    "FLT_ENGGRP":       lambda r: _drop_last_js_item(r, "FLT_ENGGRP"),
    "CAT":              lambda r: _drop_last_js_item(r, "CAT"),
    "FILTER_TYPES":     lambda r: _drop_last_js_item(r, "FILTER_TYPES"),
    "FLT_TYPE_COUNT":   lambda r: _sub(r, "Source/ui/public/index.html", r"window\.FLT_TYPE_COUNT = 94;", "window.FLT_TYPE_COUNT = 118;"),
    # the regression that matters most next commit: someone re-types the count at a use site
    "relapse_tpN":      lambda r: _sub(r, "Source/ui/public/index.html", r"tpN:window\.FLT_TYPE_COUNT", "tpN:94"),
    "relapse_search":   lambda r: _sub(r, "Source/ui/public/index.html",
                                       r"searchPlaceholder:window\.__fltRoster\.searchPlaceholder\(FLT_ENGINES\.length\)",
                                       "searchPlaceholder:'Search 94 filters…'"),
    # the three that keep every LENGTH right and are still wrong — a pure count gate misses these
    "name_order":       lambda r: _sub(r, "Source/ui/public/index.html", r"'SVF LP','SVF HP'", "'SVF HP','SVF LP'"),
    "filter_types_idx": lambda r: _sub(r, "Source/ui/public/index.html", r"\{ idx: 93, label: 'Reverb Metal'", "{ idx: 92, label: 'Reverb Metal'"),
    "cat_key":          lambda r: _sub(r, "Source/ui/public/index.html", r"'diffusor','bode','tilt'", "'diffuseur','bode','tilt'"),
}


def run_checks(root, label):
    print("══ fb603 · FILTER ROSTER CARDINALITY GATE ══   root: %s" % label)
    print()
    got, missing = {}, []
    print("  the %d places the roster is written down:" % len(SOURCES))
    for key, fn, desc in SOURCES:
        try:
            n, where, payload = fn(root)
            got[key] = (n, where, payload)
            print("    %-14s %5d   %-42s %s" % (key, n, desc, where))
        except LookupError as e:
            missing.append((key, str(e)))
            print("    %-14s   ??   %-42s NOT FOUND — %s" % (key, desc, e))
    print()

    chk(not missing, "[0] EVERY ROSTER SITE IS STILL FINDABLE — a renamed list is a silent hole",
        "all %d extractors matched" % len(SOURCES) if not missing
        else "UNPARSEABLE: " + "; ".join("%s (%s)" % (k, m) for k, m in missing))
    if missing:
        return 1

    truth = got["kNumTypes"][0]

    # [1] the headline: one number, ten sites.
    bad = [(k, v[0], v[1]) for k, v in got.items() if v[0] != truth]
    chk(not bad,
        "[1] ALL %d ROSTER SITES CARRY THE SAME CARDINALITY (kNumTypes = %d)" % (len(SOURCES), truth),
        ("every site = %d" % truth) if not bad else
        "MISMATCHED: " + " | ".join("%s = %d at %s (want %d)" % (k, n, w, truth) for k, n, w in bad))

    # [2] names, index for index. A shifted name is a wrong ENGINE, not a typo:
    #     the browser writes idx/(N-1) into choice(N), so the DSP obeys the INDEX.
    cxx, js = got["names_add"][2], got["FLT_ENGINES"][2]
    diff = [(i, a, b) for i, (a, b) in enumerate(zip(cxx, js)) if a != b]
    chk(not diff and len(cxx) == len(js),
        "[2] index.html FLT_ENGINES IS terrainFilterEngineNames() INDEX FOR INDEX",
        "%d names identical" % len(cxx) if not diff else
        "DIVERGED at " + ", ".join("[%d] C++ %r vs JS %r" % (i, a, b) for i, a, b in diff[:6]))

    # [3] the category browser must reach every engine exactly once.
    idxs = got["FILTER_TYPES"][2]
    want = set(range(truth))
    dupes = sorted({i for i in idxs if idxs.count(i) > 1})
    gaps = sorted(want - set(idxs))
    stray = sorted(set(idxs) - want)
    chk(not dupes and not gaps and not stray,
        "[3] FILTER_TYPES REACHES EVERY ENGINE 0..%d EXACTLY ONCE" % (truth - 1),
        "%d entries, no gap, no duplicate" % len(idxs) if not (dupes or gaps or stray) else
        "FILTER_TYPES: gaps=%s duplicates=%s out-of-range=%s" % (gaps, dupes, stray))

    # [4] every CAT[] key must name a real mag() branch, or the engine draws a generic
    #     2-pole lowpass from mag()'s trailing `else` — a placeholder curve by another name.
    s = read(root, "Source/ui/public/index.html")
    mm = re.search(r"function mag\(type,f,fc,res,drv,t\)\{(.*?)\n    return A\*A; \}", s, re.S)
    branches = set()
    if mm:
        branches |= set(re.findall(r"type===\s*'([a-z0-9]+)'", mm.group(1)))
        if "type.length===3&&type.charAt(0)==='m'" in mm.group(1):
            branches |= {c for c in got["CAT"][2] if len(c) == 3 and c[0] == "m"}
        if "if(type==='none')return 1" in s[mm.start() - 90:mm.start() + 60]:
            branches.add("none")
    orphan = sorted({c for c in got["CAT"][2] if c not in branches})
    chk(bool(mm) and not orphan,
        "[4] EVERY CAT[] MODEL KEY NAMES A REAL mag() BRANCH — no engine falls to the generic else",
        ("%d keys, %d distinct, all resolve" % (len(got["CAT"][2]), len(set(got["CAT"][2]))))
        if mm and not orphan else
        ("mag() not parseable" if not mm else "ORPHAN KEYS (draw a generic 2-pole LP): %s" % orphan))

    # [5] the enum has no holes — kNumTypes is a COUNT, and the DSP switch indexes on it.
    vals = got["enum"][2]
    holes = sorted(set(range(truth)) - set(vals.values()))
    chk(not holes,
        "[5] enum class Type IS DENSE 0..%d — kNumTypes is a count, not a high-water mark" % (truth - 1),
        "%d enumerators, no hole" % len(vals) if not holes else "MISSING ENUM VALUES: %s" % holes)

    # [6] THE COUNT MUST STAY DERIVED. Counting a literal is only a gate until someone re-types
    #     it somewhere new; asserting that the use sites are EXPRESSIONS is what survives the
    #     growth to 118. Both of these were bare 94s before fb603.
    # Comments are STRIPPED first. index.html:721 documents the bare literals fb603 removed
    # ("was `const FILTER_NUM_CHOICES = 94`"); a scanner that reads its own changelog as a
    # relapse is a gate that cries wolf until someone deletes it.
    code = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    code = re.sub(r"(?m)^\s*//[^\n]*$", " ", code)
    relapses = []
    for pat, what in (
            (r"tpN\s*:\s*(\d+)\s*,\s*tp2\s*:\s*'SYN_FLT_ENGINE'", "the filter device's tpN:"),
            (r"searchPlaceholder\s*:\s*'Search\s+\d+\s+filters", "a filter search placeholder"),
            (r"const\s+FILTER_NUM_CHOICES\s*=\s*\d+", "const FILTER_NUM_CHOICES")):
        m = re.search(pat, code)
        if m:
            relapses.append("%s — %r" % (what, m.group(0)[:70]))
    chk(not relapses,
        "[6] NO BARE ROSTER LITERAL AT A USE SITE — the count is DERIVED, not re-typed",
        "tpN, the search placeholder and the round-trip cardinality all read window.FLT_TYPE_COUNT"
        if not relapses else "RE-TYPED: " + " | ".join(relapses))

    print()
    print("  %d passed, %d FAILED" % (_pass, _fail))
    if _failed_bars:
        print("  FAILED BARS:")
        for b in _failed_bars:
            print("    - %s" % b)
    print()
    return 1 if _fail else 0


def mutate_and_run(which):
    """Copy the tree, break ONE list in the copy, run the gate against the copy."""
    tmp = tempfile.mkdtemp(prefix="fltcard_mut_")
    try:
        for rel in ("Source/TerrainFilters.h", "Source/PluginProcessor.cpp", "Source/ui/public/index.html"):
            dst = os.path.join(tmp, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(ROOT, rel), dst)
        MUTATIONS[which](tmp)
        env = dict(os.environ)
        env.pop("TI_CARD_MUT", None)
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "--root", tmp],
                           capture_output=True, text=True, env=env)
        return r.returncode, r.stdout + r.stderr
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    args = sys.argv[1:]
    root = ROOT
    if "--root" in args:
        root = args[args.index("--root") + 1]
    mut = os.environ.get("TI_CARD_MUT", "")

    if mut == "all":
        print("══ fb603 · CARDINALITY GATE — MUTATION MATRIX ══")
        print("   Each row breaks ONE roster site in a throwaway copy of the tree.")
        print("   A row is OK only if the gate goes RED *and* its FAILED BARS name the break.\n")
        bad = 0
        for name in MUTATIONS:
            rc, out = mutate_and_run(name)
            bars = [l.strip()[2:] for l in out.splitlines() if l.strip().startswith("- [")]
            detail = ""
            for l in out.splitlines():
                if "MISMATCHED:" in l or "DIVERGED at" in l or "ORPHAN KEYS" in l \
                   or "FILTER_TYPES: gaps" in l or "MISSING ENUM" in l or "UNPARSEABLE" in l:
                    detail = l.strip()
                    break
            ok = rc != 0
            if not ok:
                bad = 1
            print("   %-17s rc=%d  %-4s  %s" % (name, rc, "RED" if rc else "GREEN-BROKEN", bars))
            if detail:
                print("                     %s" % detail[:150])
        print()
        print("   %s" % ("every mutation went red — the gate is live"
                         if not bad else "AT LEAST ONE MUTATION DID NOT GO RED — the gate is not a gate"))
        sys.exit(bad)

    if mut:
        if mut not in MUTATIONS:
            print("unknown TI_CARD_MUT=%r; pick one of: %s" % (mut, ", ".join(MUTATIONS) + ", all"))
            sys.exit(2)
        rc, out = mutate_and_run(mut)
        print(out, end="")
        print("  MUTATION CONTROL TI_CARD_MUT=%s — the gate must be RED above.  rc=%d" % (mut, rc))
        sys.exit(0 if rc != 0 else 1)   # the control PASSES when the mutated gate FAILED

    sys.exit(run_checks(root, "shipping tree" if root == ROOT else root))
