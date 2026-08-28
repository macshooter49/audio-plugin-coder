#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb530 — THE TEN-SITE GATE.   python3 Tests/wt_list_gate.py     (run from the plugin ROOT)
#
#  The wavetable roster lives in TEN places. Grow any nine and the tenth silently selects a
#  different table — the fb373 failure, which has bitten this project three times:
#
#     1  Source/WavetableBank.h        enum Preset { … kNumPresets }
#     2  Source/WavetableBank.h        specForPreset() — one case per table
#     3  Source/PluginProcessor.cpp    SYN_OSC_A_WT_PRESET StringArray
#     4  …                             SYN_OSC_B_WT_PRESET StringArray
#     5  …                             SYN_OSC_C_WT_PRESET StringArray
#     6  …                             SYN_OSC_D_WT_PRESET StringArray
#     7  Source/ui/public/index.html   <select data-syn="SYN_OSC_A_WT_PRESET"> options
#     8  …                             …B…
#     9  …                             …C…
#    10  …                             …D…
#
#  fb529 closed the WRITE half at source (__synChoiceCount asks the PARAMETER, not the DOM), so a
#  short list can no longer renumber a menu. It cannot make a missing table REACHABLE, and it
#  cannot notice that osc D's list disagrees with osc A's. That is what this gate is for.
#
#  Tests/all_menus.js proves the LIVE behaviour of one oscillator's menu in a real browser; this
#  proves that all ten lists say the same thing, statically, for all four.
#
#  MUTATION CONTROL (fb421 — a gate that has never failed has never been tested):
#     WTLG_MUTATE=enum      drop the last enum entry           → site 1 fails
#     WTLG_MUTATE=case      drop the last specForPreset case   → site 2 fails
#     WTLG_MUTATE=strD      drop the last name from OSC D      → site 6 fails
#     WTLG_MUTATE=optD      drop the last <option> from OSC D  → site 10 fails
#     WTLG_MUTATE=rename    rename one HTML option             → the label cross-check fails
# ══════════════════════════════════════════════════════════════════════════════════════════════
import io, re, sys, os

MUT = os.environ.get('WTLG_MUTATE', '')
npass = nfail = 0
def chk(ok, what, detail=''):
    global npass, nfail
    if ok: npass += 1; print('  ok    ' + what + ('   ' + detail if detail else ''))
    else:  nfail += 1; print('  FAIL  ' + what + ('   ' + detail if detail else ''))

def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    return re.sub(r'//[^\n]*', '', s)

bank = io.open('Source/WavetableBank.h', encoding='utf-8').read()
wt   = io.open('Source/Wavetable.h',     encoding='utf-8').read()
proc = io.open('Source/PluginProcessor.cpp', encoding='utf-8').read()
html = io.open('Source/ui/public/index.html', encoding='utf-8').read()

# ── SITE 1 — the enum, in declaration order ───────────────────────────────────────────────────
m = re.search(r'enum\s+Preset\s*\{(.*?)kNumPresets', bank, re.S)
if not m: print('  FAIL  enum Preset not found in WavetableBank.h'); sys.exit(1)
enum_names = [t.split('=')[0].strip() for t in strip_comments(m.group(1)).split(',')]
enum_names = [n for n in enum_names if re.fullmatch(r'[A-Za-z_]\w*', n or '')]
if MUT == 'enum': enum_names = enum_names[:-1]
N = len(enum_names)
chk(N > 0, 'SITE 1 — enum Preset parsed', '%d entries, [0]=%s [%d]=%s' % (N, enum_names[0], N-1, enum_names[-1]))

# ── SITE 2 — specForPreset(), one case per enum entry, mapping to a maker that EXISTS ─────────
sw = re.search(r'specForPreset\s*\(int preset\).*?\{(.*?)\n        \}', bank, re.S).group(1)
if MUT == 'case':
    i = sw.rfind('case '); sw = sw[:i]
cases = dict(re.findall(r'case\s+(\w+)\s*:\s*return\s+Wavetable::(make\w+Spec)\s*\(\)\s*;', sw))
missing = [n for n in enum_names if n not in cases]
chk(not missing, 'SITE 2 — specForPreset() has a case for every enum entry',
    'missing: ' + (', '.join(missing) if missing else 'none'))
