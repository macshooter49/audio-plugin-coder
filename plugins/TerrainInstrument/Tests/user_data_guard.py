#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  user_data_guard.py — fb602 · THE OWNER'S SAVED WORK, GUARDED BY A GATE INSTEAD OF A PROMISE.
#
#    python3 Tests/user_data_guard.py            # from plugins/TerrainInstrument
#    TI_DATA_MUT=1 python3 Tests/user_data_guard.py     # mutation control — must go RED
#
#  fb602 changed two things that decide WHERE the owner's presets live and WHAT FOLDER a card id
#  resolves to:
#
#   (d) tiPresetDir used to hand-build a macOS path out of $HOME:
#           userHomeDirectory / "Library/WavesCrate/TerrainInstrument/presets"
#       It now goes through terrainDataDir(), i.e. userApplicationDataDirectory. On macOS
#       userApplicationDataDirectory IS ~/Library (juce_Files_mac.mm:209) so the two spellings
#       coincide EXACTLY — which is why nobody noticed that on Windows the old spelling produced
#       C:\Users\<u>\Library\WavesCrate\… instead of %APPDATA%\WavesCrate\… (juce_Files_windows.cpp:721).
#       This gate's job on macOS is the boring one that matters: BYTE-IDENTICAL, or the owner's 29
#       card presets are stranded.
#
#   ·   tiCardSlug was accessor-unified and DELIBERATELY LEFT ALONE — fb132's exact character set,
#       retainCharacters("a-z"). A draft of fb602 widened it to toLowerCase()+[a-z0-9]; that
#       widening fixes 0 collisions (index.html:12204 already hand-maps 'Diode 1'/'Diode 2' to
#       diodeone/diodetwo) and MOVES one folder — cmp_fet 76: cmpfet -> cmpfet76. One stranded
#       folder to buy nothing is a bad trade, so it was reverted; widen it WITH a migration in the
#       commit that owns the preset browser. Bar [2] is what says whether that trade has changed:
#       it prints every id where the two spellings disagree and reds if any of them has a folder.
#
#  Everything below reads the REAL disk. No fixtures. Nothing here writes, moves or deletes.
#
#  MUTATION CONTROL: TI_DATA_MUT=1 re-roots terrainDataDir() at ~/Library/Application Support —
#  the single most plausible way to mis-read userApplicationDataDirectory, and the same CLASS of
#  mistake as bug (d) itself. Bars [2] and [3] must go RED with all five folders unreachable.
#  (An earlier draft mutated the SLUG back to a-z-only. It did not work as a control: the gate
#  compares old-slug against new-slug, so making them agree made the bar GREENER. Recorded here
#  because "the mutation made it pass" is the exact failure a mutation control exists to catch.)
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys, json, hashlib, xml.etree.ElementTree as ET

MUT   = os.environ.get('TI_DATA_MUT') == '1'
HOME  = os.path.expanduser('~')
ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INDEX = os.path.join(ROOT, 'Source', 'ui', 'public', 'index.html')
BACKUP = '/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/USER-PRESET-BACKUP-fb602'

CARD_DIR = os.path.join(HOME, 'Library', 'WavesCrate', 'TerrainInstrument', 'presets')
USER_XML = os.path.join(HOME, 'Library', 'Noizefield', 'Terrain Instrument', 'UserPresets.xml')

npass = nfail = 0
def chk(ok, label, detail):
    global npass, nfail
    if ok: npass += 1
    else:  nfail += 1
    print(f"  {'PASS' if ok else 'FAIL'}  {label}\n        {detail}")

AZ   = 'abcdefghijklmnopqrstuvwxyz'
AZ09 = AZ + '0123456789'
def slug_old(card):            # fb132: retainCharacters("a-z") — NO toLowerCase
    return ''.join(c for c in card if c in AZ)
def slug_new(card):            # fb602 SHIPPED: retainCharacters("a-z") — unchanged from fb132.
    return ''.join(c for c in card if c in AZ)
def slug_widened(card):        # the DRAFT that was reverted, kept so bar [2] can price the trade
    return ''.join(c for c in card.lower() if c in AZ09)

# ── the two path spellings, transcribed from the two versions of tiPresetDir ───────────────────
def dir_old(card):   # git show 5a2ba1c:…/PluginEditor.cpp:26-31
    return os.path.join(HOME, 'Library/WavesCrate/TerrainInstrument/presets', slug_old(card))
