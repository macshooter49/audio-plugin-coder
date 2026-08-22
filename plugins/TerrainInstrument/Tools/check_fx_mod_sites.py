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
#    4. Then every read site in PluginProcessor.cpp is classified: through `M (...)`, or still
#       raw `->load()`. A cell passes only when every site it needs is an M-read and none of
#       them is still raw.
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
        m = re.search (r'auto&\s+(\w+)\s*=\s*(\w+Refs_)\s*\[', ln)
        if m: ref[m.group (1)] = m.group (2)
        elif re.search (r'auto&\s+(\w+)\s*=\s*\(\*', ln):
            ref[re.search (r'auto&\s+(\w+)', ln).group (1)] = '?'
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
def parse_ref_array_sizes (hdr_text):
    sizes = {}
    for m in re.finditer (r'std::array\s*<\s*\w+Refs\s*,\s*\(size_t\)\s*([\w:]+)\s*>\s*([^;]+);', hdr_text):
        for a in re.findall (r'(\w+Refs_)', m.group (2)): sizes[a] = m.group (1)
    return sizes

# ── 4. classify every read site in the .cpp ───────────────────────────────────────────────
M_REF   = re.compile (r'(?<![A-Za-z0-9_])M\s*\(\s*(\w+)\.(\w+)\s*(?:\[([^\]]*)\])?\s*\)')
RAW_REF = re.compile (r'(?<![A-Za-z0-9_])(\w+)\.(\w+)\s*(?:\[([^\]]*)\])?\s*->\s*load\s*\(\s*\)')
M_ID    = re.compile (r'(?<![A-Za-z0-9_])M\s*\(\s*rawParam\s*\(\s*ParameterIDs::(\w+)\s*\)\s*\)')
RAW_ID  = re.compile (r'rawParam\s*\(\s*ParameterIDs::(\w+)\s*\)\s*->\s*load\s*\(\s*\)')
M_TBL   = re.compile (r'(?<![A-Za-z0-9_])M\s*\(\s*rawParam\s*\(\s*(\w+)\s*\[([^\]]*)\]\s*\)\s*\)')
RAW_TBL = re.compile (r'rawParam\s*\(\s*(\w+)\s*\[([^\]]*)\]\s*\)\s*->\s*load\s*\(\s*\)')

def parse_id_tables (cpp_text):
    out = {}
    for m in re.finditer (r'static const char\* const (\w+)\s*\[\d+\]\s*=\s*\{(.*?)\};', cpp_text, re.S):
        ids = re.findall (r'ParameterIDs::(\w+)', m.group (2))
        if ids: out[m.group (1)] = ids
    return out

def scan_reads (lines, id_tables):
    """-> mread{key:[lines]}, rawread{key:[lines]}   key = ('ref',arr,member,idx) | ('id',ID)"""
    mread, rawread = {}, {}
    ref = {}
    def note (d, key, ln): d.setdefault (key, []).append (ln)
    def loop_bound (i, var):
        for back in range (0, 6):
            if i - back < 0: break
            m = re.search (r'for\s*\(\s*int\s+' + re.escape (var) + r'\s*=\s*0\s*;\s*'
                           + re.escape (var) + r'\s*<\s*(\d+)', lines[i - back])
            if m: return int (m.group (1))
        return None
    def indices (i, idx):
        if idx is None: return [None]
        v = eval_idx (idx, '', 0)
        if v is not None and re.fullmatch (r'\s*\d+\s*', idx): return [v]
        var = re.fullmatch (r'\s*(\w+)\s*', idx)
        if var:
            n = loop_bound (i, var.group (1))
            if n is not None: return list (range (n))
        return []
    for i, ln in enumerate (lines):
        m = re.search (r'auto&\s+(\w+)\s*=\s*(\w+Refs_)\s*\[', ln)
        if m: ref[m.group (1)] = m.group (2)
        elif re.search (r'auto&\s+(\w+)\s*=\s*\(\*', ln):
            ref[re.search (r'auto&\s+(\w+)', ln).group (1)] = '?'
        for pat, d in ((M_REF, mread), (RAW_REF, rawread)):
            for mm in pat.finditer (ln):
                arr = ref.get (mm.group (1))
                if arr in (None, '?'): continue
                for k in indices (i, mm.group (3)):
                    note (d, ('ref', arr, mm.group (2), k), i + 1)
        for pat, d in ((M_ID, mread), (RAW_ID, rawread)):
            for mm in pat.finditer (ln): note (d, ('id', mm.group (1)), i + 1)
        for pat, d in ((M_TBL, mread), (RAW_TBL, rawread)):
            for mm in pat.finditer (ln):
                ids = id_tables.get (mm.group (1))
                if not ids: continue
                for k in indices (i, mm.group (2)) or range (len (ids)):
                    if k is not None and 0 <= k < len (ids): note (d, ('id', ids[k]), i + 1)
    return mread, rawread