noimpl = sorted({v for v in cases.values() if ('static WavetableSpec ' + v + '()') not in wt})
chk(not noimpl, 'SITE 2 — every case maps to a maker that exists in Wavetable.h',
    'undefined: ' + (', '.join(noimpl) if noimpl else 'none'))
dupes = [v for v in set(cases.values()) if list(cases.values()).count(v) > 1]
chk(not dupes, 'SITE 2 — no two tables share a maker (a copy-paste case is a silent duplicate)',
    'shared: ' + (', '.join(sorted(dupes)) if dupes else 'none'))

# ── SITES 3-6 — the four StringArrays ─────────────────────────────────────────────────────────
arrays = {}
for o in 'ABCD':
    a = proc.index('ParameterIDs::SYN_OSC_%s_WT_PRESET, 1 }' % o)
    b = proc.index('juce::StringArray {', a); e = proc.index('},', b)
    body = strip_comments(proc[b:e])
    names = [x[1:-1] for x in re.findall(r'"(?:[^"\\]|\\.)*"', body)]
    if MUT == 'strD' and o == 'D': names = names[:-1]
    arrays[o] = names
    chk(len(names) == N, 'SITE %d — OSC %s StringArray has kNumPresets entries' % (2 + 'ABCD'.index(o) + 1, o),
        '%d vs %d' % (len(names), N))
for o in 'BCD':
    chk(arrays[o] == arrays['A'], 'SITES 3-6 — OSC %s StringArray is identical to OSC A' % o,
        'first difference: ' + str([(i, arrays['A'][i] if i < len(arrays['A']) else None,
                                    arrays[o][i] if i < len(arrays[o]) else None)
                                   for i in range(max(len(arrays['A']), len(arrays[o])))
                                   if (arrays['A'][i:i+1] or [None])[0] != (arrays[o][i:i+1] or [None])[0]][:1]))

# ── SITES 7-10 — the four <select>s ───────────────────────────────────────────────────────────
def norm(s):   # the HTML uses the real arrow glyph; the C++ StringArray uses ASCII
    return s.replace('→', '->').replace('&amp;', '&').strip()
opts = {}
for o in 'ABCD':
    a = html.index('data-syn="SYN_OSC_%s_WT_PRESET"' % o)
    e = html.index('</select>', a)
    block = html[a:e]
    if MUT == 'optD' and o == 'D':
        block = block[:block.rfind('<option')]
    if MUT == 'rename' and o == 'B':
        block = block.replace('>Terra Growl<', '>Terra Grrrowl<')
    rows = re.findall(r'<option value="(\d+)"[^>]*>(.*?)</option>', block, re.S)
    opts[o] = rows
    site = 7 + 'ABCD'.index(o)
    chk(len(rows) == N, 'SITE %d — OSC %s <select> has kNumPresets options' % (site, o), '%d vs %d' % (len(rows), N))
    seq = [int(v) for v, _ in rows]
    chk(seq == list(range(len(rows))), 'SITE %d — OSC %s option VALUES are 0..N-1 in order' % (site, o),
        'first out of order: ' + str(next((i for i, v in enumerate(seq) if v != i), None)))
    bad = [(i, arrays['A'][i], norm(t)) for i, (v, t) in enumerate(rows)
           if i < len(arrays['A']) and norm(t) != arrays['A'][i]]
    chk(not bad, 'SITE %d — OSC %s option LABELS match the parameter\'s StringArray' % (site, o),
        'mismatches: ' + (str(bad[:3]) if bad else 'none'))

# ── the no-doubles rule (CLAUDE.md §5): never the same name twice ─────────────────────────────
seen = {}
dups = [n for n in arrays['A'] if (seen.setdefault(n, 0) or seen.update({n: seen[n] + 1})) or seen[n] > 1]
chk(len(set(arrays['A'])) == len(arrays['A']), 'NO DOUBLES — no table name appears twice',
    'repeated: ' + (', '.join(sorted(set(dups))) if dups else 'none'))

print('\n%d passed, %d failed%s' % (npass, nfail, ('   [MUTATION ' + MUT + ']') if MUT else ''))
sys.exit(1 if nfail else 0)