# ⚠️ MUTATION CONTROL. userApplicationDataDirectory is ~/Library on macOS
# (juce_Files_mac.mm:209) — NOT ~/Library/Application Support, which is
# userApplicationDataDirectory's Application Support CHILD and the single most likely way to
# mis-transcribe terrainDataDir(). TI_DATA_MUT=1 makes exactly that mistake, so the bars below
# have to be able to see a wrong ROOT, not just a wrong slug.
NEW_ROOT = [HOME, 'Library', 'Application Support'] if MUT else [HOME, 'Library']
def dir_new(card):   # working tree: terrainDataDir()/"presets"/tiCardSlug(card)
    return os.path.join(*NEW_ROOT, 'WavesCrate', 'TerrainInstrument', 'presets', slug_new(card))

# ── [2a] THIS GATE'S TRANSCRIPTION IS NOT ALLOWED TO DRIFT OFF THE SHIPPING SOURCE ───────────
#     slug_new/dir_new above are PYTHON copies of C++ that lives in PluginEditor.cpp, and a copy is
#     a second version of something that already exists. Tests/preset_path_cert.cpp is the
#     authority — it COMPILES the real terrainDataDir()/tiCardSlug() (sliced out by
#     Tests/extract_helpers.py) — so this gate's job is to stay honest about being a model: the bar
#     below reads the real function bodies and fails if they stop matching what is modelled here.
PE = os.path.join(ROOT, 'Source', 'PluginEditor.cpp')
_pe = open(PE, encoding='utf-8', errors='replace').read()
def _body(fn):
    m = re.search(r'^static\s+juce::\w+\s+' + fn + r'\s*\(', _pe, re.M)
    if not m: return ''
    i = _pe.index('{', m.end()); d = 0
    for j in range(i, len(_pe)):
        if _pe[j] == '{': d += 1
        elif _pe[j] == '}':
            d -= 1
            if d == 0: return _pe[i:j+1]
    return ''
_slug, _root = _body('tiCardSlug'), _body('terrainDataDir')
_pdir = _body('tiPresetDir')

print('\n══ fb602 · USER DATA GUARD — 29 CARD PRESETS AND 5 PATCHES THAT MUST NOT MOVE ══')
print(f'   MUTATION CONTROL TI_DATA_MUT={"1 (ACTIVE — [2] [3] MUST go RED)" if MUT else "0 (inactive)"}')
print(f'   card presets : {CARD_DIR}')
print(f'   patches      : {USER_XML}\n')

# ── [0] THE BACKUP IS REAL. Verify it, do not trust it. ───────────────────────────────────────
def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 16), b''): h.update(b)
    return h.hexdigest()

live_cards = {}
if os.path.isdir(CARD_DIR):
    for d, _, fs in os.walk(CARD_DIR):
        for fn in fs:
            if fn.endswith('.json'):
                p = os.path.join(d, fn)
                live_cards[os.path.relpath(p, CARD_DIR)] = sha(p)

bk_cards_dir = os.path.join(BACKUP, 'WavesCrate-TerrainInstrument', 'presets')
bk_cards = {}
if os.path.isdir(bk_cards_dir):
    for d, _, fs in os.walk(bk_cards_dir):
        for fn in fs:
            if fn.endswith('.json'):
                p = os.path.join(d, fn)
                bk_cards[os.path.relpath(p, bk_cards_dir)] = sha(p)

bk_xml = os.path.join(BACKUP, 'Noizefield-Terrain-Instrument', 'UserPresets.xml')
xml_ok = os.path.isfile(bk_xml) and os.path.isfile(USER_XML) and sha(bk_xml) == sha(USER_XML)
missing = sorted(set(live_cards) - set(bk_cards))
drifted = sorted(k for k in set(live_cards) & set(bk_cards) if live_cards[k] != bk_cards[k])
chk(len(live_cards) > 0 and not missing and not drifted and xml_ok,
    '[0] THE BACKUP COVERS EVERYTHING LIVE, SHA-256 FOR SHA-256',
    f'{len(live_cards)} live card presets · {len(bk_cards)} backed up · not-backed-up: {missing or "none"} · '
    f'content drift: {drifted or "none"} · UserPresets.xml identical: {xml_ok}')

# ── [1] THE COUNT. 29 card presets and 5 patches, on the real disk, right now. ────────────────
try:
    tree = ET.parse(USER_XML); names = [p.get('name') for p in tree.getroot().findall('Preset')]
