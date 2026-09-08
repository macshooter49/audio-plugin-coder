#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  identity_lock_gate.py — fb605 · THE PLUGIN'S IDENTITY IS PINNED, SO THE RENAME CAN NEVER
#                                  COST MAX A SINGLE SAVED SESSION.
#
#    python3 Tests/identity_lock_gate.py            # from plugins/Terrain
#    TI_ID_MUT=cmake|cid|au|fx python3 Tests/identity_lock_gate.py     # mutation controls, RED
#
#  ── WHY THIS EXISTS ──────────────────────────────────────────────────────────────────────────
#  fb605 renamed the product: "Terrain Instrument" -> **Terrain**, and the old "Terrain" ->
#  **Terrain FX**. Directories moved, CMake targets moved, bundle ids moved, every user-visible
#  string moved. NONE of that is what a DAW uses to find a plugin in a saved project:
#
#    · A VST3 host stores the **class UID** of the processor it instantiated. JUCE derives that
#      UID from the MANUFACTURER code and the PLUGIN code — the plugin's NAME is not in it. That
#      is not folklore, it is bar [3] below: the low 8 bytes of every CID here decode, byte for
#      byte, to the ASCII of `Wvcr` + `Tern` (and `Wvcr` + `Trrn` for the FX).
#    · An AU host stores the **type / subtype / manufacturer** quad — `aumu Tern Wvcr`. Again,
#      no name.
#
#  So the rename was safe. But "was safe" is a fact about ONE commit. The thing that makes it
#  permanently safe is a gate that fails the moment anybody edits PLUGIN_CODE,
#  PLUGIN_MANUFACTURER_CODE, or anything else that moves a class UID — because the failure mode
#  is invisible: the build goes green, the plugin installs, and then EVERY project Max ever saved
#  opens with a missing plugin and a silent channel. There is no error message for that. This
#  gate is the error message.
#
#  ── WHAT IT READS ────────────────────────────────────────────────────────────────────────────
#  Nothing is trusted twice. The frozen constants below are the ONLY hardcoded values; every
#  other number is read off disk:
#    · plugins/Terrain/CMakeLists.txt   and  plugins/TerrainFX/CMakeLists.txt  (the declaration)
#    · build/.../Terrain.vst3/Contents/Resources/moduleinfo.json               (the ARTEFACT)
#    · build/.../Terrain.component/Contents/Info.plist                         (the ARTEFACT)
#    · the two INSTALLED bundles, when they are installed                      (what Max loads)
#    · EVERY *.component in the Components folder, for a second bundle claiming our quad
#  Declaration AND artefact, on purpose: editing the CMakeLists without rebuilding, or shipping a
#  stale artefact whose codes no longer match the source, are both real ways to end up lying.
#
#  ── WHAT IT DOES NOT DO ──────────────────────────────────────────────────────────────────────
#  It reads. It never writes, moves, installs or deletes anything.
#
#  ── MUTATION CONTROLS (a green bar that cannot go red is not a gate) ─────────────────────────
#    TI_ID_MUT=cmake  the CMakeLists is read as if PLUGIN_CODE had been edited to `Terp`
#                     -> [0] RED (frozen code) and [4] RED (source/artefact drift)
#    TI_ID_MUT=cid    the built VST3's CID is read with its last byte changed
#                     -> [2] RED (frozen CID) and [3] RED (the code no longer decodes out of it)
#    TI_ID_MUT=au     the built AU's subtype is read as `Terp`
#                     -> [4] RED
#    TI_ID_MUT=fx     the FX's PLUGIN_CODE is read as `Trrx`
#                     -> [1] RED, [5] RED, [6] RED
#  Each mutation is a thing a person could plausibly TYPE — a code retyped to match a new name is
#  exactly the edit this gate exists to stop.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys, json, plistlib

