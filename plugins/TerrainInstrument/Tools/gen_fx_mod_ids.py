#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_fx_mod_ids.py — emit Source/fx_mod_ids.inc, the dial -> plugin-parameter map for the FX rack.

WHY THIS IS GENERATED (fb453 T2)
--------------------------------
The dial and the destination must be authored in exactly ONE place. `Source/ui/public/index.html`
already names the parameter behind every rack dial: `DEV_TEMPLATES[k].knobs[n].p` for the four front
dials and `DEV_TEMPLATES[k].back.knobs[n][2]` for the eight back knobs. Re-typing that map in C++ is
184 chances to modulate the WRONG knob while every gate stays green. So it is generated from the
descriptor the UI itself renders from, and committed beside it — the same pattern this repo already
uses for Design/fx4/eq/shipped_labels.inc.

WHAT IT PARSES
--------------
- `var FX_PREFIX={...}`   -> the per-kind parameter prefix (`reverb:'SYN_RVB_'`). The tag emitted is
                             that prefix WITHOUT the trailing underscore, because instances 2..6
                             insert the number exactly there (`SYN_RVB_` -> `SYN_RVB2_`, instPrefix()).
- `var DEV_TEMPLATES=[...]` -> one entry per device kind, keyed by its `core:` field.
    * front dials: the template's own `knobs:` — TWO literal shapes exist and both are handled,
      `{l:'Size',v:30,p:'SYN_RVB_SIZE'}` and the positional `['Time',45,'SYN_TPE_TIME']` — plus one
      FUNCTION form, `knobs:TPE_FRONT('Studio')`, which is resolved from the function's own body and
      its lookup table (still one authorship: nothing is typed here).
    * back knobs: the template's own `back.knobs`.

⚠️ It parses the TEMPLATE's OWN `back.knobs`, never a file-wide regex. Some kinds carry PER-TYPE
   relabel tables elsewhere (FX4_SPL_BACK and friends) that rewrite back-knob LABELS per Type; a
   naive sweep finds those too and reports 16 back knobs for a device that has 8. The param IDs
   never change per Type — the descriptor is the truth.

OUTPUT ORDER is the canonical C++ KIND ORDER (PluginProcessor.h:1660), which is NOT the order the
templates happen to appear in:
    0=reverb 1=delay 2=saturate 3=granular 4=tape 5=flt 6=cho 7=fla 8=pha
    9=eqz 10=wid 11=cmp 12=ott 13=bod 14=utl 15=spl

Deterministic and re-runnable: same input -> byte-identical output.

Usage:  python3 Tools/gen_fx_mod_ids.py > Source/fx_mod_ids.inc      (run from plugins/TerrainInstrument)
"""

import os
import re
import sys

# ── The canonical kind order. Fixed by the C++ (PluginProcessor.h:1660); the dest arithmetic in
#    Task 1/3 indexes straight into it, so it is NOT the template array's order.
KIND_ORDER = ['reverb', 'delay', 'saturate', 'granular', 'tape', 'flt', 'cho', 'fla',
              'pha', 'eqz', 'wid', 'cmp', 'ott', 'bod', 'utl', 'spl']

KNOBS_PER_KIND = 12          # 4 front dials (Mix is 3) + 8 back knobs
FRONT_KNOBS    = 4
BACK_KNOBS     = 8

HERE      = os.path.dirname(os.path.abspath(__file__))
PLUGIN    = os.path.dirname(HERE)
HTML_PATH = os.path.join(PLUGIN, 'Source', 'ui', 'public', 'index.html')


# ══ a tolerant JS-literal reader ══════════════════════════════════════════════════════════════
# Enough of JS to walk object/array literals: it tracks string state so a `//` or a brace inside a
# quoted string can never be mistaken for syntax.

def strip_comments(s):
    """Remove // and /* */ comments, leaving string literals untouched."""
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c in "\"'":
            q = c
            out.append(c); i += 1
            while i < n:
                if s[i] == '\\':
                    out.append(s[i:i + 2]); i += 2; continue
                out.append(s[i])
                if s[i] == q:
                    i += 1; break
                i += 1
            continue
        if c == '/' and i + 1 < n and s[i + 1] == '/':
            while i < n and s[i] != '\n':
                i += 1
            continue
        if c == '/' and i + 1 < n and s[i + 1] == '*':
            j = s.find('*/', i + 2)
            i = (j + 2) if j >= 0 else n
            continue
        out.append(c); i += 1
    return ''.join(out)


