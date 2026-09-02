#!/usr/bin/env python3
"""fb552 — A MOD SOURCE HAS TO SURVIVE FOUR DOORS, AND THREE OF THEM FAIL SILENTLY.

Adding a matrix source touches: the enum, the wire codes, the JS encoder, the JS decoder, the
torn-off card's receiver, and `setSynthModMatrix`'s accepted-source test. fb552 wired five new
sources through every one of those and STILL shipped nothing, because the last one — the
validator — drops an unrecognised source with a bare `continue`. The drag worked, the underline
drew, the patch saved, and the audio never moved. MEASURED with wire codes either side of the
door: s=211 gave r=+0.910 between source and destination envelopes; s=209 and s=215 gave +0.12,
identical to no route at all.

This gate checks the four places agree. It cannot check that the DSP is right — that is what the
audio measurement is for — only that a source the UI can emit is a source the processor accepts.
"""
import re, sys, pathlib, os
root = pathlib.Path(__file__).resolve().parent.parent
cfg  = (root / 'Source' / 'SynthModConfig.h').read_text()
proc = pathlib.Path(os.environ.get('MSG_PROC') or (root / 'Source' / 'PluginProcessor.cpp')).read_text()
import os
js   = pathlib.Path(os.environ.get('MSG_JS') or (root / 'Source' / 'ui' / 'public' / 'index.html')).read_text()
fails = []

def const(name, default=None):
    m = re.search(r'constexpr int %s\s*=\s*(\d+)' % name, cfg)
    if m: return int(m.group(1))
    if default is not None: return default
    fails.append("SynthModConfig.h has no `%s`" % name); return -1

ENV = const('kEnvSrcBase'); VEL = const('kVelSrc'); NOTE = const('kNoteSrc')
FOL = const('kFollowSrcBase'); NFOL = const('kNumFollowers')

# ── 1 · THE DOOR. setSynthModMatrix must name every family. ─────────────────────────────────
m = re.search(r'setSynthModMatrix \(const juce::String& json\)\s*\{(.{0,12000}?)synModJson\s*=', proc, re.S)   # the DEFINITION, and a body that has grown past 4000 chars
door = m.group(1) if m else ''
if not door: fails.append("could not find setSynthModMatrix's body")
for fam, tok in (('LFO', 'NUM_LFOS'), ('Env', 'kEnvSrcBase'), ('Velocity', 'kVelSrc'),
                 ('Follower', 'kFollowSrcBase'), ('Key', 'kNoteSrc')):
    if tok not in door:
        fails.append("the setSynthModMatrix door never mentions %s (%s) — every route from that "
                     "family is dropped by the bare `continue`, silently" % (fam, tok))
gate = re.search(r'if \(! *lfoSrc[^;]*\)\s*continue;', door)
if gate:
    for fam in ('folSrc', 'velSrc', 'envSrc', 'notSrc'):
        if fam not in gate.group(0):
            fails.append("the door's reject test omits `%s`" % fam)

# ── 2 · THE JS CODEC (fb563). ONE table, ONE encoder, and the table must equal the C++. ─────
#  Three hand-written decoders had drifted apart (restore() stopped at `sv<215` while the C++ had
#  seven followers; the popped card special-cased 200 and turned a Key drop into Velocity). Now
#  the wire codes live in one JS table and this gate reads it AGAINST SynthModConfig.h.
wt = re.search(r'var WIRE=\{([^}]*)\}', js)
if not wt: fails.append("could not find the JS WIRE table (`var WIRE={...}`)")
else:
    tbl = dict((k.strip(), int(v)) for k, v in re.findall(r'(\w+)\s*:\s*(\d+)', wt.group(1)))
    PAIRS = (('ENV', 'kEnvSrcBase'), ('VEL', 'kVelSrc'), ('NOTE', 'kNoteSrc'), ('FOL', 'kFollowSrcBase'), ('NFOL', 'kNumFollowers'),
             ('MACRO', 'kMacroSrcBase'), ('NMACRO', 'kNumMacros'), ('WHEEL', 'kWheelSrc'), ('AT', 'kAftertouchSrc'),
             ('BEND', 'kBendSrc'), ('RAND', 'kRandSrcBase'), ('NRAND', 'kNumRands'), ('ALT', 'kAltSrc'))
    for jk, ck in PAIRS:
        want = const(ck)
        if jk not in tbl: fails.append("the JS WIRE table has no %s (C++ %s = %d) — the UI cannot express that family" % (jk, ck, want))
        elif tbl[jk] != want:
            fails.append("JS WIRE.%s = %d but C++ %s = %d — the UI and the processor disagree about a wire code" % (jk, tbl[jk], ck, want))