MUT  = os.environ.get('TI_ID_MUT', '')
HERE = os.path.dirname(os.path.abspath(__file__))
PLUG = os.path.dirname(HERE)                      # plugins/Terrain
REPO = os.path.dirname(os.path.dirname(PLUG))     # the repo root
BUILD = os.environ.get('TERRAIN_BUILD_DIR') or os.path.join(REPO, 'build')
HOME = os.path.expanduser('~')

# ── THE FROZEN IDENTITY. Every one of these is written into files Max already has. ────────────
#    Changing a value here is not "updating a test" — it is deciding that his saved projects may
#    lose their plugin. Do not touch these without a migration and his say-so.
SYNTH = dict(
    target   = 'Terrain',                 # fb605: was TerrainInstrument
    product  = 'Terrain',                 # fb605: was "Terrain Instrument"
    bundleid = 'com.wavescrate.terrain',  # fb605: was com.wavescrate.terraininstrument
    mfr      = 'Wvcr',
    code     = 'Tern',
    au_type  = 'aumu',
    cids     = ['ABCDEF019182FAEB577663725465726E',    # Audio Module Class
                'ABCDEF011234ABCD577663725465726E'],   # Component Controller Class
)
FX = dict(
    target   = 'TerrainFX',                 # fb605: was Terrain
    product  = 'Terrain FX',                # fb605: was "Terrain"
    bundleid = 'com.wavescrate.terrainfx',
    mfr      = 'Wvcr',
    code     = 'Trrn',
    au_type  = 'aufx',
    cids     = ['ABCDEF019182FAEB577663725472726E',
                'ABCDEF011234ABCD577663725472726E'],
)

npass = nfail = 0
def chk(ok, label, detail=''):
    global npass, nfail
    if ok: npass += 1
    else:  nfail += 1
    print(f"  {'PASS' if ok else 'FAIL'}  {label}")
    if detail: print(f"        {detail}")

def note(msg):
    print(f"        {msg}")

# ── readers ───────────────────────────────────────────────────────────────────────────────────
def read_cmake(path, mutate_code=None):
    """PLUGIN_CODE / PLUGIN_MANUFACTURER_CODE / BUNDLE_ID / PRODUCT_NAME, as DECLARED."""
    txt = open(path, encoding='utf-8').read()
    def one(key, quoted=False):
        m = re.search(r'^\s*' + key + r'\s+' + (r'"([^"]+)"' if quoted else r'(\S+)'),
                      txt, re.M)
        return m.group(1) if m else None
    out = dict(code=one('PLUGIN_CODE'), mfr=one('PLUGIN_MANUFACTURER_CODE'),
               bundleid=one('BUNDLE_ID', True), product=one('PRODUCT_NAME', True))
    if mutate_code: out['code'] = mutate_code
    return out

def read_moduleinfo(path, mutate_cid=False):
    """VST3 moduleinfo.json is JSON5-flavoured (trailing commas). Strip them, then parse."""
    txt = open(path, encoding='utf-8').read()
    txt = re.sub(r',(\s*[}\]])', r'\1', txt)
    d = json.loads(txt)
    cids = [c['CID'] for c in d.get('Classes', [])]
    if mutate_cid and cids:
        cids[0] = cids[0][:-1] + ('0' if cids[0][-1] != '0' else '1')
    return d.get('Name'), cids

def read_au(path, mutate_sub=None):
    with open(path, 'rb') as f:
        pl = plistlib.load(f)
    ac = (pl.get('AudioComponents') or [{}])[0]
    sub = ac.get('subtype')
    if mutate_sub: sub = mutate_sub
    return dict(type=ac.get('type'), subtype=sub, manufacturer=ac.get('manufacturer'),
                name=pl.get('CFBundleName'), bundleid=pl.get('CFBundleIdentifier'),
                exe=pl.get('CFBundleExecutable'))

def cid_tail_ascii(cid):
    """The low 8 bytes of a JUCE-derived VST3 class UID, as ASCII. This is the MECHANISM."""
    try:    return bytes.fromhex(cid[-16:]).decode('ascii')
    except Exception: return '<not ascii>'

