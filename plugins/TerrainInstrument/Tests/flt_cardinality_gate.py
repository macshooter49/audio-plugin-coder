#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  flt_cardinality_gate.py — fb604 · THE FILTER ROSTER MOVES AS ONE NUMBER, OR IT DOES NOT MOVE.
#
#      python3 Tests/flt_cardinality_gate.py                 # from plugins/TerrainInstrument
#      python3 Tests/flt_cardinality_gate.py --root <dir>    # against another copy of the tree
#      TI_CARD_MUT=<list> python3 Tests/flt_cardinality_gate.py     # mutation control (see below)
#
#  WHY THIS FILE EXISTS.  The filter roster is written down TEN times and nothing compares them:
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
#      Tests/flt_measure.h            kName[]                  the HARNESS's labels  ← fb604
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
#                   CAT | FILTER_TYPES | FLT_TYPE_COUNT | kName | name_order |
#                   filter_types_idx | cat_key | relapse_tpN | relapse_search
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


def _strip_js(body):
    """Blank out // and /* */ comments, leaving every other byte at its own offset.

       fb604 — THE PARSER USED TO COUNT COMMENTS AS ENTRIES. The append documents itself inside
       these lists ("/* fb604 — THE COMB DAMPING MATRIX ... */"), and a block comment carrying a
       top-level comma split into TWO phantom items with no `idx:`, which surfaced as
       `duplicates=[-1]` and a length nobody could reconcile with the file. A roster gate that
       miscounts the roster because someone explained the roster is not a gate. Offsets are
       preserved (comments become spaces) so every index into the body stays valid."""
    out, i, n = list(body), 0, len(body)
    while i < n:
        c = body[i]
        if c in "'\"`":
            q = c
            i += 1
            while i < n and body[i] != q:
                i += 2 if body[i] == "\\" else 1
            i += 1
        elif c == "/" and i + 1 < n and body[i + 1] == "/":
            while i < n and body[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and body[i + 1] == "*":
            j = body.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        else:
            i += 1
    return "".join(out)


def _js_body(src, name):
    """(start, open-bracket-end, close-bracket-index, body) of the JS array literal
       `name = [ ... ]`. Brackets inside strings and comments do NOT count."""
    m = re.search(r"\b%s\s*=\s*\[" % re.escape(name), src)
    if not m:
        raise LookupError("%s = [ ... ] not found in index.html" % name)
    tail = _strip_js(src[m.end():])
    i, depth = 0, 1
    while depth:
        if i >= len(tail):
            raise LookupError("%s = [ ... ] is not closed in index.html" % name)
        c = tail[i]
        if c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
            if depth == 0:
                break
        i += 1
    return m.start(), m.end(), m.end() + i, _strip_js(src[m.end():m.end() + i])


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


def x_kName(root):
    # fb604 — THE TENTH SITE. Tests/flt_measure.h carries its own label table, and both C++
    # harnesses print every measured number under it. Nothing compared it to anything, so a
    # roster append that forgot it would have printed 24 rows of real measurements under the
    # wrong 24 names — a report that is worse than no report, in the file whose entire job is to
    # be the one that does not lie. Same failure shape as the nine below, one layer out.
    s = read(root, "Tests/flt_measure.h")
    m = re.search(r"static const char\* kName\s*\[\s*\]\s*=\s*\{", s)
    if not m:
        raise LookupError("static const char* kName[] = { ... } not found in Tests/flt_measure.h")
    i = s.index("};", m.end())
    body = re.sub(r"//[^\n]*", "", s[m.end():i])
    names = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    return len(names), "Tests/flt_measure.h:%d" % lineof(s, m.start()), names


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
    ("kName",          x_kName,          "Tests/flt_measure.h kName[]        (the HARNESS's labels)"),
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


def _last_js_item(root, name):
    """The text of the LAST entry of a JS array literal in index.html, and its span."""
    with open(os.path.join(root, "Source/ui/public/index.html"), "r", encoding="utf-8", errors="replace") as f:
        s = f.read()
    _, ob, cb, body = _js_body(s, name)
    items = _split_top(body)
    return s, ob, cb, items


def _rewrite_js_items(root, name, items):
    p = os.path.join(root, "Source/ui/public/index.html")
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        s = f.read()
    _, ob, cb, _ = _js_body(s, name)
    with open(p, "w", encoding="utf-8") as f:
        f.write(s[:ob] + ",".join(items) + s[cb:])


def _mut_kNumTypes(root):
    n = x_kNumTypes(root)[0]
    _sub(root, "Source/TerrainFilters.h",
         r"constexpr int kNumTypes\s*=\s*%d\s*;" % n, "constexpr int kNumTypes = %d;" % (n + 1))


def _mut_enum(root):
    """Push the HIGHEST enumerator one past the end, whatever it is called today."""
    s = read(root, "Source/TerrainFilters.h")
    vals = x_enum(root)[2]
    top = max(vals, key=lambda k: vals[k])
    _sub(root, "Source/TerrainFilters.h",
         r"\b%s\s*=\s*%d\b" % (top, vals[top]), "%s = %d" % (top, vals[top] + 6))


def _mut_names_add(root):
    """Delete the LAST .add() — the roster's newest entry, not a name typed in 2024."""
    names = x_names_add(root)[2]
    _sub(root, "Source/PluginProcessor.cpp",
         r'\n *filterTypeChoices\.add \("%s"\);' % re.escape(names[-1]), "", count=1)


def _mut_static_assert(root):
    n = x_static_assert(root)[0]
    _sub(root, "Source/PluginProcessor.cpp",
         r"tw::filters::kNumTypes == %d" % n, "tw::filters::kNumTypes == %d" % (n - 1))


def _mut_type_count(root):
    n = x_FLT_TYPE_COUNT(root)[0]
    _sub(root, "Source/ui/public/index.html",
         r"window\.FLT_TYPE_COUNT\s*=\s*%d\s*;" % n, "window.FLT_TYPE_COUNT = %d;" % (n + 1))


def _mut_name_order(root):
    """Swap two ADJACENT display names. A shifted name is a wrong ENGINE, not a typo — the
       dropdown writes idx/(N-1) into a choice(N) param, so the DSP obeys the INDEX."""
    s, ob, cb, items = _last_js_item(root, "FLT_ENGINES")
    i = min(5, len(items) - 2)
    items[i], items[i + 1] = items[i + 1], items[i]
    _rewrite_js_items(root, "FLT_ENGINES", items)


def _mut_filter_types_idx(root):
    """Point the LAST browser entry at the FIRST engine: same length, one engine unreachable
       and one selectable twice."""
    s, ob, cb, items = _last_js_item(root, "FILTER_TYPES")
    last = items[-1]
    m = re.search(r"idx:\s*(\d+)", last)
    if not m:
        raise RuntimeError("FILTER_TYPES last entry has no idx:")
    items[-1] = last[:m.start(1)] + "0" + last[m.end(1):]
    _rewrite_js_items(root, "FILTER_TYPES", items)


def _mut_cat_key(root):
    """Corrupt ONE display-model key so it names no mag() branch — the engine then draws the
       generic 2-pole lowpass from mag()'s trailing else, i.e. a placeholder curve."""
    s, ob, cb, items = _last_js_item(root, "CAT")
    for i, it in enumerate(items):
        k = it.strip().strip("'\"")
        if k and k != "none" and not (len(k) == 3 and k[0] == "m"):
            items[i] = "'%s_zz'" % k
            _rewrite_js_items(root, "CAT", items)
            return
    raise RuntimeError("no mutable CAT key found")


# fb604 — EVERY MUTATION IS NOW ROSTER-SIZE-AGNOSTIC. They were written as literals against 94
# ("kNumTypes = 94", "REVERB_METAL = 93", drop `.add("Reverb Metal")`, set FLT_TYPE_COUNT to 118).
# At 118 five of the fourteen would not have matched at all and one — FLT_TYPE_COUNT -> 118 —
# would have become a NO-OP that still printed GREEN-BROKEN's opposite: a mutation that changes
# nothing, on a gate whose whole job is to prove it can go red. The controls are the last thing
# anyone re-reads, so they are the first thing that has to survive the append.
MUTATIONS = {
    "kNumTypes":        _mut_kNumTypes,
    "enum":             _mut_enum,
    "names_add":        _mut_names_add,
    "static_assert":    _mut_static_assert,
    "FLT_ENGINES":      lambda r: _drop_last_js_item(r, "FLT_ENGINES"),
    "FLT_ENGGRP":       lambda r: _drop_last_js_item(r, "FLT_ENGGRP"),
    "CAT":              lambda r: _drop_last_js_item(r, "CAT"),
    "FILTER_TYPES":     lambda r: _drop_last_js_item(r, "FILTER_TYPES"),
    "FLT_TYPE_COUNT":   _mut_type_count,
    # the regression that matters most next commit: someone re-types the count at a use site
    "relapse_tpN":      lambda r: _sub(r, "Source/ui/public/index.html", r"tpN:window\.FLT_TYPE_COUNT", "tpN:94"),
    "relapse_search":   lambda r: _sub(r, "Source/ui/public/index.html",
                                       r"searchPlaceholder:window\.__fltRoster\.searchPlaceholder\(FLT_ENGINES\.length\)",
                                       "searchPlaceholder:'Search 94 filters…'"),
    # the three that keep every LENGTH right and are still wrong — a pure count gate misses these
    "name_order":       _mut_name_order,
    "filter_types_idx": _mut_filter_types_idx,
    "cat_key":          _mut_cat_key,
    # fb604 — the tenth site: the measurement harness's own label table. A kName[] that has
    # drifted from terrainFilterEngineNames() mis-attributes every number flt_gate prints, and
    # nothing else in the tree compares them.
    "kName":            lambda r: _drop_last_js_item_cxx(r),
}


def _drop_last_js_item_cxx(root):
    """Delete the LAST name from Tests/flt_measure.h's kName[] table."""
    p = os.path.join(root, "Tests/flt_measure.h")
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        s = f.read()
    ob = s.index("static const char* kName[] = {") + len("static const char* kName[] = {")
    cb = s.index("};", ob)
    body = s[ob:cb]
    k = body.rindex('"')
    k0 = body.rindex('"', 0, k)
    k0 = body.rindex(",", 0, k0)
    with open(p, "w", encoding="utf-8") as f:
        f.write(s[:ob] + body[:k0] + " " + s[cb:])


def run_checks(root, label):
    print("══ fb604 · FILTER ROSTER CARDINALITY GATE ══   root: %s" % label)
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
        body = mm.group(1)
        branches |= set(re.findall(r"type===\s*'([A-Za-z0-9]+)'", body))
        if "type.length===3&&type.charAt(0)==='m'" in body:
            branches |= {c for c in got["CAT"][2] if len(c) == 3 and c[0] == "m"}
        # fb604 — FAMILY BRANCHES. The 11 appended phasers are dispatched by PREFIX
        # (`type.indexOf('phaser')===0`), not by 16 more === literals, and the old extractor —
        # which only understood `type==='x'` and the mHN family — reported all 16 phaser keys as
        # ORPHANS drawing a generic 2-pole lowpass. They are not: they reach a real branch. A
        # false orphan is as corrosive as a missed one, because the next person deletes the bar.
        # NOTE THE LIMIT, so nobody reads more into a green [4] than it says: this asserts the key
        # REACHES a branch, never that the branch draws the right curve. That is
        # Tests/flt_curve_diff.js's job, and it currently rates all 11 phasers > 3 dB out.
        for pre in re.findall(r"type\.indexOf\(\s*'([A-Za-z0-9]+)'\s*\)===0", body) + \
                   re.findall(r"type\.startsWith\(\s*'([A-Za-z0-9]+)'\s*\)", body):
            branches |= {c for c in got["CAT"][2] if c.startswith(pre)}
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

    # [7] the harness's labels, index for index. Bar [2] does this for the UI; this does it for
    #     the measurement, and the two together are what make "type 84 moves 14.2 dB" a sentence
    #     about Samp-Hold - rather than about whichever type happens to sit at 84 in one file.
    cxx2, hn = got["names_add"][2], got["kName"][2]
    d2 = [(i, a, b) for i, (a, b) in enumerate(zip(cxx2, hn)) if a != b]
    chk(not d2 and len(cxx2) == len(hn),
        "[7] Tests/flt_measure.h kName[] IS terrainFilterEngineNames() INDEX FOR INDEX",
        "%d harness labels identical to the shipping roster" % len(hn) if not d2 and len(cxx2) == len(hn) else
        ("DIVERGED at " + ", ".join("[%d] C++ %r vs harness %r" % (i, a, b) for i, a, b in d2[:6])
         if d2 else "LENGTH: C++ %d vs harness %d" % (len(cxx2), len(hn))))

    print()
    print("  %d passed, %d FAILED" % (_pass, _fail))
    if _failed_bars:
        print("  FAILED BARS:")
        for b in _failed_bars:
            print("    - %s" % b)
    print()
    return 1 if _fail else 0


def mutate_and_run(which):
    """Copy the tree, break ONE list in the copy, run the gate against the copy.
       `which=None` copies WITHOUT mutating — that run is the BASELINE (see evidence())."""
    tmp = tempfile.mkdtemp(prefix="fltcard_mut_")
    try:
        for rel in ("Source/TerrainFilters.h", "Source/PluginProcessor.cpp",
                    "Source/ui/public/index.html", "Tests/flt_measure.h"):
            dst = os.path.join(tmp, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(ROOT, rel), dst)
        if which is not None:
            MUTATIONS[which](tmp)
        env = dict(os.environ)
        env.pop("TI_CARD_MUT", None)
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "--root", tmp],
                           capture_output=True, text=True, env=env)
        return r.returncode, r.stdout + r.stderr
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def evidence(out):
    """Every falsifiable claim a run makes: the site table (name -> count), each bar's verdict,
       and each detail line. fb604 — the matrix used to accept `rc != 0` as proof the mutation
       fired. THAT IS ONLY TRUE ON A GREEN TREE. Four agents are editing the roster in parallel
       and the tree is red for minutes at a time while an append lands; during those minutes a
       mutation that changed NOTHING would still have printed RED and the matrix would still
       have said 'the gate is live'. So a row now has to add a claim the unmutated copy of the
       same tree did not make."""
    ev = set()
    for l in out.splitlines():
        t = l.rstrip()
        if re.match(r"^    \w+ +(\d+|\?\?) ", t):        # the site table
            ev.add("SITE " + re.sub(r"\s+", " ", t))
        elif re.match(r"^  (PASS|FAIL)  ", t):             # bar verdicts
            ev.add("BAR " + re.sub(r"\s+", " ", t))
        elif t.startswith("        "):                      # bar details
            ev.add("DETAIL " + re.sub(r"\s+", " ", t.strip())[:200])
    return ev


