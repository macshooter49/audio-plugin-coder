#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb530 — THE HARMONIC-CEILING GATE.  python3 Tests/harmonic_ceiling_gate.py   (from plugin ROOT)
#
#  THE LAW IT ENFORCES:
#      A GENERATOR MAY NOT CARRY A HARMONIC-COUNT CEILING.
#      The harmonic loop always runs h = 1 … FrameSpec::kMaxHarmonics.
#      A harmonic may only become inaudible because the AMPLITUDE LAW put it there.
#
#  WHY. 25 of the 30 legacy generators stop their synthesis loop at a hard-coded integer — 24 of
#  them at ≤ 96, 16 at ≤ 64. It is invisible: the code compiles, the table renders, the waterfall
#  looks right, and the instrument is simply dull. Measured: "Prophet Saw" reads the same 24
#  harmonics at C1 THROUGH C5, with h24 at −29.31 dBc and h25 at −134.22 dBc — a 104.9 dB cliff
#  across ONE harmonic with Nyquist 22 kHz away. Only absent content can do that.
#
#  This gate does NOT retune the legacy 30 — they are frozen below exactly as they are, so an
#  existing patch never changes. It fails the build if a ceiling is ADDED anywhere: a new one in a
#  legacy generator, any at all in a TERRA generator, or the kernel's own loop bound turning into
#  a number. The frozen list is the debt, written down, and it can only ever shrink.
#
#  MUTATION CONTROL (fb421):
#     HCG_MUTATE=terra    give a Terra generator a hard-coded loop bound   → fails
#     HCG_MUTATE=kernel   replace the kernel's loop bound with a literal   → fails
#     HCG_MUTATE=legacy   add a NEW ceiling to a legacy generator          → fails
#     HCG_MUTATE=fixed    REMOVE a legacy ceiling (the debt shrinking)     → fails LOUDLY, asking
#                         you to update the frozen list — a silent shrink hides a sound change.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import io, re, sys, os

MUT = os.environ.get('HCG_MUTATE', '')
src = io.open('Source/Wavetable.h', encoding='utf-8').read()

npass = nfail = 0
def chk(ok, what, detail=''):
    global npass, nfail
    if ok: npass += 1; print('  ok    ' + what + ('   ' + detail if detail else ''))
    else:  nfail += 1; print('  FAIL  ' + what + ('   ' + detail if detail else ''))

def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    return re.sub(r'//[^\n]*', '', s)

# ── every generator body, by name ─────────────────────────────────────────────────────────────
gens = {}
for m in re.finditer(r'static WavetableSpec (make\w+Spec)\s*\(\)\s*\n\s*\{', src):
    name, i = m.group(1), m.end() - 1
    depth, j = 0, i
    while j < len(src):
        if src[j] == '{': depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0: break
        j += 1
    gens[name] = src[i:j + 1]

if MUT == 'terra':
    gens['makeTerraStackSpec'] += '\n for (int h = 1; h <= 64; ++h) { }\n'
if MUT == 'legacy':
    gens['makeSquareSpec'] += '\n fs.numHarmonics = 48;\n'
if MUT == 'fixed':
    gens['makeProphetSawSpec'] = 'X'

# ── what counts as a harmonic-COUNT ceiling ───────────────────────────────────────────────────
CEIL = [
    # (a) a for-loop bound that is a NUMBER
    re.compile(r'for\s*\([^;]*;\s*\w+\s*<=?\s*(\d+)\s*;'),
    # (b) the initialiser of any local whose NAME is a harmonic/partial count
    re.compile(r'\bint\s+(?:\w*(?:[nN][hH]|[nN]um[hH]|[mM]ax[hH]|[nN][hH]arm|[nN][pP]art|[hH][mM]ax)\w*|nP|numH|kNH|kMaxH|hMax)\s*=\s*([^;]+);'),
    # (c) a direct assignment to the spec's own count
    re.compile(r'num(?:Harmonics|Partials)\s*=\s*([^;]+);'),
]
# (d) a HAND-WRITTEN partial list. Its length IS the ceiling — the five metallic/keyboard tables
#     carry 5, 6, 6, 14 and 28 partials and can never carry one more.
PLIST = re.compile(r'(?:FrameSpec::Partial|\bstatic const [A-Z]{2}|\bconst [A-Z]{2})\s+\w+\s*\[\s*\]\s*=\s*\{')
def ceilings(body):
    b = strip_comments(body)
    out = []
    for rx in CEIL:
        for m in rx.finditer(b):
            for g in m.groups():
                if g is None: continue
                # kMaxHarmonics / kMaxPartials / a computed count are LEGAL; a NUMBER is the defect.
                g2 = re.sub(r'\bFrameSpec::kMax(?:Harmonics|Partials)\b', '', g)
                out += [int(x) for x in re.findall(r'(?<![\w.])(\d+)(?![\w.])', g2)]
    for m in PLIST.finditer(b):
        i = b.index('{', m.end() - 1); depth = 0; j = i; rows = 0
        while j < len(b):
            if b[j] == '{':
                depth += 1
                if depth == 2: rows += 1
            elif b[j] == '}':
                depth -= 1
                if depth == 0: break
            j += 1
        out.append(rows)
    return sorted(out)

