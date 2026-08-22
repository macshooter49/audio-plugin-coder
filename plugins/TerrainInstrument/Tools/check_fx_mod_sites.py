#!/usr/bin/env python3
# ═══════════════════════════════════════════════════════════════════════════════════════════
#  fb453 T4 — PROVE EVERY LIVE RACK DIAL IS READ THROUGH M().
#
#  The bug this exists to catch is SILENT: a knob whose read site still says `p->load()` has a
#  modulation destination in the matrix, a route the user can draw, a lamp that lights — and no
#  audible effect, ever. Nothing crashes, nothing warns, every other gate stays green. That is
#  the fb373 shape ("verify the PATH, not just the engine") one level down.
#
#  So this checker proves PRESENCE, never the absence of a pattern:
#    1. `Source/fx_mod_ids.inc` names the parameter behind every dial  -> the 184 live cells.
#    2. The cache routines in PluginProcessor.cpp/.h say WHICH C++ EXPRESSION holds that
#       parameter's pointer (`v.size = R (r + "SIZE")` => rvbRefs_::size IS SYN_RVB_SIZE). That
#       map is PARSED, never re-typed here — re-typing it would be 184 fresh chances to check
#       the wrong knob.
#    3. A refs array declared `kFxExtra` long covers instances 2..6 ONLY, so that kind's
#       instance 1 must ALSO be read directly by parameter id. Those sites are required too —
#       derived from the array's own declared length, not from a list.
#    4. Then EVERY textual occurrence of that pointer expression in PluginProcessor.cpp is
#       classified. A cell passes only when every site it needs has at least one `M (...)` read
#       AND every other occurrence of it is an accounted-for non-read (a cache binding, or a
#       `!= nullptr` guard). Anything else is a FAILURE.
#
#  🚨 IT FAILS CLOSED, AND THAT IS THE WHOLE POINT (fb393 — a harness kinder than reality is
#     worse than no harness). Review round 1 fooled the first version of this file three ways,
#     each returning a green 184/184 over a genuinely raw read:
#       · the refs array's length written as `(ParameterIDs::kFxInstances - 1)` instead of
#         `kFxExtra` — semantically identical, unrecognised, and the instance-1 requirement was
#         silently DROPPED. An unknown size expression is now a hard error, never a shrug.
#       · a read reverted to `*rawParam (...)` — the file's own idiom for choices — which no
#         `->load()` regex can see. Raw forms are NO LONGER ENUMERATED: an occurrence that is
#         not provably an M-read or a known non-read is a failure by default.
#       · an alias bound as `const RvbRefs& W = rvbRefs_[e]` instead of `auto&`, which the
#         binding regex did not match, so `W.size->load()` belonged to no device at all. Any
#         `<x>Refs_[...]` line that yields no binding and no member access is now a hard error.
#
#  Usage:  python3 Tools/check_fx_mod_sites.py           (run from plugins/TerrainInstrument)
#  Exit 0 only on "184/184 substituted, 0 missing".
# ═══════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (HERE)
INC  = os.path.join (ROOT, 'Source', 'fx_mod_ids.inc')
CPP  = os.path.join (ROOT, 'Source', 'PluginProcessor.cpp')
HDR  = os.path.join (ROOT, 'Source', 'PluginProcessor.h')

KIND_NAMES = ['reverb','delay','saturate','granular','tape','flt','cho','fla',
              'pha','eqz','wid','cmp','ott','bod','utl','spl']