def artefact(target, product, kind):
    base = os.path.join(BUILD, 'plugins', target, f'{target}_artefacts', 'Release')
    if kind == 'vst3':
        return os.path.join(base, 'VST3', f'{product}.vst3', 'Contents', 'Resources', 'moduleinfo.json')
    return os.path.join(base, 'AU', f'{product}.component', 'Contents', 'Info.plist')

# ══ run ═══════════════════════════════════════════════════════════════════════════════════════
print()
print('══ fb605 IDENTITY LOCK ══'
      f"   TI_ID_MUT={MUT or '(none - expect GREEN)'}   build={BUILD}")
print('   The rename is safe only while these do not move. Nothing below is typed twice:')
print('   the frozen block at the top of this file is the only hardcoded identity.\n')

# ── [0] the SYNTH's declared codes ────────────────────────────────────────────────────────────
cm_s = read_cmake(os.path.join(PLUG, 'CMakeLists.txt'),
                  mutate_code='Terp' if MUT == 'cmake' else None)
ok = cm_s['code'] == SYNTH['code'] and cm_s['mfr'] == SYNTH['mfr']
chk(ok, '[0]  plugins/Terrain/CMakeLists.txt still declares the FROZEN synth codes',
    f"PLUGIN_CODE={cm_s['code']} (frozen {SYNTH['code']}) · "
    f"PLUGIN_MANUFACTURER_CODE={cm_s['mfr']} (frozen {SYNTH['mfr']})")
note(f"PRODUCT_NAME={cm_s['product']!r} · BUNDLE_ID={cm_s['bundleid']!r}  <- fb605 CHANGED these, "
     "and that is the point: they are presentation, the codes are identity")

# ── [1] the FX's declared codes ───────────────────────────────────────────────────────────────
cm_f = read_cmake(os.path.join(REPO, 'plugins', 'TerrainFX', 'CMakeLists.txt'),
                  mutate_code='Trrx' if MUT == 'fx' else None)
ok = cm_f['code'] == FX['code'] and cm_f['mfr'] == FX['mfr']
chk(ok, '[1]  plugins/TerrainFX/CMakeLists.txt still declares the FROZEN FX codes',
    f"PLUGIN_CODE={cm_f['code']} (frozen {FX['code']}) · "
    f"PLUGIN_MANUFACTURER_CODE={cm_f['mfr']} (frozen {FX['mfr']})")
note(f"PRODUCT_NAME={cm_f['product']!r} · BUNDLE_ID={cm_f['bundleid']!r}")

# ── [2] the BUILT synth VST3's class UIDs ─────────────────────────────────────────────────────
mi = artefact(SYNTH['target'], SYNTH['product'], 'vst3')
if not os.path.isfile(mi):
    chk(False, '[2]  the built synth VST3 carries the FROZEN class UIDs',
        f"NOT BUILT — no {mi}\n        build it first:  cmake --build build --target Terrain_VST3 --target Terrain_AU")
    s_cids = []
else:
    s_name, s_cids = read_moduleinfo(mi, mutate_cid=(MUT == 'cid'))
    ok = s_cids == SYNTH['cids']
    chk(ok, '[2]  the built synth VST3 carries the FROZEN class UIDs — every saved project keys on these',
        ' · '.join(s_cids) if s_cids else '(no classes)')
    if not ok: note('frozen: ' + ' · '.join(SYNTH['cids']))
    note(f"moduleinfo Name={s_name!r}  <- fb605 renamed the NAME; the UIDs above did not move")

# ── [3] THE MECHANISM: the UID is a function of the CODES, not of the name ────────────────────
want_s, want_f = SYNTH['mfr'] + SYNTH['code'], FX['mfr'] + FX['code']
mi_f = artefact(FX['target'], FX['product'], 'vst3')
f_name, f_cids = (read_moduleinfo(mi_f) if os.path.isfile(mi_f) else (None, []))
tails = [(c, cid_tail_ascii(c), want_s) for c in s_cids] + \
        [(c, cid_tail_ascii(c), want_f) for c in f_cids]
