#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  tp31 — THE VIZ-SPECTRUM FEED GATE.   python3 Tests/viz_feed_gate.py   (run from the plugin ROOT)
#
#  Max: "I'm playing something and there isn't a background filter visualizer. Some presets don't
#  have it and some do."  The filter card's white spectrum and the rack's spectrum cards were
#  drawing SILENCE on every patch that routes an oscillator into the FX rack, because their feed
#  (analyzerPre/analyzerPost) is tapped inside the master-EQ block — upstream of the rack, and
#  since tp30 upstream of the FLOW cards — while a routed oscillator LEAVES the main mix outright
#  (SynthVoice exKeep_, tp12). MEASURED in Tests/au_viz_feed.cpp: -240 dB, nothing passes.
#
#  🔑 THE LAW THIS ENFORCES, the fb439 shape applied to a data path instead of a device:
#        a viz feed is only alive if EVERY link exists — the analyzer, its prepare, its tap at the
#        FINAL buffer, the editor's push, and the page's read. Any one missing is a dead picture
#        with a fully green build, which is exactly how this shipped.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import re, sys, io

PROC_H, PROC, ED, PAGE = 'Source/PluginProcessor.h', 'Source/PluginProcessor.cpp', 'Source/PluginEditor.cpp', 'Source/ui/public/index.html'
proc_h = io.open(PROC_H, encoding='utf-8').read()
proc   = io.open(PROC,   encoding='utf-8').read()
ed     = io.open(ED,     encoding='utf-8').read()
page   = io.open(PAGE,   encoding='utf-8').read()

npass = nfail = 0
def chk(ok, what, detail=''):
    global npass, nfail
    if ok: npass += 1; print('  ok   %s   %s' % (what, detail))
    else:  nfail += 1; print('  FAIL %s   %s' % (what, detail))

print('\n══ tp31 · THE VIZ-SPECTRUM FEED GATE ══\n')

# ── 1. the analyzer exists and is prepared ────────────────────────────────────────────────────
chk('SpectrumAnalyzer analyzerOut;' in proc_h, '[1] analyzerOut is declared', PROC_H)
chk('analyzerOut.prepare (' in proc, '[2] analyzerOut is prepared (message thread, may allocate)', PROC)

# ── 2. it is fed from the FINAL buffer — after the FLOW dispatch, before the auditions ────────
i_feed  = proc.find('analyzerOut.pushSample')
i_flow  = proc.find('if (flowAnyRouted_)')          # tp30 — the flow cards' deferred dispatch
i_ring  = proc.find('writeToMasterFxRing (leftChannel')
chk(i_feed > 0, '[3] analyzerOut is fed at all', 'pushSample found' if i_feed > 0 else 'NO TAP — the analyzer would be silent forever')
chk(i_flow > 0 < i_ring, '[4] the flow dispatch and the masterFx ring are both found', 'flow@%d ring@%d' % (i_flow, i_ring))
chk(i_flow < i_feed < i_ring,
    '[5] the tap is AFTER the FLOW dispatch and at the masterFx ring — i.e. the final master',
    'flow %d < feed %d < ring %d' % (i_flow, i_feed, i_ring))
# and the two analyzers that bracket the master EQ must still be where they were
chk(proc.find('analyzerPre.pushSample') < i_feed and proc.find('analyzerPost.pushSample') < i_feed,
    '[6] pre/post still tap the master EQ, untouched — the EQ panel keeps its honest pair')

# ── 3. the editor transforms and pushes it, under a condition a consumer can satisfy ──────────
chk('analyzerOut.update()' in ed, '[7] the editor runs analyzerOut\'s transform')
chk('analyzerOut.readLatest()' in ed, '[8] the editor reads its latest frame')
chk(re.search(r'AP\s*\(\s*"out:\[', ed) is not None, '[9] the push carries an out:[] array')
m = re.search(r'needOut\s*=\s*\((.*?)\);', ed, re.S)
cond = m.group(1) if m else ''
chk(m is not None and 'pg == 1' in cond and 'pg == 5' in cond and 'fltExtOpen_' in cond,
    '[10] out is pushed on the pages that show it (synth, Patcher, the floating filter)',
    cond.strip()[:90])
chk('eqPushSeqOut_' in ed, '[11] its freshness is tracked separately (an idle analyzer never holds the frame)')

# ── 4. the page reads it ──────────────────────────────────────────────────────────────────────
chk(re.search(r'd\s*&&\s*d\.out', page) is not None or 'd.out' in page,
    '[12] the filter/rack spectrum reads d.out')
chk('d.out)?d.out:(d?d.post:null)' in page.replace(' ', ''),
    '[13] ...and falls back to d.post, so an EQ-panel-only frame never blanks the card')
chk('if (data.pre && data.post)' in page,
    '[14] the master EQ panel ignores a frame that carries no pair instead of blanking to undefined')

print('\n  PASS %d   FAIL %d\n' % (npass, nfail))
sys.exit(1 if nfail else 0)
