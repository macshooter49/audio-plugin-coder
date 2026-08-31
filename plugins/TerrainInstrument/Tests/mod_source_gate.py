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
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
cfg  = (root / 'Source' / 'SynthModConfig.h').read_text()
proc = (root / 'Source' / 'PluginProcessor.cpp').read_text()
js   = (root / 'Source' / 'ui' / 'public' / 'index.html').read_text()
fails = []

def const(name, default=None):
    m = re.search(r'constexpr int %s\s*=\s*(\d+)' % name, cfg)
    if m: return int(m.group(1))
    if default is not None: return default
    fails.append("SynthModConfig.h has no `%s`" % name); return -1

ENV = const('kEnvSrcBase'); VEL = const('kVelSrc')
FOL = const('kFollowSrcBase'); NFOL = const('kNumFollowers')

# ── 1 · THE DOOR. setSynthModMatrix must name every family. ─────────────────────────────────
m = re.search(r'setSynthModMatrix[^{]*\{(.{0,4000}?)synModJson\s*=', proc, re.S)
door = m.group(1) if m else ''
if not door: fails.append("could not find setSynthModMatrix's body")
for fam, tok in (('LFO', 'NUM_LFOS'), ('Env', 'kEnvSrcBase'),
                 ('Velocity', 'kVelSrc'), ('Follower', 'kFollowSrcBase')):
    if tok not in door:
        fails.append("the setSynthModMatrix door never mentions %s (%s) — every route from that "
                     "family is dropped by the bare `continue`, silently" % (fam, tok))
gate = re.search(r'if \(! *lfoSrc[^;]*\)\s*continue;', door)
if gate:
    for fam in ('folSrc', 'velSrc', 'envSrc'):
        if fam not in gate.group(0):
            fails.append("the door's reject test omits `%s`" % fam)

# ── 2 · THE JS ENCODER. Every family's base constant must appear. ────────────────────────────
enc = re.search(r'var arr=assigns\.map\(function\(a\)\{return \{s:\((.*?)\),d:a\.dest', js)
if not enc: fails.append("could not find the JS wire encoder")
else:
    e = enc.group(1)
    for val, fam in ((FOL, 'follower'), (VEL, 'velocity'), (ENV, 'envelope')):
        if str(val) not in e:
            fails.append("the JS encoder never emits the %s base (%d) — the UI cannot express that source" % (fam, val))

# ── 3 · THE JS DECODER. The env test needs an UPPER bound or it swallows every later family. ─
dec = re.search(r'var vel=.{0,200}?env=.{0,200}?lfo=.{0,80}?;', js, re.S)
if not dec: fails.append("could not find the JS wire decoder")
elif ('sv<%d' % VEL) not in dec.group(0).replace(' ', ''):
    fails.append("the JS decoder's env test has no upper bound (`sv<%d`): wire code %d decodes as "
                 "'Env %d' instead of a follower — the fb261 bug, one family later"
                 % (VEL, FOL, FOL - ENV + 1))

# ── 4 · THE TORN-OFF CARD. It must not subtract 1 from a follower's wire code. ───────────────
card = re.search(r'var sv=.{0,200}?dv=.{0,80}?;', js, re.S)
if not card: fails.append("could not find the popped card's wire receiver")
elif ('wire>=%d' % FOL) not in card.group(0).replace(' ', ''):
    fails.append("the popped card's receiver does not pass follower wire codes through; `wire-1` "
                 "would write %d..%d — one rejected outright, the rest pointing at the wrong source"
                 % (FOL - 1, FOL + NFOL - 2))

if fails:
    print("  ❌ mod source gate"); [print("     " + f) for f in fails]; sys.exit(1)
print("  ✅ mod sources agree across the door, the encoder, the decoder and the card receiver"
      "  (LFO 0.. · Env %d.. · Vel %d · Follow %d..%d)" % (ENV, VEL, FOL, FOL + NFOL - 1))
