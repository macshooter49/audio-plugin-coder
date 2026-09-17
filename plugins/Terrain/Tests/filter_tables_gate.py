#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  filter_tables_gate.py — tp25 · THE TABLE ROSTER IS ONE LIST, IN ONE ORDER, IN THREE PLACES.
#
#      python3 Tests/filter_tables_gate.py            # from plugins/Terrain
#      TI_FTBL_MUT=<name> python3 Tests/filter_tables_gate.py   # mutation control
#
#  The factory library is named in three places that must agree index-for-index forever:
#      Resources/Wavetables/<cat>/<name>.flac   the files themselves
#      Source/FactoryTableIds.h                 what SYN_FILTER1/2_TBL appends after the built-ins
#      index.html  window.__factoryTables       what the Tables category offers
#  A choice parameter's index IS its saved meaning, so a disagreement here does not throw an error
#  — it silently gives a saved patch a different filter shape than the one it was saved with.
#
#  It also holds the design decision in place: the Tables rows are NOT filter-type indices. The
#  type parameter stays as wide as it was, so no host automation lane is renumbered. If someone
#  later "simplifies" this by appending 500 entries to the type roster, bar [5] goes red.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys, glob, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MUT = os.environ.get('TI_FTBL_MUT', '')
_p = _f = 0
def chk(ok, what, detail=''):
    global _p, _f
    if ok: _p += 1
    else:  _f += 1
    print('  %s  %s' % ('PASS' if ok else 'FAIL', what))
    if detail and not ok: print('        %s' % detail)

hdr  = open(os.path.join(ROOT, 'Source', 'FactoryTableIds.h'), encoding='utf-8').read()
html = open(os.path.join(ROOT, 'Source', 'ui', 'public', 'index.html'), encoding='utf-8').read()
cpp  = open(os.path.join(ROOT, 'Source', 'PluginProcessor.cpp'), encoding='utf-8').read()

if MUT == 'drop_js':      html = html.replace('"Blancmange", ', '', 1)
if MUT == 'reorder_js':
    m = re.search(r'window\.__factoryTables=\[\n(.*?)\n      \];', html, re.S)
    rows = m.group(1).split('\n'); rows[0], rows[1] = rows[1], rows[0]
    html = html[:m.start(1)] + '\n'.join(rows) + html[m.end(1):]
if MUT == 'type_roster':  html = html.replace("cats.push({ label: 'Tables'", "FILTER_TYPES.push({idx:999}); cats.push({ label: 'Tables'", 1)

def cstrings(block):
    return re.findall(r'"((?:[^"\\]|\\.)*)"', block)

# ── [1] the header vs the files on disk ──────────────────────────────────────────────────────
n_hdr = int(re.search(r'kFactoryTableCount = (\d+);', hdr).group(1))
rel_block = hdr[hdr.index('kFactoryTableRel[kFactoryTableCount] = {'):]
rel_block = rel_block[:rel_block.index('};')]
rels = cstrings(rel_block)
name_block = hdr[hdr.index('kFactoryTableName[kFactoryTableCount] = {'):]
name_block = name_block[:name_block.index('};')]
names = cstrings(name_block)

disk = sorted((os.path.basename(os.path.dirname(p)), os.path.splitext(os.path.basename(p))[0])
              for p in glob.glob(os.path.join(ROOT, 'Resources', 'Wavetables', '*', '*.flac')))
disk_sorted = sorted(disk, key=lambda r: (r[0].lower(), r[1].lower()))
chk(n_hdr == len(disk), '[1] the header counts every factory .flac on disk (%d)' % n_hdr,
    'header %d, disk %d' % (n_hdr, len(disk)))
chk(len(rels) == n_hdr and len(names) == n_hdr, '[1] and its three arrays are all that long',
    'rel %d, name %d' % (len(rels), len(names)))
chk(names == [r[1] for r in disk_sorted], '[1] names are the files, in the frozen (category, name) order')
chk(rels == ['%s/%s.flac' % r for r in disk_sorted], '[1] and each path resolves to the file it names')
chk(len(set(names)) == len(names), '[1] every name is unique (the choice list needs no qualifying)')

# ── [2] the UI list is the header's list ─────────────────────────────────────────────────────
m = re.search(r'window\.__factoryTables=\[(.*?)\];', html, re.S)
chk(m is not None, '[2] index.html carries window.__factoryTables')
if m:
    js = cstrings(m.group(1))
    chk(len(js) == n_hdr, '[2] with the same count (%d)' % len(js), 'js %d, header %d' % (len(js), n_hdr))
    chk(js == names, '[2] and the same names in the same order',
        next(('first difference at %d: js=%r header=%r' % (i, a, b)
              for i, (a, b) in enumerate(zip(js, names)) if a != b), 'length differs'))

# ── [3] the parameter appends them AFTER the built-in roster ─────────────────────────────────
chk('juce::StringArray tblRoster = wtRoster;' in cpp,
    '[3] the table parameter starts from the built-in roster (indices 0..N-1 unchanged)')
chk(re.search(r'for \(int i = 0; i < ParameterIDs::kFactoryTableCount; \+\+i\)\s*\n\s*tblRoster\.add', cpp) is not None,
    '[3] and APPENDS the factory library after it')
chk('tblRoster, defTable' in cpp, '[3] both filter slots use the grown roster')

# ── [4] the split is asked of the parameter, never guessed ───────────────────────────────────
chk('ch->choices.size() - ParameterIDs::kFactoryTableCount' in cpp,
    '[4] the built-in/factory split is derived from the parameter itself (fb373 law)')
chk('bakeFactoryTableGrid (preset - nBuiltIn' in cpp,
    '[4] and an index past the built-ins is baked from the factory library')

# ── [5] THE DESIGN DECISION: Tables are not filter TYPES ─────────────────────────────────────
chk("cats.push({ label: 'Tables'" in html, '[5] a Tables category exists in the filter browser')
chk('filterStTbl.setNormalisedValue' in html and 'setFilterTypeIdx(tblIdx)' in html,
    '[5] picking one sets the TABLE and the ENGINE together, in one click')
pushes = re.findall(r'FILTER_TYPES\.push\(', html)
chk(len(pushes) == 0, '[5] nothing appends to FILTER_TYPES — the type roster is NOT 500 entries longer',
    '%d push(es) found: a grown type list renumbers every saved automation lane' % len(pushes))

if MUT:
    print('\nMUTATION %r: gate went %s' % (MUT, 'RED' if _f else 'GREEN'))
    sys.exit(0 if _f else 1)
print('\n%d passed, %d failed' % (_p, _f))
sys.exit(1 if _f else 0)