def match_block(s, start):
    """s[start] is one of {[( . Return (inner_text, index_just_past_the_closer)."""
    pairs = {'{': '}', '[': ']', '(': ')'}
    opener = s[start]
    closer = pairs[opener]
    depth, i, n = 0, start, len(s)
    while i < n:
        c = s[i]
        if c in "\"'":
            q = c; i += 1
            while i < n:
                if s[i] == '\\':
                    i += 2; continue
                if s[i] == q:
                    i += 1; break
                i += 1
            continue
        if c in '{[(':
            depth += 1
        elif c in '}])':
            depth -= 1
            if depth == 0:
                return s[start + 1:i], i + 1
        i += 1
    raise ValueError('unbalanced %s at %d' % (opener, start))


def split_top(s):
    """Split on commas that sit at nesting depth 0."""
    parts, buf, depth, i, n = [], [], 0, 0, len(s)
    while i < n:
        c = s[i]
        if c in "\"'":
            q = c; buf.append(c); i += 1
            while i < n:
                if s[i] == '\\':
                    buf.append(s[i:i + 2]); i += 2; continue
                buf.append(s[i])
                if s[i] == q:
                    i += 1; break
                i += 1
            continue
        if c in '{[(':
            depth += 1
        elif c in '}])':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(''.join(buf)); buf = []; i += 1; continue
        buf.append(c); i += 1
    tail = ''.join(buf).strip()
    if tail:
        parts.append(tail)
    return [p.strip() for p in parts if p.strip()]


def obj_entries(s):
    """Parse a (comment-stripped) object-literal body into an ordered list of (key, value)."""
    out = []
    for part in split_top(s):
        m = re.match(r"""^\s*(?:'([^']*)'|"([^"]*)"|([A-Za-z_$][\w$]*))\s*:\s*""", part)
        if not m:
            continue
        key = m.group(1) or m.group(2) or m.group(3)
        out.append((key, part[m.end():].strip()))
    return out


def obj_get(s, key):
    for k, v in obj_entries(s):
        if k == key:
            return v
    return None


def js_string(tok):
    """Unquote a JS string literal; return None if the token is not a plain string."""
    tok = tok.strip()
    m = re.match(r"""^'([^']*)'$|^"([^"]*)"$""", tok)
    if not m:
        return None
    return m.group(1) if m.group(1) is not None else m.group(2)


def top_level_items(literal):
    """Given a `[...]` token, return the list of its top-level element tokens."""
    literal = literal.strip()
    if not literal.startswith('['):
        raise ValueError('not an array literal: %.40s' % literal)
    inner, _ = match_block(literal, 0)
    return split_top(inner)


def find_decl(src, name):
    """Return the token following `var <name>=` (an object/array literal)."""
    m = re.search(r'\bvar\s+' + re.escape(name) + r'\s*=\s*', src)
    if not m:
        raise ValueError('no declaration for ' + name)
    start = m.end()
    if src[start] not in '{[':
        raise ValueError('%s is not an object/array literal' % name)
    inner, _ = match_block(src, start)
    return src[start], inner


def find_function_body(src, name):
    m = re.search(r'\bfunction\s+' + re.escape(name) + r'\s*\(', src)
    if not m:
        raise ValueError('no function ' + name)
    brace = src.index('{', m.end())
    inner, _ = match_block(src, brace)
    return inner


# ══ knob descriptors ══════════════════════════════════════════════════════════════════════════

def knob_param(tok):
    """The parameter ID behind ONE knob descriptor. Both shapes the file uses are accepted:
         {l:'Size',v:30,p:'SYN_RVB_SIZE'}      — the named front shape
         ['Time',45,'SYN_TPE_TIME']            — the positional back shape (also used up front)
    """
    tok = tok.strip()
    if tok.startswith('{'):
        inner, _ = match_block(tok, 0)
        p = obj_get(inner, 'p')
        if p is None:
            raise ValueError('knob object has no p: %.60s' % tok)
        s = js_string(p)
        if s is None:
            raise ValueError('knob p is not a literal string: %s' % p)
        return s
    if tok.startswith('['):
        items = top_level_items(tok)
        if len(items) < 3:
            raise ValueError('positional knob is too short: %.60s' % tok)
        s = js_string(items[2])
        if s is None:
            raise ValueError('positional knob id is not a literal string: %s' % items[2])
        return s
    raise ValueError('unrecognised knob descriptor: %.60s' % tok)