ok = bool(tails) and all(t == w for _, t, w in tails)
chk(ok, '[3]  each class UID DECODES to <manufacturer><plugin code> — measured, not assumed',
    '  '.join(f"{c[-16:]} -> {t!r} (want {w!r})" for c, t, w in tails) if tails
    else 'NOT BUILT — nothing to decode')
note('this is WHY the rename was safe: the plugin NAME is nowhere in the UID, so renaming '
     'cannot move it — and changing a code CANNOT NOT move it.')

# ── [4] the BUILT synth AU quad ───────────────────────────────────────────────────────────────
ap = artefact(SYNTH['target'], SYNTH['product'], 'au')
if not os.path.isfile(ap):
    chk(False, '[4]  the built synth AU reports aumu / Tern / Wvcr', f'NOT BUILT — no {ap}')
else:
    au = read_au(ap, mutate_sub='Terp' if MUT == 'au' else None)
    ok = (au['type'] == SYNTH['au_type'] and au['subtype'] == SYNTH['code']
          and au['manufacturer'] == SYNTH['mfr'] and au['subtype'] == cm_s['code'])
    chk(ok, "[4]  the built synth AU reports aumu / Tern / Wvcr — and AGREES with the CMakeLists",
        f"type={au['type']} subtype={au['subtype']} manufacturer={au['manufacturer']}"
        f"   (frozen {SYNTH['au_type']} / {SYNTH['code']} / {SYNTH['mfr']}"
        f", declared {cm_s['code']})")
    note(f"CFBundleName={au['name']!r} · CFBundleExecutable={au['exe']!r} · "
         f"CFBundleIdentifier={au['bundleid']!r}  <- fb605 renamed all three; auval still "
         f"finds it by 'auval -v aumu Tern Wvcr'")

# ── [5] the BUILT FX VST3 ─────────────────────────────────────────────────────────────────────
if not os.path.isfile(mi_f):
    chk(False, '[5]  the built FX VST3 carries the FROZEN class UIDs', f'NOT BUILT — no {mi_f}')
else:
    fc = list(f_cids)
    if MUT == 'fx': fc[0] = fc[0][:-2] + '78'      # ...Trrn -> ...Trrx, as the code change would
    ok = fc == FX['cids']
    chk(ok, '[5]  the built FX VST3 carries the FROZEN class UIDs', ' · '.join(fc))
    if not ok: note('frozen: ' + ' · '.join(FX['cids']))
    note(f"moduleinfo Name={f_name!r}  <- fb605 renamed it from 'Terrain' to 'Terrain FX'")

# ── [6] the BUILT FX AU quad ──────────────────────────────────────────────────────────────────
ap_f = artefact(FX['target'], FX['product'], 'au')
if not os.path.isfile(ap_f):
    chk(False, '[6]  the built FX AU reports aufx / Trrn / Wvcr', f'NOT BUILT — no {ap_f}')
else:
    auf = read_au(ap_f, mutate_sub='Trrx' if MUT == 'fx' else None)
    ok = (auf['type'] == FX['au_type'] and auf['subtype'] == FX['code']
          and auf['manufacturer'] == FX['mfr'] and auf['subtype'] == cm_f['code'])
    chk(ok, '[6]  the built FX AU reports aufx / Trrn / Wvcr — and AGREES with its CMakeLists',
        f"type={auf['type']} subtype={auf['subtype']} manufacturer={auf['manufacturer']}"
        f"   (frozen {FX['au_type']} / {FX['code']} / {FX['mfr']}, declared {cm_f['code']})")