enc = re.search(r'function encodeSrc\(a\)\{ return (.*?); \}', js)
if not enc: fails.append("could not find the JS wire encoder (encodeSrc)")
else:
    for tok in ('WIRE.FOL', 'WIRE.VEL', 'WIRE.ENV', 'WIRE.NOTE', 'WIRE.MACRO', 'WIRE.WHEEL', 'WIRE.AT', 'WIRE.BEND', 'WIRE.RAND', 'WIRE.ALT'):
        if tok not in enc.group(1):
            fails.append("encodeSrc never emits %s — the UI cannot express that source" % tok)
if 's:encodeSrc(a)' not in js.replace(' ', ''):
    fails.append("push() does not encode through encodeSrc — a second encoder is a second place to drift")

# ── 3 · THE JS DECODER. Every family bounded, and restore() must use it. ────────────────────
dec = re.search(r'function decodeSrc\(sv\)\{(.*?)return null; \}', js, re.S)
if not dec: fails.append("could not find the JS wire decoder (decodeSrc)")
else:
    d = dec.group(1).replace(' ', '')
    for bound, why in (('sv<WIRE.FOL+WIRE.NFOL', 'a seventh follower would decode as an LFO (the fb556 hole)'),
                       ('sv<WIRE.ENV+32', 'a later wire code would decode as an envelope (the fb261 bug)'),
                       ('sv<10', 'an unknown code would decode as an LFO'),
                       ('sv<WIRE.MACRO+WIRE.NMACRO', 'a ninth macro code would be invented'),
                       ('sv<WIRE.RAND+WIRE.NRAND', 'a fifth random code would be invented')):
        if bound not in d: fails.append("decodeSrc is missing the bound `%s` — %s" % (bound, why))
if 'var S=decodeSrc(sv);' not in js:
    fails.append("restore() does not decode through decodeSrc — a second decoder is a second place to drift")

# ── 4 · THE TORN-OFF CARD. Every code from kVelSrc up is streamed AS ITSELF. ─────────────────
card = re.search(r'var sv=\(wire>=(\d+)\)\?wire:\(wire-1\)', js)
if not card:
    fails.append("could not find the popped card's wire pass-through (`var sv=(wire>=N)?wire:(wire-1)`)")
elif int(card.group(1)) != VEL:
    fails.append("the popped card passes wire codes through from %d, not from kVelSrc (%d): Velocity, Key or the "
                 "followers would be off by one" % (int(card.group(1)), VEL))

# ── 4b · fb563 PHASE 2 — the C++ sites. One helper (phase2SourceForWire) must name every constant,
#          and the door, the per-voice build and the block loop must all ask it. ─────────────────
helper = re.search(r'inline int phase2SourceForWire \(int wire\) noexcept\s*\{(.*?)\n\}', cfg, re.S)
if not helper: fails.append("SynthModConfig.h has no phase2SourceForWire()")
else:
    for tok in ('kMacroSrcBase', 'kWheelSrc', 'kAftertouchSrc', 'kBendSrc', 'kRandSrcBase', 'kAltSrc'):
        if tok not in helper.group(1): fails.append("phase2SourceForWire() never maps %s — that family is dropped at every C++ door" % tok)
if 'phase2SourceForWire (r.src)' not in door:
    fails.append("the setSynthModMatrix door never asks phase2SourceForWire — macros, wheel, aftertouch, bend, random and alt are dropped silently")
if proc.count('phase2SourceForWire (r.src)') < 3:
    fails.append("phase2SourceForWire is asked at %d site(s); the door, the per-voice build and the block loop make 3" % proc.count('phase2SourceForWire (r.src)'))
sv_ = (root / 'Source' / 'SynthVoice.h').read_text()
for tok in ('wc::isMacroModSource (sI)', 'wc::ModSource::Wheel', 'wc::ModSource::Aftertouch', 'wc::ModSource::Bend', 'wc::isRandModSource (sI)', 'wc::ModSource::Alt'):
    if tok not in sv_: fails.append("SynthVoice never evaluates %s — declared and inert (the fb555 shape)" % tok)