# ── 1. the live cells ──────────────────────────────────────────────────────────────────────
def parse_inc (text):
    tags = re.search (r'kFxModTag\s*\[16\]\s*=\s*\{(.*?)\};', text, re.S)
    if not tags: sys.exit ('fx_mod_ids.inc: no kFxModTag table')
    tag = [m.group (1) for m in re.finditer (r'"([^"]+)"', tags.group (1))]
    body = re.search (r'kFxModLeaf\s*\[16\]\s*\[12\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not body: sys.exit ('fx_mod_ids.inc: no kFxModLeaf table')
    leaf = []
    for row in re.finditer (r'\{([^{}]*)\}', body.group (1)):
        cells = []
        for c in row.group (1).split (','):
            c = c.strip()
            if not c: continue
            cells.append (None if c == 'nullptr' else c.strip ('"'))
        leaf.append (cells)
    live = re.search (r'kFxModLive\s*=\s*(\d+)', text)
    if len (tag) != 16 or len (leaf) != 16 or any (len (r) != 12 for r in leaf):
        sys.exit ('fx_mod_ids.inc: table is not 16 x 12')
    return tag, leaf, int (live.group (1)) if live else None

# ── a paren-balanced argument grab, so `R (g + "X" + juce::String (k + 1))` parses ──────────
def grab_call_arg (s, open_idx):
    depth, i = 0, open_idx
    while i < len (s):
        if s[i] == '(': depth += 1
        elif s[i] == ')':
            depth -= 1
            if depth == 0: return s[open_idx + 1 : i], i
        i += 1
    return None, len (s)

def split_top_plus (s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(': depth += 1
        elif ch == ')': depth -= 1
        if ch == '+' and depth == 0:
            out.append (cur.strip()); cur = ''
        else: cur += ch
    out.append (cur.strip())
    return [t for t in out if t]

SAFE_IDX = re.compile (r'^[0-9\s+*a-zA-Z_]+$')
def eval_idx (expr, var, val):
    expr = expr.strip()
    if not SAFE_IDX.match (expr): return None
    try: return int (eval (expr, {'__builtins__': {}}, {var: val}))
    except Exception: return None

# ── 2. leaf -> (refs array, member, index) — PARSED from the cache routines ────────────────
def parse_cache_map (cpp_lines):
    """returns  {(refsArray, member, index) : "SYN_XXX_LEAF"}"""
    out = {}
    pfx  = {}     # juce::String var  -> "SYN_XXX"   ('?' = table-driven)
    ref  = {}     # local var         -> refs array  ('?' = table-driven)
    forv = {}     # loop var          -> count, most recent
    assign = re.compile (r'(\w+)\.(\w+)\s*(?:\[([^\]]*)\])?\s*=\s*R\s*\(')
    for ln in cpp_lines:
        for m in re.finditer (r'for\s*\(\s*int\s+(\w+)\s*=\s*0\s*;\s*\1\s*<\s*(\d+)', ln):
            forv[m.group (1)] = int (m.group (2))
        m = re.search (r'const\s+juce::String\s+(\w+)\s*=\s*(.*)$', ln)
        if m:
            rhs = m.group (2)
            lit = re.search (r'"(SYN_[A-Z0-9]+)_?"', rhs)
            pfx[m.group (1)] = lit.group (1) if lit else '?'
        # the binding form is NOT assumed to be `auto&` — review round 1 slipped a raw read past
        # this checker with `const RvbRefs& W = rvbRefs_[e];`, which no `auto&` regex can see.
        for m in re.finditer (r'(?:const\s+)?[\w:]+\s*&\s*(\w+)\s*=\s*(\w+Refs_)\s*\[', ln):
            ref[m.group (1)] = m.group (2)
        for m in re.finditer (r'(?:const\s+)?[\w:]+\s*&\s*(\w+)\s*=\s*\(\s*\*', ln):
            ref[m.group (1)] = '?'
        for m in assign.finditer (ln):
            var, member, idx = m.group (1), m.group (2), m.group (3)
            arr = ref.get (var)
            if arr in (None, '?'): continue
            arg, _ = grab_call_arg (ln, m.end (0) - 1)
            if arg is None: continue
            toks = split_top_plus (arg)
            if not toks or toks[0] not in pfx: continue
            tag = pfx[toks[0]]
            if tag == '?': continue
            text, ivar, ioff = '', None, 0
            ok = True
            for t in toks[1:]:
                sl = re.fullmatch (r'"([^"]*)"', t)
                js = re.fullmatch (r'juce::String\s*\((\w+)\s*\+\s*(\d+)\)', t)
                if sl: text += sl.group (1)
                elif js: ivar, ioff, text = js.group (1), int (js.group (2)), text + '\x00'
                else: ok = False; break
            if not ok: continue
            if ivar is None:
                key = (arr, member, eval_idx (idx, '', 0) if idx else None)
                out[key] = tag + '_' + text
            else:
                n = forv.get (ivar)
                if n is None: continue
                for i in range (n):
                    k = eval_idx (idx, ivar, i) if idx else None
                    out[(arr, member, k)] = tag + '_' + text.replace ('\x00', str (i + ioff))
    return out

def parse_fx4_spec (cpp_text):
    """the four+one table-driven devices: kSpec names the leaves, kArr the refs arrays."""
    out = {}
    spec = re.search (r'static const Spec kSpec\s*\[5\]\s*=\s*\{(.*?)\n\s*\};', cpp_text, re.S)
    arr  = re.search (r'kArr\s*\[5\]\s*=\s*\n?\s*\{(.*?)\};', cpp_text, re.S)
    if not spec or not arr: sys.exit ('cacheFx4Refs: kSpec / kArr not found')
    arrs = re.findall (r'&(\w+Refs_)', arr.group (1))
    rows = re.findall (r'\{\s*"(SYN_[A-Z0-9]+)",\s*"[^"]*",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",'
                       r'\s*\{([^}]*)\}', spec.group (1))
    if len (rows) != 5 or len (arrs) != 5: sys.exit ('cacheFx4Refs: kSpec/kArr are not 5 rows')
    for (tag, f1, f2, f3, bs), a in zip (rows, arrs):
        out[(a, 'f1',  None)] = tag + '_' + f1
        out[(a, 'f2',  None)] = tag + '_' + f2
        out[(a, 'f3',  None)] = tag + '_' + f3
        out[(a, 'mix', None)] = tag + '_MIX'
        for i, b in enumerate (re.findall (r'"([^"]*)"', bs)):
            out[(a, 'b', i)] = tag + '_' + b
    return out

# ── 3. which refs arrays cover instance 1, and which start at instance 2 ───────────────────
# 🚨 STRICT. A refs array `kFxExtra` long holds instances 2..6, so that kind's instance 1 MUST be
#    read somewhere else, by parameter id — and the checker demands that second site. Derive that
#    from a string it does not recognise and the requirement vanishes with no diagnostic, which is
#    review round 1's Important-1: `(size_t) (ParameterIDs::kFxInstances - 1)` is the SAME array,
#    reads the same, and made a raw instance-1 read invisible. So: exactly two spellings are
#    understood, and anything else — including an array with no declaration at all — is fatal.
KNOWN_SIZES = {'kFxExtra': 'inst2_6', 'ParameterIDs::kFxInstances': 'all'}

def parse_ref_array_sizes (hdr_text):
    sizes, bad = {}, []
    for m in re.finditer (r'std::array\s*<\s*(\w+Refs)\s*,\s*([^>]*?)\s*>\s*([^;]+);', hdr_text):
        raw = m.group (2).strip()
        txt = re.sub (r'\(\s*size_t\s*\)', '', raw).strip()
        while txt.startswith ('(') and txt.endswith (')'): txt = txt[1:-1].strip()
        txt = re.sub (r'\s+', ' ', txt)
        for a in re.findall (r'(\w+Refs_)', m.group (3)):
            sizes[a] = txt
            if txt not in KNOWN_SIZES: bad.append ((a, raw))
    return sizes, bad

# ── 4. classify EVERY occurrence of every rack parameter's pointer expression ──────────────
# The first version of this file asked "is there an M-read, and is there a `->load()`?". That is
# a question about FOUR REGEXES, and review round 1 walked straight past them with `*rawParam (…)`
# — the file's own idiom for choice params, and what the Convolution idle bake used to read Size
# with. So the question is now the other way round: find every place the pointer expression
# appears AT ALL, and demand that each one be provably harmless. A read form nobody thought of is
# a failure, not a blind spot.
ALIAS_BIND  = re.compile (r'(?:const\s+)?[\w:]+\s*&\s*(\w+)\s*=\s*(\w+Refs_)\s*\[')
ALIAS_OPAQUE= re.compile (r'(?:const\s+)?[\w:]+\s*&\s*(\w+)\s*=\s*\(\s*\*')
ARR_DIRECT  = re.compile (r'(\w+Refs_)\s*\[[^\]]*\]\s*\.\s*(\w+)\s*(\[[^\]]*\])?')
REFS_TOUCH  = re.compile (r'\w+Refs_\s*\[')
ID_EXPR     = re.compile (r'rawParam\s*\(\s*ParameterIDs::(\w+)\s*\)')
TBL_EXPR    = re.compile (r'rawParam\s*\(\s*(\w+)\s*\[([^\]]*)\]\s*\)')
M_OPEN      = re.compile (r'(?<![A-Za-z0-9_])M\s*\(\s*$')

def parse_id_tables (cpp_text):
    out = {}
    for m in re.finditer (r'static const char\* const (\w+)\s*\[\d+\]\s*=\s*\{(.*?)\};', cpp_text, re.S):
        ids = re.findall (r'ParameterIDs::(\w+)', m.group (2))
        if ids: out[m.group (1)] = ids
    return out

def classify (line, a, b):
    """what is this occurrence of the pointer expression DOING? 'M' | 'bind' | 'guard' | 'raw'"""
    before, after = line[:a], line[b:]
    if M_OPEN.search (before) and re.match (r'\s*\)', after):  return 'M'      # M (expr)
    if re.match (r'\s*(==|!=)\s*nullptr', after):              return 'guard'  # a null guard
    if re.match (r'\s*=(?!=)', after):                         return 'bind'   # cache: expr = R (…)
    return 'raw'                                                               # ANYTHING else

def scan_reads (lines, id_tables, wanted_ref, wanted_id):
    """-> uses{key:[(line, kind)]}, unparsed[]   key = ('ref',arr,member,idx) | ('id',ID)

    `wanted_ref` / `wanted_id` are the keys that actually belong to a live cell; everything else
    (active, rank, power, type, the pills, src[]) is not this checker's business."""
    uses, unparsed = {}, []
    ref = {}
    def note (key, ln, kind): uses.setdefault (key, []).append ((ln, kind))
    def loop_bound (i, var):
        for back in range (0, 6):
            if i - back < 0: break
            m = re.search (r'for\s*\(\s*int\s+' + re.escape (var) + r'\s*=\s*0\s*;\s*'
                           + re.escape (var) + r'\s*<\s*(\d+)', lines[i - back])
            if m: return int (m.group (1))
        return None
    def indices (i, idx):
        if idx is None: return [None]
        idx = idx.strip ('[]')
        if re.fullmatch (r'\s*\d+\s*', idx): return [int (idx)]
        var = re.fullmatch (r'\s*(\w+)\s*', idx)
        if var:
            n = loop_bound (i, var.group (1))
            if n is not None: return list (range (n))
        return []
    for i, ln in enumerate (lines):
        touched = False
        for m in ALIAS_BIND.finditer (ln): ref[m.group (1)] = m.group (2); touched = True
        for m in ALIAS_OPAQUE.finditer (ln): ref[m.group (1)] = '?'
        # (a) through a bound alias
        for alias, arr in ref.items():
            if arr == '?': continue
            for m in re.finditer (r'(?<![\w.>])' + re.escape (alias)
                                  + r'\s*\.\s*(\w+)\s*(\[[^\]]*\])?', ln):
                for k in indices (i, m.group (2)):
                    key = ('ref', arr, m.group (1), k)
                    if key in wanted_ref: note (key, i + 1, classify (ln, m.start(), m.end()))
        # (b) indexed straight off the array
        for m in ARR_DIRECT.finditer (ln):
            touched = True
            for k in indices (i, m.group (3)):
                key = ('ref', m.group (1), m.group (2), k)
                if key in wanted_ref: note (key, i + 1, classify (ln, m.start(), m.end()))
        # (c) by parameter id, and (d) through an id table
        for m in ID_EXPR.finditer (ln):
            if m.group (1) in wanted_id:
                note (('id', m.group (1)), i + 1, classify (ln, m.start(), m.end()))
        for m in TBL_EXPR.finditer (ln):
            ids = id_tables.get (m.group (1))
            if not ids: continue
            for k in indices (i, m.group (2)) or range (len (ids)):
                if k is None or not (0 <= k < len (ids)): continue
                if ids[k] in wanted_id:
                    note (('id', ids[k]), i + 1, classify (ln, m.start(), m.end()))
        # 🚨 a refs array touched in a shape this scanner does not understand is FATAL: review
        #    round 1's `const RvbRefs& W = rvbRefs_[e]` bound an alias nothing tracked, so
        #    `W.size->load()` belonged to no device and 184/184 stayed green over a raw read.
        if REFS_TOUCH.search (ln) and not touched:
            unparsed.append ((i + 1, ln.strip()[:100]))
    return uses, unparsed

# ── the report ────────────────────────────────────────────────────────────────────────────
def main():
    inc_text = open (INC).read()
    cpp_text = open (CPP).read()
    hdr_text = open (HDR).read()
    cpp_lines = cpp_text.split ('\n')

    tag, leaf, declared_live = parse_inc (inc_text)
    cache = parse_cache_map (cpp_lines)
    cache.update (parse_fx4_spec (cpp_text))
    sizes, bad_sizes = parse_ref_array_sizes (hdr_text)
    id_tables = parse_id_tables (cpp_text)

    # param id -> the (refs array, member, index) slots that hold its pointer
    by_id = {}
    for k, pid in cache.items(): by_id.setdefault (pid, []).append (k)

    # ── which sites each live cell needs, decided BEFORE anything is scanned ───────────────
    need, fatal = {}, []
    wanted_ref, wanted_id = set(), set()
    live_cells = []
    for kd in range (16):
        for kn in range (12):
            lf = leaf[kd][kn]
            if lf is None: continue
            pid = tag[kd] + '_' + lf
            live_cells.append ((kd, kn, pid))
            slots = by_id.get (pid, [])
            sites = []
            for arr, member, idx in slots:
                sz = sizes.get (arr)
                if sz is None:
                    fatal.append ('refs array %s holds %s but has NO std::array declaration in '
                                  'PluginProcessor.h — its instance coverage is unknown' % (arr, pid))
                elif sz not in KNOWN_SIZES:
                    pass          # already reported once, per array, in bad_sizes
                sites.append (('ref', arr, member, idx))
                wanted_ref.add (('ref', arr, member, idx))
                # a kFxExtra-long array is instances 2..6 ONLY: instance 1 is read by id, and
                # that second site is REQUIRED, not optional.
                if KNOWN_SIZES.get (sz) == 'inst2_6':
                    sites.append (('id', pid)); wanted_id.add (pid)
            need[(kd, kn)] = sites

    uses, unparsed = scan_reads (cpp_lines, id_tables, wanted_ref, wanted_id)

    rows, ok_total = [], 0
    missing, still_raw, unmapped = [], [], []
    for kd in range (16):
        cells = []
        for kn in range (12):
            if leaf[kd][kn] is None: cells.append (None); continue
            pid = tag[kd] + '_' + leaf[kd][kn]
            sites = need[(kd, kn)]
            if not sites:
                unmapped.append ((kd, kn, pid)); cells.append ('?'); continue
            good = True
            for key in sites:
                u = uses.get (key, [])
                if not any (k == 'M' for _, k in u):
                    missing.append ((kd, kn, pid, key)); good = False
                raws = [ln for ln, k in u if k == 'raw']
                if raws:
                    still_raw.append ((kd, kn, pid, key, raws)); good = False
            if good: ok_total += 1
            cells.append ('M' if good else 'x')
        rows.append (cells)
    live_total = len (live_cells)

    print ('fb453 T4 — every live FX-rack dial read through M()')
    print ('  fx_mod_ids.inc : %d live cells declared' % (declared_live or -1))
    print ('  cache map      : %d (refs array, member) -> parameter bindings parsed' % len (cache))
    print ('  required sites : %d (%d by refs member, %d by parameter id for an instance 1 that '
           'no refs array covers)' % (sum (len (v) for v in need.values()),
                                      len ([1 for v in need.values() for k in v if k[0] == 'ref']),
                                      len ([1 for v in need.values() for k in v if k[0] == 'id'])))
    print ('  occurrences    : %d classified — %d M(...), %d cache bindings, %d null guards, %d RAW'
           % (sum (len (v) for v in uses.values()),
              sum (1 for v in uses.values() for _, k in v if k == 'M'),
              sum (1 for v in uses.values() for _, k in v if k == 'bind'),
              sum (1 for v in uses.values() for _, k in v if k == 'guard'),
              sum (1 for v in uses.values() for _, k in v if k == 'raw')))
    print()
    print ('  kind                 tag        0  1  2  3 | 4  5  6  7  8  9 10 11    ok')
    print ('  ' + '-' * 74)
    for kd in range (16):
        cells = rows[kd]
        def g (c): return ' . ' if c is None else (' M ' if c == 'M' else (' ? ' if c == '?' else ' X '))
        live = sum (1 for c in cells if c is not None)
        good = sum (1 for c in cells if c == 'M')
        print ('  %2d %-10s %-10s %s|%s  %2d/%-2d %s'
               % (kd, KIND_NAMES[kd], tag[kd],
                  ''.join (g (c) for c in cells[:4]), ''.join (g (c) for c in cells[4:]),
                  good, live, '' if good == live else '  <-- FAIL'))
    print()

    hard = 0
    if bad_sizes:
        hard += 1
        print ('  !! UNRECOGNISED REFS-ARRAY LENGTH — the instance-1 requirement cannot be derived,')
        print ('     so it is NOT quietly dropped. Spell it kFxExtra or ParameterIDs::kFxInstances:')
        for a, raw in sorted (set (bad_sizes)): print ('       %-12s declared <%s>' % (a, raw))
    if fatal:
        hard += 1
        print ('  !! REFS ARRAY WITH NO DECLARATION:')
        for f in sorted (set (fatal)): print ('       ' + f)
    if unparsed:
        hard += 1
        print ('  !! A REFS ARRAY IS TOUCHED IN A SHAPE THIS SCANNER DOES NOT UNDERSTAND. Reads')
        print ('     through it would be invisible, so this is fatal rather than ignored:')
        for ln, txt in unparsed[:12]: print ('       line %-6d %s' % (ln, txt))
    if unmapped:
        hard += 1
        print ('  !! NO CACHE BINDING — the parameter behind this dial was never found in a')
        print ('     cache routine, so nothing here can be checked at all:')
        for kd, kn, pid in unmapped: print ('       kind %2d knob %2d  %s' % (kd, kn, pid))
    if missing:
        print ('  !! NOT READ THROUGH M() — this knob has a destination and NO modulation:')
        for kd, kn, pid, key in missing:
            print ('       kind %2d (%s) knob %2d  %s   site %s' % (kd, KIND_NAMES[kd], kn, pid, key))
    if still_raw:
        print ('  !! READ WITHOUT M() — an occurrence that is neither an M-read, a cache binding')
        print ('     nor a null guard. It bypasses the matrix whatever form it takes:')
        for kd, kn, pid, key, lns in still_raw:
            print ('       kind %2d (%s) knob %2d  %s   site %s at line(s) %s'
                   % (kd, KIND_NAMES[kd], kn, pid, key, lns))

    bad = live_total - ok_total
    if declared_live is not None and live_total != declared_live:
        print ('  !! the table says %d live cells, this walk found %d' % (declared_live, live_total))
        hard += 1
    # 🚨 THE SUCCESS STRING IS A PROMISE. When anything above is blocking, this checker cannot
    #    see the whole tree, and a count printed over a tree it cannot read IS the false green it
    #    exists to prevent — so the exact phrase "N/N substituted, 0 missing" is withheld, not
    #    merely accompanied by a warning nobody greps for.
    if hard:
        print ('  REFUSED — %d blocking problem(s) above; the tree is not in a shape this checker'
               % hard)
        print ('  can vouch for. (%d/%d of the cells it COULD still see look substituted, which'
               % (ok_total, live_total))
        print ('  proves nothing about the ones it could not.)')
        return 1
    print ('  %d/%d substituted, %d missing' % (ok_total, live_total, bad))
    return 0 if bad == 0 else 1

if __name__ == '__main__':
    sys.exit (main())