def resolve_knob_call(src, expr):
    """Resolve `knobs:SOME_FN('Arg')` — the Tape front row (TPE_FRONT). Every id still comes out of
    the file: the function's lookup table supplies the per-type rows and its own `.push(...)` calls
    supply the fixed ones (Mix). Nothing is typed here."""
    m = re.match(r"^([A-Za-z_$][\w$]*)\s*\(", expr)
    if not m:
        return None
    fname = m.group(1)
    args = split_top(match_block(expr, expr.index('('))[0])
    arg = js_string(args[0]) if args else None

    body = find_function_body(src, fname)
    body = strip_comments(body)

    # the lookup table this function indexes, e.g. `TPE_SETS[type]||TPE_SETS['Studio']`
    tref = re.search(r"\b([A-Z][A-Z0-9_]*)\s*\[", body)
    if not tref:
        raise ValueError('%s indexes no table' % fname)
    table_name = tref.group(1)
    kind, table_src = find_decl(src, table_name)
    table = dict((js_string(k) if js_string(k) is not None else k, v)
                 for k, v in obj_entries(strip_comments(table_src)))

    # a `||TABLE['Default']` fallback in the body names the default row
    dflt = re.search(re.escape(table_name) + r"\[\s*'([^']*)'\s*\]", body)
    key = arg if arg in table else (dflt.group(1) if dflt else None)
    if key not in table:
        raise ValueError('%s: no row %r in %s' % (fname, arg, table_name))

    knobs = [knob_param(t) for t in top_level_items(table[key])]

    # trailing fixed knobs the function appends itself (Mix)
    for pm in re.finditer(r'\.push\s*\(', body):
        lit, _ = match_block(body, body.index('(', pm.end() - 1))
        for t in split_top(lit):
            knobs.append(knob_param(t))
    return knobs


# ══ main ══════════════════════════════════════════════════════════════════════════════════════