if __name__ == "__main__":
    args = sys.argv[1:]
    root = ROOT
    if "--root" in args:
        root = args[args.index("--root") + 1]
    mut = os.environ.get("TI_CARD_MUT", "")

    if mut == "all":
        print("══ fb604 · CARDINALITY GATE — MUTATION MATRIX ══")
        print("   Each row breaks ONE roster site in a throwaway copy of the tree.")
        print("   A row is OK only if the gate goes RED *and* makes a claim the UNMUTATED copy of")
        print("   the same tree did not — so the matrix stays meaningful while the roster is mid-append.\n")
        brc, bout = mutate_and_run(None)
        base = evidence(bout)
        bbars = sorted(l.strip()[2:] for l in bout.splitlines() if l.strip().startswith("- ["))
        print("   baseline (no mutation): rc=%d  %s" % (brc, ("already RED: " + "; ".join(b[:44] for b in bbars))
                                                        if brc else "GREEN"))
        print()
        bad = 0
        for name in MUTATIONS:
            rc, out = mutate_and_run(name)
            gained = sorted(evidence(out) - base)
            bars = [l.strip()[2:] for l in out.splitlines() if l.strip().startswith("- [")]
            newbars = [b for b in bars if b not in bbars]
            ok = rc != 0 and bool(gained)
            if not ok:
                bad = 1
            print("   %-17s rc=%d  %-12s  +%d new claim(s)  new bars: %s"
                  % (name, rc, "RED" if rc else "GREEN-BROKEN", len(gained),
                     newbars if newbars else "(none — see the claim below)"))
            shown = 0
            for g in gained:
                if g.startswith("BAR PASS"):
                    continue
                print("                     %s" % g[:150])
                shown += 1
                if shown == 2:
                    break
            if not ok:
                print("                     ^^ THIS ROW PROVED NOTHING: the mutation changed no claim "
                      "the gate makes.")
        print()
        print("   %s" % ("every mutation added a NEW failing claim — the gate is live"
                         if not bad else "AT LEAST ONE MUTATION PROVED NOTHING — the gate is not a gate"))
        sys.exit(bad)

    if mut:
        if mut not in MUTATIONS:
            print("unknown TI_CARD_MUT=%r; pick one of: %s" % (mut, ", ".join(MUTATIONS) + ", all"))
            sys.exit(2)
        brc, bout = mutate_and_run(None)
        rc, out = mutate_and_run(mut)
        gained = sorted(evidence(out) - evidence(bout))
        print(out, end="")
        print("  MUTATION CONTROL TI_CARD_MUT=%s — rc=%d (baseline rc=%d)" % (mut, rc, brc))
        for g in gained:
            if not g.startswith("BAR PASS"):
                print("    NEW CLAIM: %s" % g[:150])
        ok = rc != 0 and bool(gained)
        print("    %s" % ("the control FIRED" if ok else
                          "THE CONTROL PROVED NOTHING — the mutation changed no claim the gate makes"))
        sys.exit(0 if ok else 1)   # the control PASSES when the mutation manufactured a NEW failure

    sys.exit(run_checks(root, "shipping tree" if root == ROOT else root))
