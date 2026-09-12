#!/usr/bin/env python3
"""pool_prebuild_gate.py — fb636 bugA, the paths an AU host cannot reach.

pool_send_first_block_au.cpp proves the state-load and prepareToPlay paths by ear. The UI pill click
(both setSynParam natives) and a learned CC (applyPendingMidiCc) cannot be driven from an AU host, so
this reads the source: on each of those paths a pooled pill's pair must be built (prebuildPoolSend)
BEFORE the parameter is written (setValueNotifyingHost), or the audio thread can see the lit pill with
no pair behind it — the send missing and, in insert mode, the routed oscillator gone.

    python3 Tests/pool_prebuild_gate.py            # gate
    POOL_PREBUILD_MUT=editor python3 ...          # control: the main native writes first  -> red
    POOL_PREBUILD_MUT=cc     python3 ...          # control: the CC path writes first      -> red
    POOL_PREBUILD_MUT=state  python3 ...          # control: the tree build after replaceState -> red
    POOL_PREBUILD_MUT=engine python3 ...          # control: setState builds ahead of a tape/granular/reverb engine -> red
    POOL_PREBUILD_MUT=timer  python3 ...          # control: the timer builds its pairs before the engines again -> red
    POOL_PREBUILD_MUT=want   python3 ...          # control: the push raises the engine want AFTER the mask -> red

Bar [8] is the other half: a message-side build must NEVER put a pair ahead of a timer-built ENGINE
(reverb 2-6, granular, tape). Until that engine exists its slot passes the routed oscillator through
UNPROCESSED — measured +10.0 dB over steady state on Max's "Damaged Tapes" when the filter was missing.

Bar [9] (fb636 review) is the same law on the TIMER's own path: the tick used to build every wanted pair and
only then the granular/tape engines, so a pair reached by host automation (or handed back to the timer by
dropSendsAwaitingEngine) could publish while its engine was still being built. The tick now drops the sends
awaiting an engine from its first build and builds the same snapshot in full after the engine builders.

Bar [10] (fb636 review, second pass) closes the hole [9] left: the tick's engine builders act on tpeWantBuild_ /
grnWantBuild_, which applyTpe/applyGrn raised only in the rack, LATER in the block than the push that release-stores
poolWantMask_. A tick in between saw the mask, no want, built no engine — and its full-snapshot build published the
pair anyway. The push now raises the want (applyTpe/applyGrn's own condition: POWER > 0.5 and the send lit, for the
instances in the chain) BEFORE the mask stores, so the release-store publishes both.
"""
import os, re, sys

HERE = os.path.dirname (os.path.abspath (__file__))
SRC  = os.path.join (HERE, "..", "Source")
MUT  = os.environ.get ("POOL_PREBUILD_MUT", "")
passed = failed = 0

def chk (ok, label, detail):
    global passed, failed
    if ok: passed += 1
    else:  failed += 1
    print ("  %s  %s\n        %s" % ("PASS" if ok else "FAIL", label, detail))