except Exception as e:
    names = []; print(f'        !! UserPresets.xml did not parse: {e}')
EXPECT = ['Monte Cristo', 'Slatt', 'One Shot Killer', 'This Is Scary', 'M.I.A.']
chk(len(live_cards) == 29 and names == EXPECT,
    '[1] THE OWNER\'S DATA IS ALL THERE — 29 card presets, 5 named patches',
    f'card presets: {len(live_cards)} (expected 29) · patches: {names}')

# ── [2] PATH IDENTITY. Every id that has a folder today must land on the SAME folder. ────────
#     The id set is not invented: it is read out of index.html.
src = open(INDEX, encoding='utf-8', errors='replace').read()
card_ids = sorted(set(re.findall(r"presets\(\{\s*id:'([A-Za-z0-9_]+)'", src)))
_ns = re.search(r'var FX_PRESET_NS=\{[^}]*\}', src)
prefixes = sorted(set(re.findall(r"[A-Za-z0-9_]+\s*:\s*'([A-Za-z0-9]+_)'", _ns.group(0)))) if _ns else []
types = sorted({t.strip().strip("'") for m in re.findall(r"types:\[([^\]]*)\]", src) for t in m.split(',')})
overrides = ['diodeone', 'diodetwo', 'all']            # index.html:12204 + the flt 'all' collapse
fx_ids = sorted({p + s for p in prefixes for s in [t.lower() for t in types] + overrides})
all_ids = sorted(set(card_ids) | set(fx_ids))

disagree = [i for i in all_ids if dir_old(i) != dir_new(i)]
# what the reverted widening WOULD have cost, priced against the real disk, every run
would_move = sorted({(slug_new(i), slug_widened(i)) for i in all_ids if slug_new(i) != slug_widened(i)})
would_strand = sorted({a for a, b in would_move if os.path.isdir(os.path.join(CARD_DIR, a))})
on_disk  = sorted(os.listdir(CARD_DIR)) if os.path.isdir(CARD_DIR) else []
on_disk  = [d for d in on_disk if os.path.isdir(os.path.join(CARD_DIR, d))]
# A folder is STRANDED if an id that reaches a REAL directory today lands somewhere else now.
# Compared as FULL PATHS against the real disk — a basename comparison cannot see a wrong root,
# which is the whole of bug (d).
stranded = sorted({dir_old(i) for i in all_ids
                   if os.path.isdir(dir_old(i)) and dir_new(i) != dir_old(i)})
chk(not stranded,
    '[2] NO EXISTING FOLDER IS STRANDED by the root change, and the reverted widening is PRICED',
    f'{len(all_ids)} card ids read out of index.html ({len(card_ids)} extension cards + {len(prefixes)} FX '
    f'prefixes x {len(types)+len(overrides)} slugs) · folders on disk: {on_disk} · '
    f'\n        ⚠️ that id set is a deliberate SUPERSET: every prefix is crossed with every type '
    f'table, so it contains ids the page cannot build (there is no bod "Diode 1"). '
    f'Tests/preset_path_cert.cpp enumerates the 122 REAL ones. A superset finding nothing '
    f'stranded is the stronger statement; the counts below are of that superset. · '
    f'ids where old!=new: {len(disagree)}{" " + str(disagree[:6]) if disagree else ""} · '
    f'STRANDED: {stranded or "none"}\n'
    f'        the REVERTED widening (toLowerCase+[a-z0-9]) would move {len(would_move)} slug(s) '
    f'{[a + " -> " + b for a, b in would_move[:4]] or "none"}{" ..." if len(would_move) > 4 else ""} and strand {would_strand or "none"} — '
    f'that is the trade, priced against the real disk on every run, not asserted once in a comment')

drift = []
if 'toLowerCase' in _slug or 'abcdefghijklmnopqrstuvwxyz0123456789' in _slug \
   or 'abcdefghijklmnopqrstuvwxyz' not in _slug:
    drift.append('tiCardSlug is no longer plain retainCharacters("a-z") — if it was widened on '
                 'purpose, slug_new() here and Tests/preset_path_cert.cpp must move WITH a migration')
if 'userApplicationDataDirectory' not in _root or '"WavesCrate"' not in _root or '"TerrainInstrument"' not in _root:
    drift.append('terrainDataDir is no longer userAppData/WavesCrate/TerrainInstrument')
if 'terrainDataDir' not in _pdir or '"presets"' not in _pdir or 'tiCardSlug' not in _pdir:
    drift.append('tiPresetDir no longer = terrainDataDir()/presets/tiCardSlug(card)')