# ── 4c · fb565 — MACROS ARE DESTINATIONS. The page stamps data-mod-dest from ONE number; it must equal
#          the C++ MacroDest1, and the global pass must actually apply the range. ─────────────────
mjs  = re.search(r'window\.__MACRO_DEST\s*=\s*(\d+)', js)
mcpp = re.search(r'\(int\) ModDest::MacroDest1 == (\d+)', cfg)
if not mjs:  fails.append("index.html has no window.__MACRO_DEST — the Macros view cannot stamp its destinations")
if not mcpp: fails.append("SynthModConfig.h has no static_assert pinning ModDest::MacroDest1")
if mjs and mcpp and int(mjs.group(1)) != int(mcpp.group(1)):
    fails.append("JS __MACRO_DEST = %s but C++ MacroDest1 = %s — a route into a macro would land on the wrong destination" % (mjs.group(1), mcpp.group(1)))
if 'wc::ModDest::MacroDest1 + k' not in proc:
    fails.append("PluginProcessor.cpp never applies MacroDest1..9 in the global pass — a route into a macro is silently nothing")
nm = re.search(r'kNumMacros = (\d+)', cfg)
if nm and int(nm.group(1)) != 9: fails.append("kNumMacros is %s, Max asked for nine (3×3)" % nm.group(1))

# ── 4d · fb566 — THE RACK'S WALK KNOWS EVERY FAMILY. buildFxMod asks one by-source reader (the same
#          one the global pass and Scale by use) and never drops a family with a bare `continue`. ──
fxv = (root / 'Source' / 'FxModValue.h').read_text()
if 'si >= NUM_LFOS) continue' in fxv:
    fails.append("FxModValue.h still drops every non-LFO, non-envelope source (`si >= NUM_LFOS) continue`) — a macro into a rack knob is silent")
for tok in ('isShapeModSource (sI)', 'applyModCurve (curves', 'sourceTo01 ((int) as.auxSource'):
    if tok not in fxv: fails.append("FxModValue.h's buildFxMod is missing `%s` — the rack evaluates a law the other two evaluators have" % tok)
if 'sourceValueOfSrc (sI, ok)' not in proc:
    fails.append("PluginProcessor.cpp's rack call does not hand buildFxMod sourceValueOfSrc — a second reader is a second place to drift")

# ── 5 · fb554 · THE CONNECTION CURVE MUST ROUND-TRIP ────────────────────────────────────────
#  It rides the route JSON, so the encoder must emit `c` and the decoder must read it back. If
#  only one side knows, a drawn curve is silently lost on the next reload — which is the same
#  silent class as everything else this gate exists for.
# the ASSIGNMENT, guarded by the curve's own presence — `if(false) o.c=a.curve...` must still red
pw = js.find('s:encodeSrc(a)'); enc_win = js[pw:pw + 400].replace(' ', '') if pw >= 0 else ''
if pw >= 0 and 'if(a.curve)o.c=a.curve' not in enc_win:
    fails.append("the JS encoder never writes the connection curve (`c`) — a drawn curve would not survive a save")
if 'a.curve=String(o.c)' not in js.replace(' ', ''):
    fails.append("the JS decoder never reads the connection curve back (`o.c`)")
# the exact CALL, not the substring: `applyModCurveX` contains `applyModCurve` and would pass
# fb555 — Note must be EVALUATED, not merely declared. It sat in the enum unread for a whole
#  arc: no evaluator, no wire code, no UI. A source nobody can reach is not a source.
sv = (root / 'Source' / 'SynthVoice.h').read_text()
if 'wc::isNoteModSource (sI)' not in sv:
    fails.append("SynthVoice never evaluates ModSource::Note — it would be declared and inert, which is how it shipped for an entire arc")
if 'wc::applyModCurve (' not in sv:
    fails.append("the per-voice evaluator never applies the connection curve")
if 'wc::applyModCurve (' not in proc:
    fails.append("the processor's global pass never applies the connection curve")

if fails:
    print("  ❌ mod source gate"); [print("     " + f) for f in fails]; sys.exit(1)
print("  ✅ mod sources agree across the door, the encoder, the decoder and the card receiver"
      "  (LFO 0.. · Env %d.. · Vel %d · Follow %d..%d)" % (ENV, VEL, FOL, FOL + NFOL - 1))
