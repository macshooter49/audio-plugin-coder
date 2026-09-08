#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb439 — THE CHAIN-PATH GATE.  python3 Tests/fx_path_gate.py   (run from the plugin ROOT)
#
#  Max, fb439: "the audio is not getting to the devices... nothing is being affected visually
#  and I could also not hear it."  Four devices (Equalizer/Widen/Compress/Multiband) had ELEVEN
#  of their twelve integration touchpoints wired — params, refs, pools, prepare, per-block
#  setParams, route mask, entry gains, send bases, the apply branch, the viz emitter, the UI's
#  ACTIVE/RANK writes — and were still stone dead, because `rebuildChainOrder()` never emitted a
#  ChainEntry for kinds 9..12.  The `else if (ce.kind == 9) applyEqz(...)` line was UNREACHABLE.
#
#  🔑 THE LAW THIS ENFORCES (fb373, hardened): a green ENGINE harness never proves the plugin
#     REACHES the engine.  So gate the REACHING, structurally, for every kind at once:
#
#         if any code branches on `ce.kind == K`, then rebuildChainOrder() MUST be able to
#         produce a ChainEntry with kind K — else that branch is dead code by construction.
#
#  This is deliberately a WHOLE-MATRIX gate (fb425), not a per-device one: it costs the same to
#  check all thirteen kinds as one, and the next device (Bode, Utility) is covered the day its
#  apply branch lands, with no edit here.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import re, sys, io

SRC = 'Source/PluginProcessor.cpp'
HDR = 'Source/PluginProcessor.h'
src = io.open(SRC, encoding='utf-8').read()
hdr = io.open(HDR, encoding='utf-8').read()

npass = nfail = 0
def chk(ok, what, detail=''):
    global npass, nfail
    if ok: npass += 1; print('  ok   %s   %s' % (what, detail))
    else:  nfail += 1; print('  FAIL %s   %s' % (what, detail))

# ── 1. every kind the processing path branches on ──────────────────────────────────────────────
branched = sorted({int(m) for m in re.findall(r'ce\.kind\s*==\s*(\d+)', src)})
chk(len(branched) > 0, 'the chain dispatch branches on ce.kind', 'kinds seen: %s' % branched)

# ── 2. what rebuildChainOrder() can actually PRODUCE (brace-matched body, no regex guessing) ───
i = src.find('::rebuildChainOrder')
chk(i > 0, 'rebuildChainOrder() found in the processor', SRC)
b0 = src.find('{', i); depth = 0; b1 = b0
for j in range(b0, len(src)):
    if src[j] == '{': depth += 1
    elif src[j] == '}':
        depth -= 1
        if depth == 0: b1 = j; break
body = src[b0:b1]
produced = sorted({int(m) for m in re.findall(r'\badd(?:Fixed)?\s*\(\s*(\d+)\s*,', body)})
chk(len(produced) > 0, 'rebuildChainOrder() emits chain entries', 'kinds added: %s' % produced)

# ── 3. THE INVARIANT ───────────────────────────────────────────────────────────────────────────
dead = [k for k in branched if k not in produced]
NAMES = {0:'Reverb',1:'Delay',2:'Distortion',3:'Granular',4:'Tape',5:'Filter',6:'Chorus',
         7:'Flanger',8:'Phaser',9:'Equalizer',10:'Widen',11:'Compress',12:'Multiband'}
chk(not dead, 'EVERY branched kind is reachable from rebuildChainOrder (no dead apply branch)',
    'unreachable: %s' % ', '.join('%d=%s' % (k, NAMES.get(k, '?')) for k in dead) if dead else 'all %d kinds reachable' % len(branched))

# ── 4. the capacity bound must cover every kind that exists ────────────────────────────────────
m = re.search(r'kFxKinds\s*=\s*(\d+)', hdr)
kinds = int(m.group(1)) if m else -1
chk(kinds > max(branched), 'kFxKinds covers the highest kind (no silent >= kChainMax drop)',
    'kFxKinds=%d  highest kind=%d' % (kinds, max(branched)))

# ── 5. and each produced kind must have somewhere to go ────────────────────────────────────────
# kinds 0/1/2 are dispatched by the inst==1 fallback (`else applyDst`), so 2 legitimately has
# no `ce.kind == 2` branch of its own. Everything else must be branched on explicitly.
ELSE_FALLBACK = {2}
orphan = [k for k in produced if k not in branched and k not in ELSE_FALLBACK]
chk(not orphan, 'no kind is added to the chain without a branch to process it', 'orphans: %s' % orphan)

print('\n  PASS %d   FAIL %d\n' % (npass, nfail))
sys.exit(1 if nfail else 0)