# ── THE FROZEN DEBT — the 25 capped legacy generators, exactly as they ship today. ────────────
#    This list may SHRINK (with a deliberate edit here, so the sound change is announced). It may
#    never GROW, and no name may ever be ADDED to it.
FROZEN = {
    'makeBowedMetalSpec':          [6],
    'makeCS80BrassSpec':           [40, 80],
    'makeChoirAtoOSpec':           [64],
    'makeD50BellSpec':             [0, 14],
    'makeDustbowlSpec':            [48, 48, 48],
    'makeFormantRiseSpec':         [96, 96],
    'makeGlassHarmonicsSpec':      [5],
    'makeHarmonicRiseSpec':        [1],
    'makeHarmonicSeriesSpec':      [2, 2, 2, 32],
    'makeJunoStrSpec':             [30, 60],
    'makeJupiterPWMSpec':          [96],
    'makeM1PianoSpec':             [28],
    'makeMoogSqrSpec':             [32, 72],
    'makeOBXSawSpec':              [22, 60],
    'makeOddEvenSpec':             [16, 96],
    'makePPGWaveSpec':             [64],
    'makePhaseDriftSpec':          [64, 64, 64],
    'makeProphetSawSpec':          [24, 80],
    'makeRailroadSpec':            [6],
    'makeSerumHDSpec':             [96],
    'makeSineSpec':                [1, 1, 1, 24],
    'makeSpectralDriftSpec':       [32, 32],
    'makeStaticEvolveSpec':        [64],
    'makeVowelMorphSpec':          [64],
    'makeWhisperSpec':             [96],
}

new_offenders, grown, shrunk = [], [], []
for name, body in sorted(gens.items()):
    c = ceilings(body)
    base = FROZEN.get(name)
    if base is None:
        if c: new_offenders.append((name, c))
    else:
        if [x for x in c if x not in base] or len(c) > len(base): grown.append((name, base, c))
        elif c != base: shrunk.append((name, base, c))
for name in FROZEN:
    if name not in gens: shrunk.append((name, FROZEN[name], 'GENERATOR GONE'))

chk(not new_offenders, 'THE ONE LAW — no generator outside the frozen list carries a harmonic ceiling',
    ('OFFENDERS: ' + '; '.join('%s %s' % (n, c) for n, c in new_offenders)) if new_offenders else
    '%d generators clean (%d Terra)' % (len(gens) - len(FROZEN), sum(1 for g in gens if 'Terra' in g)))
chk(not grown, 'THE DEBT DID NOT GROW — no frozen generator gained a new ceiling',
    ('GREW: ' + '; '.join('%s %s -> %s' % t for t in grown)) if grown else '25 frozen, unchanged')
chk(not shrunk, 'THE FROZEN LIST IS ACCURATE — update it deliberately when a legacy table is fixed',
    ('CHANGED: ' + '; '.join('%s %s -> %s' % t for t in shrunk)) if shrunk else 'in sync')

# ── the kernel's own loop bound is the law, spelled out ───────────────────────────────────────
k = re.search(r'inline void renderFrame \(const FrameParams& P, FrameSpec& fs\).*?\n        \}', src, re.S)
kernel = strip_comments(k.group(0)) if k else ''
if MUT == 'kernel': kernel = kernel.replace('const int H = FrameSpec::kMaxHarmonics;', 'const int H = 96;')
chk(bool(k), 'THE KERNEL — terra::renderFrame exists (one loop, one bound, for every Terra table)')
chk('const int H = FrameSpec::kMaxHarmonics;' in kernel and 'for (int h = 1; h <= H; ++h)' in kernel,
    'THE KERNEL — its harmonic loop runs 1 … FrameSpec::kMaxHarmonics and nothing else',
    'bound: ' + (re.search(r'const int H = ([^;]+);', kernel).group(1) if re.search(r'const int H = ([^;]+);', kernel) else '??'))

# ── every Terra generator must route through the kernel, never hand-roll a loop ───────────────
terra = {n: b for n, b in gens.items() if 'Terra' in n}
notrouted = [n for n, b in terra.items() if not re.search(r'return terra::(build|buildStiff|buildFolded) \(', b)]
chk(terra and not notrouted, 'EVERY Terra generator returns terra::build / buildStiff / buildFolded',
    ('not routed: ' + ', '.join(notrouted)) if notrouted else '%d Terra generators' % len(terra))

# ── and the spec ceiling itself is the frame's true limit ─────────────────────────────────────
mh = re.search(r'static constexpr int kMaxHarmonics = (\d+);', src)
fs = re.search(r'static constexpr int kFrameSize\s*= (\d+);', src)
lad = re.search(r'kMipMaxHarmonics\s*\n\s*\{\s*(\d+)', src)
chk(mh and fs and int(mh.group(1)) <= int(fs.group(1)),
    'kMaxHarmonics fits the frame', 'kMaxHarmonics %s · kFrameSize %s' % (mh.group(1), fs.group(1)))
chk(lad and fs and int(lad.group(1)) <= int(fs.group(1)) // 2 - 1,
    'kMipMaxHarmonics[0] is at or under the representable ceiling (kFrameSize/2 − 1)',
    'ladder[0] %s · limit %d' % (lad.group(1), int(fs.group(1)) // 2 - 1))

print('\n%d passed, %d failed%s' % (npass, nfail, ('   [MUTATION ' + MUT + ']') if MUT else ''))
sys.exit(1 if nfail else 0)
