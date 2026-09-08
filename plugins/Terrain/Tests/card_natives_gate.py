#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  card_natives_gate.py — fb570: THE TWO NATIVE LISTS AGREE ON EVERYTHING THE CURVE EDITOR CALLS.
#
#    python3 Tests/card_natives_gate.py            # from the plugin root
#    CNG_MUTATE=drop python3 Tests/card_natives_gate.py   # proves it can fail
#
#  WHY. A popped card window is a SECOND WebView with ITS OWN native-function list
#  (PluginEditor.cpp, class TerrainCardWindow). A native registered only on the main editor's list
#  is not "missing" in the card — the call is dispatched, never completed, and the promise never
#  settles: no error, no toast, an empty field (fb328), a poll that leaks a promise every tick
#  (fb342), a curve capture that silently goes nowhere (fb570's warp guest before this gate).
#  That law has now bitten four times. This gate makes it structural: every native the curve
#  editor and its guests call from a card window must appear inside TerrainCardWindow's list.
#
#  The list of natives is the CURVE EDITOR'S OWN — read out of index.html (the editor module,
#  the warp host, the mod host, the boot, the doors), not typed here, so a fifth host that calls a
#  new native is caught the day it is written.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PE   = os.path.join(ROOT, 'Source', 'PluginEditor.cpp')
HTML = os.path.join(ROOT, 'Source', 'ui', 'public', 'index.html')
MUT  = os.environ.get('CNG_MUTATE', '')
pe   = open(PE, encoding='utf-8').read()
html = open(HTML, encoding='utf-8').read()

# ── the card window's list: from `class TerrainCardWindow` to its destructor ──
a = pe.find('class TerrainCardWindow')
b = pe.find('~TerrainCardWindow()', a)
assert a > 0 and b > a, 'TerrainCardWindow not found'
card_src = pe[a:b]
if MUT == 'drop':
    card_src = card_src.replace('.withNativeFunction ("getWarpCurve"', '.withNativeFunction ("getWarpCurve_MUTATED"', 1)
card = set(re.findall(r'withNativeFunction\s*\(\s*"([A-Za-z0-9_]+)"', card_src))
main = set(re.findall(r'withNativeFunction\s*\(\s*"([A-Za-z0-9_]+)"', pe[:a] + pe[b:]))

# ── the natives the curve editor + its guests + the card-only boot call (by name, from the page) ──
def region(start_marker, end_marker):
    s = html.find(start_marker); assert s > 0, start_marker
    e = html.find(end_marker, s); assert e > s, end_marker
    return html[s:e]
regions = {
  'editor module':  region('ONE editor, THREE HOSTS. A host owns exactly four things:', 'window.__crvUtil={'),
  'editor lanes':   region('window.__crvUtil={', '/* ════════'),
  'warp host':      region('function openWarpCurve (osc, slot, d, ev)', 'window.__openWarpFilterExt = openFilterExt;'),
  'mod host':       region('function closeCurveEditor(){', 'fb563 — THE CONTROL MENU'),
  'mod mirror':     region('var lastLocalEdit=0;', 'fb149 — FOREIGN-drag receiver'),
  'card boot':      region('var crvBootT=0', "if(document.readyState==='loading')"),
}
# natives the CARD-ONLY page itself needs (its boot/drag/dock chrome) — same list, read the same way
wanted = {}
for name, src in regions.items():
    for n in re.findall(r"NF\s*\(\s*'([A-Za-z0-9_]+)'\s*\)", src) + re.findall(r"getNativeFunction\s*\(\s*'([A-Za-z0-9_]+)'\s*\)", src):
        wanted.setdefault(n, set()).add(name)
# natives the MAIN VIEW ALONE may call (they act on the main window or its card map): never on a card
MAIN_ONLY = {'popOutCard', 'dragPoppedCard', 'getPoppedCards', 'retargetCard'}
# the setSynParam door used by both hosts
wanted.setdefault('setSynParam', set()).add('__setSynParam')

passed = failed = 0
def chk(ok, label, detail=''):
    global passed, failed
    if ok: passed += 1; print('  ok    ' + label + ('   ' + detail if detail else ''))
    else:  failed += 1; print('  FAIL  ' + label + ('   ' + detail if detail else ''))

print('\n══ fb570 — THE CARD WINDOW\'S NATIVE LIST CARRIES EVERYTHING THE CURVE EDITOR CALLS ══')
print('   card list: %d natives · main list: %d · editor calls: %d' % (len(card), len(main), len(wanted)))
chk(len(card) > 20 and len(main) > 100, '0  both lists were found and parsed', '%d / %d' % (len(card), len(main)))
for n in sorted(wanted):
    if n in MAIN_ONLY:
        chk(n in main, '   main-only  %-24s registered on the main list' % n, ', '.join(sorted(wanted[n])))
        continue
    chk(n in card, '   card       %-24s registered on the CARD list' % n, ', '.join(sorted(wanted[n])))
# the three fb570 additions by name, so a rename cannot hide a drop
for n in ('getWarpCurve', 'setWarpDrawCurve', 'getParamCardinality', 'getCardState', 'setCardState', 'getSynthMod', 'setSynthMod'):
    chk(n in card and n in main, '   both       %-24s on BOTH lists' % n)
print('\n══ RESULT: %d pass, %d FAIL ══\n' % (passed, failed))
sys.exit(1 if failed else 0)