# ── [7] the INSTALLED bundles — what Max's DAW actually opens ─────────────────────────────────
#     The build tree proves what we MADE. This proves what is on his machine under the names the
#     rename now claims. A bundle that is not installed cannot be checked: it says so LOUDLY and
#     counts as a skip, never as a pass — a silent skip here is the exact class of failure this
#     gate exists to prevent.
#     TERRAIN_COMPONENTS_DIR re-points this bar at another Components folder (used to prove the
#     bar can go GREEN against freshly built bundles without touching the real one).
COMP = os.environ.get('TERRAIN_COMPONENTS_DIR') or \
       os.path.join(HOME, 'Library', 'Audio', 'Plug-Ins', 'Components')
rows, skipped = [], []
for spec in (SYNTH, FX):
    bundle = f"{spec['product']}.component"
    p = os.path.join(COMP, bundle, 'Contents', 'Info.plist')
    if not os.path.isfile(p):
        skipped.append(bundle); continue
    a = read_au(p)
    good = (a['type'] == spec['au_type'] and a['subtype'] == spec['code']
            and a['manufacturer'] == spec['mfr'])
    rows.append((bundle, spec, a, good))
if rows:
    ok = all(g for *_, g in rows)
    chk(ok, '[7]  the INSTALLED AU(s) report the frozen quad — this is what a saved session resolves',
        f'in {COMP}')
    for b, spec, a, good in rows:
        note(f"{'ok  ' if good else 'WRONG'}  {b}: {a['type']}/{a['subtype']}/{a['manufacturer']}"
             + ('' if good else f"  — expected {spec['au_type']}/{spec['code']}/{spec['mfr']}"
                                f", so this bundle is NOT {spec['product']}"))
else:
    chk(False, '[7]  the INSTALLED AU(s) report the frozen quad',
        f'NEITHER IS INSTALLED under {COMP} — this bar did NOT run. Install and re-run; do not '
        'read the summary below as if it had.')
if skipped:
    note('NOT INSTALLED, so NOT checked (said out loud rather than skipped in silence): '
         + ', '.join(skipped))

# ── [8] NOBODY ELSE CLAIMS OUR IDENTITY ───────────────────────────────────────────────────────
#     The rename creates a brand-new way to lose a session, and it is not a code change at all:
#     a bundle under the OLD name is still installed, still carries the SAME quad, and now sits
#     beside the new one. Two bundles, one identity — the host picks one and you do not get to say
#     which. That is how a "fixed" plugin keeps loading last month's binary with nobody lying
#     anywhere. Measured over the whole Components folder, not just our two names.
import glob
by_quad = {}
for p in sorted(glob.glob(os.path.join(COMP, '*.component'))):
    ip = os.path.join(p, 'Contents', 'Info.plist')
    try:    a = read_au(ip)
    except Exception: continue
    q = (a['type'], a['subtype'], a['manufacturer'])
    if q in {(SYNTH['au_type'], SYNTH['code'], SYNTH['mfr']),
             (FX['au_type'], FX['code'], FX['mfr'])}:
        by_quad.setdefault(q, []).append(os.path.basename(p))
dupes = {q: v for q, v in by_quad.items() if len(v) > 1}
chk(not dupes,
    '[8]  exactly ONE installed bundle claims each frozen quad — no coin-flip about which one loads',
    f'in {COMP}')
for q, v in sorted(by_quad.items()):
    note(f"{'/'.join(q)}  ->  " + ', '.join(v) + ('   ⚠️ AMBIGUOUS' if len(v) > 1 else ''))
if dupes:
    note('the stale bundle must be DELETED, not left beside the new one: a saved session resolves '
         'by quad, and both answer to it.')
if not by_quad:
    note('no bundle installed under either quad — nothing to be ambiguous about yet.')

print(f"\n══ RESULT: {npass} pass, {nfail} FAIL ══")
if MUT and nfail == 0:
    print("   ⚠️  A MUTATION WAS SET AND EVERY BAR STILL PASSED. That is a broken gate, not a "
          "healthy plugin.")
    sys.exit(2)
print()
sys.exit(1 if nfail else 0)