# ── the report ────────────────────────────────────────────────────────────────────────────
def main():
    inc_text = open (INC).read()
    cpp_text = open (CPP).read()
    hdr_text = open (HDR).read()
    cpp_lines = cpp_text.split ('\n')

    tag, leaf, declared_live = parse_inc (inc_text)
    cache = parse_cache_map (cpp_lines)
    cache.update (parse_fx4_spec (cpp_text))
    sizes = parse_ref_array_sizes (hdr_text)
    id_tables = parse_id_tables (cpp_text)
    mread, rawread = scan_reads (cpp_lines, id_tables)

    # param id -> the (refs array, member, index) that holds its pointer
    by_id = {}
    for k, pid in cache.items(): by_id.setdefault (pid, []).append (k)

    rows, live_total, ok_total = [], 0, 0
    missing, still_raw, unmapped = [], [], []
    for kd in range (16):
        cells = []
        for kn in range (12):
            lf = leaf[kd][kn]
            if lf is None: cells.append (None); continue
            live_total += 1
            pid = tag[kd] + '_' + lf
            sites = []
            for arr, member, idx in by_id.get (pid, []):
                sites.append ((('ref', arr, member, idx), sizes.get (arr, '?')))
            if not sites:
                unmapped.append ((kd, kn, pid)); cells.append ('?'); continue
            # a refs array only kFxExtra long starts at instance 2 — instance 1 is read by id
            if any (sz == 'kFxExtra' for _, sz in sites): sites.append ((('id', pid), 'inst1'))
            good = True
            for key, _ in sites:
                if key not in mread:
                    missing.append ((kd, kn, pid, key)); good = False
                if key in rawread:
                    still_raw.append ((kd, kn, pid, key, rawread[key])); good = False
            if good: ok_total += 1
            cells.append ('M' if good else 'x')
        rows.append (cells)

    print ('fb453 T4 — every live FX-rack dial read through M()')
    print ('  fx_mod_ids.inc : %d live cells declared' % (declared_live or -1))
    print ('  cache map      : %d (refs array, member) -> parameter bindings parsed' % len (cache))
    print ('  read sites     : %d M(...) keys, %d raw ->load() keys still present'
           % (len (mread), len (rawread)))
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

    if unmapped:
        print ('  !! NO CACHE BINDING — the parameter behind this dial was never found in a')
        print ('     cache routine, so nothing here can be checked at all:')
        for kd, kn, pid in unmapped: print ('       kind %2d knob %2d  %s' % (kd, kn, pid))
    if missing:
        print ('  !! NOT READ THROUGH M() — this knob has a destination and NO modulation:')
        for kd, kn, pid, key in missing:
            print ('       kind %2d (%s) knob %2d  %s   site %s' % (kd, KIND_NAMES[kd], kn, pid, key))
    if still_raw:
        print ('  !! STILL READ RAW — a ->load() that bypasses the matrix:')
        for kd, kn, pid, key, lns in still_raw:
            print ('       kind %2d (%s) knob %2d  %s   site %s at line(s) %s'
                   % (kd, KIND_NAMES[kd], kn, pid, key, lns))

    bad = live_total - ok_total
    print ('  %d/%d substituted, %d missing' % (ok_total, live_total, bad))
    if declared_live is not None and live_total != declared_live:
        print ('  !! the table says %d live cells, this walk found %d' % (declared_live, live_total))
        return 1
    return 0 if bad == 0 else 1

if __name__ == '__main__':
    sys.exit (main())