def strip_comments (s):
    s = re.sub (r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub (r"//[^\n]*", "", s)

def body_of (src, signature):
    """The brace-balanced body that follows `signature` (comments already stripped)."""
    at = src.find (signature)
    if at < 0: return ""
    b = src.find ("{", at)
    depth = 0
    for i in range (b, len (src)):
        if src[i] == "{": depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0: return src[b:i + 1]
    return ""

def before (text, a, b):
    ia, ib = text.find (a), text.find (b)
    return ia >= 0 and ib >= 0 and ia < ib, ia, ib

ed  = strip_comments (open (os.path.join (SRC, "PluginEditor.cpp")).read())
pp  = strip_comments (open (os.path.join (SRC, "PluginProcessor.cpp")).read())

if MUT == "editor":   # the main view's native writes the parameter first
    ed = re.sub (r"audioProcessor\.prebuildPoolSend \(p->getParameterIndex\(\), v\);\s*\n(\s*)p->setValueNotifyingHost \(v\);",
                 r"p->setValueNotifyingHost (v);\n\1audioProcessor.prebuildPoolSend (p->getParameterIndex(), v);", ed, count=1)
if MUT == "cc":
    pp = re.sub (r"prebuildPoolSend \(idx, v\);\s*\n(\s*)params\[idx\]->setValueNotifyingHost \(v\);",
                 r"params[idx]->setValueNotifyingHost (v);\n\1prebuildPoolSend (idx, v);", pp, count=1)
if MUT == "engine":   # the tree build forgets the engine filter
    pp = pp.replace ("wantedPoolMaskTree (newState, m0, m1); dropSendsAwaitingEngine (m0, m1);", "wantedPoolMaskTree (newState, m0, m1);", 1)
if MUT == "timer":    # the pre-review tick: every wanted pair first, the granular/tape engines after
    pp = re.sub (r"\{ juce::uint64 m0 = poolWant0, m1 = poolWant1; dropSendsAwaitingEngine \(m0, m1\); buildPoolPairsLocked \(m0, m1\); \}",
                 "buildPoolPairsLocked (poolWant0, poolWant1);", pp, count=1)
    tcm = pp.find ("void TerrainAudioProcessor::timerCallback()")
    tpe = pp.find ("buildPendingTapeEngines();", tcm)
    sec = pp.find ("buildPoolPairsLocked (poolWant0, poolWant1);", tpe)
    if tcm >= 0 and tpe >= 0 and sec >= 0: pp = pp[:sec] + pp[sec + len ("buildPoolPairsLocked (poolWant0, poolWant1);"):]
if MUT == "want":     # the engine want raised AFTER the mask release-stores (the ordering the review found)
    a = pp.find ("{ const int nW = juce::jmin (chainCount_")
    if a >= 0:
        depth = 0
        for i in range (a, len (pp)):
            if pp[i] == "{": depth += 1
            elif pp[i] == "}":
                depth -= 1
                if depth == 0: blk = pp[a:i + 1]; pp = pp[:a] + pp[i + 1:]; break
        anchor = "poolWantMask_[1].store (m1, std::memory_order_release);"
        k = pp.find (anchor)
        if k >= 0: pp = pp[:k + len (anchor)] + "\n" + blk + pp[k + len (anchor):]
if MUT == "state":
    pp = re.sub (r"(\{ const std::lock_guard<std::mutex> g \(prepLock_\);\s*juce::uint64 m0 = 0, m1 = 0; wantedPoolMaskTree \(newState, m0, m1\);[^}]*\})\s*(apvts\.replaceState \(newState\);)",
                 r"\2\n\1", pp, count=1)

# [1] both setSynParam natives
natives = [m.start() for m in re.finditer (r'withNativeFunction\s*\(\s*"setSynParam"', ed)]
chk (len (natives) == 2, "[1] the editor has exactly the two setSynParam natives this gate knows",
     "found %d (main view + popped card)" % len (natives))
for n, at in enumerate (natives):
    nxt = ed.find ("withNativeFunction", at + 10)
    seg = ed[at:nxt if nxt > 0 else len (ed)]
    ok, ia, ib = before (seg, "prebuildPoolSend (", "setValueNotifyingHost (")
    chk (ok, "[2.%d] setSynParam native #%d builds the pooled pair BEFORE it writes the parameter" % (n + 1, n + 1),
         "prebuildPoolSend at +%d, setValueNotifyingHost at +%d" % (ia, ib))

# [3] learned CCs
cc = body_of (pp, "void TerrainAudioProcessor::applyPendingMidiCc()")
ok, ia, ib = before (cc, "prebuildPoolSend (idx, v)", "setValueNotifyingHost (v)")
chk (ok, "[3] applyPendingMidiCc builds a learned pill's pair BEFORE it writes the parameter", "prebuild at +%d, write at +%d" % (ia, ib))
tc = body_of (pp, "void TerrainAudioProcessor::timerCallback()")
ok, ia, ib = before (tc, "applyPendingMidiCc();", "prepGuard (prepLock_)")
chk (ok, "[3b] ... and it still runs BEFORE timerCallback takes prepLock_ (prebuildPoolSend takes it; std::mutex is not recursive)",
     "applyPendingMidiCc at +%d, prepGuard at +%d" % (ia, ib))

# [4] setStateInformation: tree prediction before replaceState, live net after
ss = body_of (pp, "void TerrainAudioProcessor::setStateInformation (const void* data, int sizeInBytes)")
rs = ss.find ("apvts.replaceState (newState);")
it, il = ss.find ("wantedPoolMaskTree (newState"), ss.find ("wantedPoolMaskLive (")
chk (rs > 0 and 0 < it < rs and il > rs, "[4] setStateInformation builds from the TREE before replaceState and from the LIVE pills after it",
     "tree at +%d, replaceState at +%d, live at +%d" % (it, rs, il))

# [5] prepareToPlay: after the caches, under its prepGuard
pr = body_of (pp, "void TerrainAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)")
g, c, b = pr.find ("prepGuard (prepLock_)"), pr.find ("cacheSendRefs();"), pr.find ("buildPoolPairsLocked (")
chk (0 < g < c < b, "[5] prepareToPlay builds the lit pairs after cacheSendRefs, under its prepGuard", "guard +%d, cacheSendRefs +%d, build +%d" % (g, c, b))

# [6] the timer stays the fallback, and the audio thread never builds
# fb636 review — the tick now snapshots poolWantMask_ once (poolWant0/1) and builds that snapshot; see [9]
snap = re.search (r"poolWant0 = poolWantMask_\[0\]\.load \([^)]*\),\s*poolWant1 = poolWantMask_\[1\]\.load", tc)
chk (snap is not None and "buildPoolPairsLocked (poolWant0, poolWant1);" in tc,
     "[6] the timer still builds from poolWantMask_ (host automation, Splitter lanes)",
     "timerCallback -> poolWant0/1 = poolWantMask_ -> buildPoolPairsLocked (poolWant0, poolWant1): %s" % ("found" if snap else "MISSING"))
pb = body_of (pp, "void TerrainAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)")
bad = [t for t in ("buildPoolPairsLocked", "prebuildPoolSend", "buildPoolFilters", "prepLock_") if t in pb]
chk (len (pb) > 1000 and not bad, "[7] processBlock never builds a pair or takes prepLock_", "processBlock body %d chars; forbidden tokens: %s" % (len (pb), bad or "none"))

# [8] every MESSAGE-side build filters out the sends still waiting on a timer-built engine (the timer: see [9])
sites = [("setStateInformation", ss), ("prepareToPlay", pr),
         ("prebuildPoolSend", body_of (pp, "void TerrainAudioProcessor::prebuildPoolSend (int paramIndex, float newValue01)"))]
bad = []
for name, body in sites:
    for m in re.finditer (r"buildPoolPairsLocked \(", body):
        if "dropSendsAwaitingEngine (" not in body[max (0, m.start() - 160):m.start()]:
            bad.append ("%s@+%d" % (name, m.start()))
nbuilds = sum (len (re.findall (r"buildPoolPairsLocked \(", b)) for _, b in sites)
chk (nbuilds == 4 and not bad,
     "[8] every message-side build (setState x2, prepareToPlay, prebuildPoolSend) drops sends awaiting a timer-built engine",
     "%d message-side builds, unfiltered: %s" % (nbuilds, bad or "none"))

# [9] fb636 review — the TIMER keeps a pair behind its engine: its builds before the granular/tape engine builders
# drop the sends awaiting an engine; the full snapshot is built only after both builders have run.
ig, it_ = tc.find ("buildPendingGranularEngines();"), tc.find ("buildPendingTapeEngines();")
early = [m.start() for m in re.finditer (r"buildPoolPairsLocked \(", tc) if m.start() < min (ig, it_)]
late  = [m.start() for m in re.finditer (r"buildPoolPairsLocked \(", tc) if m.start() > max (ig, it_)]
unf   = [a for a in early if "dropSendsAwaitingEngine (" not in tc[max (0, a - 160):a]]
chk (ig > 0 and it_ > 0 and early and not unf and any (tc.startswith ("buildPoolPairsLocked (poolWant0, poolWant1);", a) for a in late),
     "[9] the timer builds its pairs BEHIND the granular/tape engines: filtered before the builders, the full snapshot after",
     "early builds %d (unfiltered %d), engine builders at +%d/+%d, full-snapshot builds after them %d" % (len (early), len (unf), ig, it_, len (late)))

# [10] fb636 review — THE ENGINE WANT TRAVELS WITH THE MASK. In processBlock's fb631 push the tape/granular engine
# wants are stored BEFORE the poolWantMask_ release-stores (the timer's acquire load then sees them), on applyTpe /
# applyGrn's condition (POWER > 0.5, the send lit), and the push is the only place the mask is stored.
pu = pb.find ("auto& R = routeSnap_;")
mk = pb.find ("poolWantMask_[0].store (m0, std::memory_order_release);")
tw_, gw_ = pb.find ("tpeWantBuild_[(size_t) i0].store (true", max (pu, 0)), pb.find ("grnWantBuild_[(size_t) i0].store (true", max (pu, 0))
seg = pb[pu:mk] if 0 <= pu < mk else ""
cond = [t for t in ("grnRefs_[(size_t) i0].power", "tpeRefs_[(size_t) i0].power", "pw->load() > 0.5f",
                    "poolRouteAny_[(size_t) (kGrnSendBase + i0)]", "poolRouteAny_[(size_t) (kTpeSendBase + i0)]") if t not in seg]
nmask = len (re.findall (r"poolWantMask_\[[01]\]\.store \(", pp))
chk (pu > 0 and 0 < tw_ < mk and 0 < gw_ < mk and not cond and nmask == 2,
     "[10] the push raises the tape/granular ENGINE WANT before the poolWantMask_ release-store publishes the mask",
     "push at +%d, tpe want at +%d, grn want at +%d, mask store at +%d; condition tokens missing: %s; mask stores in the source: %d (want 2)"
     % (pu, tw_, gw_, mk, cond or "none", nmask))

print ("\n  %d passed, %d FAILED%s\n" % (passed, failed, ("   (mutation: %s)" % MUT) if MUT else ""))
sys.exit (1 if failed else 0)