chk(not drift and _slug and _root and _pdir,
    "[2a] THE PYTHON MODEL ABOVE STILL MATCHES THE SHIPPING C++ (the authority is preset_path_cert.cpp)",
    f'read tiCardSlug ({len(_slug)} chars), terrainDataDir ({len(_root)}), tiPresetDir ({len(_pdir)}) '
    f'out of Source/PluginEditor.cpp · drift: {drift or "none"}\n'
    f'        Tests/preset_path_cert.cpp COMPILES those three bodies verbatim; this bar exists so a '
    f'change to them cannot leave this gate quietly modelling last week\'s rule.')

# ── [3] BYTE-IDENTICAL, for the folders that actually hold the owner's files ─────────────────
reached = {}
for i in all_ids:
    p = dir_new(i)
    if os.path.isdir(p): reached.setdefault(os.path.basename(p), []).append(i)
unreachable = [d for d in on_disk if d not in reached]
same = all(dir_old(i) == dir_new(i) for d in reached for i in reached[d])
chk(same and not unreachable and len(reached) == len(on_disk),
    '[3] EVERY POPULATED FOLDER IS STILL REACHED, AND BY THE IDENTICAL STRING',
    f'reached: { {k: v[:2] for k, v in sorted(reached.items())} } · '
    f'unreachable folders: {unreachable or "none"} · old-path == new-path for all of them: {same}\n'
    f'        new root -> {os.path.join(*NEW_ROOT, "WavesCrate", "TerrainInstrument", "presets")}'
    f'   (isdir={os.path.isdir(os.path.join(*NEW_ROOT, "WavesCrate", "TerrainInstrument", "presets"))})\n'
    f'        example: id {sorted(reached)[0] if reached else "-"} -> '
    f'{dir_new(reached[sorted(reached)[0]][0]) if reached else "(NOTHING REACHES ANY REAL FOLDER)"}')

# ── [4] THEY ALL STILL LOAD. Parse every one, off the real disk. ─────────────────────────────
bad = []
for rel in sorted(live_cards):
    p = os.path.join(CARD_DIR, rel)
    try:
        with open(p, encoding='utf-8') as f: j = json.load(f)
        if not isinstance(j, (dict, list)) or (isinstance(j, dict) and not j): bad.append((rel, 'empty/!object'))
    except Exception as e: bad.append((rel, str(e)[:60]))
chk(not bad and len(live_cards) == 29,
    '[4] ALL 29 CARD PRESETS PARSE AS JSON off the real disk',
    f'ok: {len(live_cards) - len(bad)}/{len(live_cards)} · bad: {bad or "none"}\n'
    f'        by folder: { {d: len([r for r in live_cards if r.startswith(d + os.sep)]) for d in on_disk} }')

# ── [5] AND SO DO THE 5 PATCHES, attribute for attribute. ───────────────────────────────────
try:
    root = ET.parse(USER_XML).getroot()
    rows = [(p.get('name'), len(p.attrib)) for p in root.findall('Preset')]
    thin = [r for r in rows if r[1] < 20]
    chk(len(rows) == 5 and not thin and [r[0] for r in rows] == EXPECT,
        '[5] ALL 5 UserPresets.xml PATCHES LOAD, WITH THEIR PARAMETERS INTACT',
        f'{rows} · suspiciously thin: {thin or "none"} · root tag <{root.tag}>')
except Exception as e:
    chk(False, '[5] ALL 5 UserPresets.xml PATCHES LOAD, WITH THEIR PARAMETERS INTACT', f'PARSE FAILED: {e}')

# ── [6] THE WINDOWS HALF OF BUG (d), stated symbolically because this lane cannot run it ────
print('  ....  [6] WINDOWS (report — cannot be executed on this machine)')
print('        old: userHomeDirectory + "Library/WavesCrate/…"  ->  C:\\Users\\<u>\\Library\\WavesCrate\\…')
print('        new: userApplicationDataDirectory + "WavesCrate/…" ->  C:\\Users\\<u>\\AppData\\Roaming\\WavesCrate\\…')
print('        On macOS both spellings resolve to ~/Library, which is why bars [2] and [3] can be green')
print('        here and the Windows bug still be real. Nothing in this gate proves the Windows path.')

print(f'\n  {npass} passed, {nfail} FAILED\n')
sys.exit(1 if nfail else 0)