def main():
    with open(HTML_PATH, 'r', encoding='utf-8') as f:
        raw = f.read()
    src = strip_comments(raw)

    # ── FX_PREFIX: core -> 'SYN_RVB_'
    _, pfx_src = find_decl(src, 'FX_PREFIX')
    prefixes = {}
    for k, v in obj_entries(pfx_src):
        s = js_string(v)
        if s is None:
            raise ValueError('FX_PREFIX[%s] is not a literal string' % k)
        prefixes[k] = s

    # ── DEV_TEMPLATES: one object per kind, keyed by its own `core:`
    _, tpl_src = find_decl(src, 'DEV_TEMPLATES')
    templates = {}
    for tok in split_top(tpl_src):
        tok = tok.strip()
        if not tok.startswith('{'):
            raise ValueError('DEV_TEMPLATES element is not an object: %.60s' % tok)
        inner, _ = match_block(tok, 0)
        core = js_string(obj_get(inner, 'core') or '')
        if not core:
            raise ValueError('DEV_TEMPLATES element has no core: %.60s' % tok)
        if core in templates:
            raise ValueError('two templates share core %r' % core)
        templates[core] = inner

    missing = [c for c in KIND_ORDER if c not in templates]
    extra = [c for c in templates if c not in KIND_ORDER]
    if missing or extra:
        raise ValueError('kind order does not match DEV_TEMPLATES: missing=%s extra=%s'
                         % (missing, extra))

    rows, holes = [], []
    for kind, core in enumerate(KIND_ORDER):
        tpl = templates[core]
        prefix = prefixes.get(core)
        if prefix is None:
            raise ValueError('%s has no FX_PREFIX entry' % core)

        # front dials — a literal array, or a function call resolved from the file
        front_tok = obj_get(tpl, 'knobs')
        if front_tok is None:
            raise ValueError('%s has no front knobs' % core)
        if front_tok.strip().startswith('['):
            front = [knob_param(t) for t in top_level_items(front_tok)]
        else:
            front = resolve_knob_call(src, front_tok.strip())
            if front is None:
                raise ValueError('%s: cannot resolve front knobs %r' % (core, front_tok))

        # back knobs — the TEMPLATE's own back.knobs, never a file-wide sweep
        back = []
        back_tok = obj_get(tpl, 'back')
        if back_tok is not None and back_tok.strip().startswith('{'):
            back_inner, _ = match_block(back_tok.strip(), 0)
            bk = obj_get(back_inner, 'knobs')
            if bk is not None and bk.strip().startswith('['):
                back = [knob_param(t) for t in top_level_items(bk)]

        if len(front) > FRONT_KNOBS:
            raise ValueError('%s has %d front knobs (max %d)' % (core, len(front), FRONT_KNOBS))
        if len(back) > BACK_KNOBS:
            raise ValueError('%s has %d back knobs (max %d)' % (core, len(back), BACK_KNOBS))

        ids = front + [None] * (FRONT_KNOBS - len(front)) \
            + back + [None] * (BACK_KNOBS - len(back))

        leaves = []
        for n, pid in enumerate(ids):
            if pid is None:
                leaves.append(None)
                holes.append((core, n))
                continue
            if not pid.startswith(prefix):
                raise ValueError('%s knob %d: id %r does not start with %r'
                                 % (core, n, pid, prefix))
            leaves.append(pid[len(prefix):])
        rows.append((kind, core, prefix.rstrip('_'), leaves))

    # ── ALIASES. Two cells CAN name one parameter, and exactly one pair does today: the Delay's
    #    front "Time" dial and its back "Time L" knob are the same SYN_DLY_TIME (fb306-310, the L/R
    #    link — Time IS Time L). That is deliberate in the UI, so it is reported, not rejected; a
    #    NEW alias appearing here means someone pointed a dial at the wrong parameter.
    seen, aliases = {}, []
    for kind, core, tag, leaves in rows:
        for n, leaf in enumerate(leaves):
            if leaf is None:
                continue
            full = tag + '_' + leaf
            if full in seen:
                aliases.append((full, seen[full], (core, n)))
            else:
                seen[full] = (core, n)

    live = sum(1 for _, _, _, ls in rows for l in ls if l is not None)
    distinct = len(seen)

    # ── emit
    w = sys.stdout.write
    w('// GENERATED by Tools/gen_fx_mod_ids.py from Source/ui/public/index.html — DO NOT EDIT BY HAND.\n')
    w('// Regenerate with:  python3 Tools/gen_fx_mod_ids.py > Source/fx_mod_ids.inc\n')
    w('//\n')
    w('// The plugin parameter behind every FX-rack dial, in the UI\'s own order: [kind][knob], knob\n')
    w('// 0..3 = the four FRONT dials (Mix is 3), 4..11 = the eight BACK knobs. A NULL entry is a knob\n')
    w('// that device does not have. It is GENERATED because the dial and its destination must be\n')
    w('// authored in exactly ONE place — DEV_TEMPLATES already names the parameter behind every dial,\n')
    w('// and re-typing that map in C++ would be %d chances to modulate the wrong knob with every\n' % live)
    w('// gate still green (same pattern as Design/fx4/eq/shipped_labels.inc).\n')
    w('//\n')
    w('// An id is rebuilt from the two halves, because instances 2..6 insert their number at the\n')
    w('// split point (instPrefix(), index.html; `p = "SYN_UTL" + sfx + "_"`, PluginProcessor.cpp):\n')
    w('//     std::string id = kFxModTag[k] + (inst == 0 ? "" : std::to_string (inst + 1)) + "_"\n')
    w('//                    + kFxModLeaf[k][n];\n')
    w('//\n')
    w('// %d live (kind, knob) cells: %d kinds x %d, and the Filter\'s %d — fb384, the Filter has no\n'
      % (live, len(KIND_ORDER) - 1, KNOBS_PER_KIND, FRONT_KNOBS))
    w('// back panel at all, and it is the only device with holes.\n')
    w('//\n')
    if aliases:
        w('// ALIASES — %d cell(s) name a parameter another cell already names. Deliberate, from the UI:\n'
          % len(aliases))
        for full, a, b in aliases:
            w('//   %-16s = %s knob %d AND %s knob %d\n' % (full, a[0], a[1], b[0], b[1]))
        w('// (the Delay front "Time" dial and its back "Time L" knob ARE one parameter — fb306-310,\n')
        w('//  the L/R link.) %d cells, %d distinct parameters.\n' % (live, distinct))
    else:
        w('// No aliases: every live cell names a distinct parameter.\n')
    w('//\n')
    w('// The array dimensions are wc::kFxModKinds x wc::kFxModKnobs (%d x %d, SynthModConfig.h) and\n'
      % (len(KIND_ORDER), KNOBS_PER_KIND))
    w('// are deliberately NOT re-declared here: a second kFxModKinds at file scope makes the name\n')
    w('// ambiguous in any TU that says `using namespace wc;` (fxmod_cert does). `constexpr` so the\n')
    w('// same fragment is legal at file scope AND inside a class body.\n')
    w('\n')
    w('static constexpr int kFxModLive     = %d;\n' % live)
    w('static constexpr int kFxModDistinct = %d;   // live cells minus the %d deliberate alias(es) above\n'
      % (distinct, live - distinct))
    w('\n')
    w('// the instance-suffix point: "SYN_RVB" + "2" + "_" + leaf\n')
    w('static constexpr const char* const kFxModTag[%d] = {\n' % len(KIND_ORDER))
    for kind, core, tag, _ in rows:
        w('    "%s",%s/* %2d %-8s */\n' % (tag, ' ' * (12 - len(tag)), kind, core))
    w('};\n')
    w('\n')
    w('static constexpr const char* const kFxModLeaf[%d][%d] = {\n'
      % (len(KIND_ORDER), KNOBS_PER_KIND))
    for kind, core, tag, leaves in rows:
        w('    /* %2d %-8s */ { %s },\n'
          % (kind, core, ', '.join('nullptr' if l is None else '"%s"' % l for l in leaves)))
    w('};\n')

    sys.stderr.write('%d live cells, %d distinct params, %d holes%s\n'
                     % (live, distinct, len(holes),
                        (' (' + ', '.join('%s[%d]' % h for h in holes) + ')') if holes else ''))
    for full, a, b in aliases:
        sys.stderr.write('alias: %s = %s[%d] and %s[%d]\n' % (full, a[0], a[1], b[0], b[1]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
