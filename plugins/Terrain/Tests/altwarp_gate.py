#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  altwarp_gate.py — fb636 ALT WARP · THE PLUMBING AROUND THE KERNELS (source gate, no build).
#  altwarp_cert.cpp proves the laws; this proves every site the laws have to reach actually reaches them.
#
#      python3 Tests/altwarp_gate.py                     (from plugins/Terrain)
#      AWG_MUTATE=<m> python3 Tests/altwarp_gate.py      (a control — must go red on its bar)
#
#  BARS
#   [1] NAMES   terrainWarpModeNames() lists 39 Bend + · 40 Bend - · 41 Bend +/- · 42 Asym + · 43 Asym - ·
#               44 Asym +/- · 45 Flip · 46 Odd/Even and pads ONLY 47 ("Reserved 47"), 48 in all; index.html's
#               WARP_MODES carries the same strings in the same slots, 48 long, 'Reserved 47' last (the
#               anchor warp_menu.js's MUT 1 looks for).
#   [2] FILED   every live index 39-46 sits in exactly one WARP_FAMILIES family (__warpGuard's own rule —
#               a live name filed nowhere makes the pickers REFUSE to open). The families' Serum order and
#               contents are altwarp_ui_gate.js [1]'s; this bar only asks that 39-46 are filed.
#   [3] FLIP    all 16 per-sample slot calls (WT + FM · 4 oscs · 2 slots) pass &flipPrev_[osc*2+slot][u] with
#               THEIR OWN index, and note-on resets the history.
#   [4] PAIR    each osc arms Odd/Even from its own two modes, its WT read mixes the second tap on BOTH the
#               feedback and the blend path, its FM carrier mixes it, and the waterfall draws it.
#   [5] FM DC   each osc's FM DC gate asks slot 1 for mode 45 and ONLY 45 (Max: remove Flip's DC).
#   [6] CARD    getWarpCurveJson routes 39-46 before the `mode >= 9` amp branch.
#   [7] MIP     the four mip lines ask warpRateFan for all 8 slots, and warpRateFan widens only mode 41.
#
#  CONTROLS (in-memory copies; Source is never written) — each must redden its bar:
#   names -> [1]   family -> [2]   flipidx -> [3]   pair -> [4]   fmdc -> [5]   curve -> [6]   mip -> [7]
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys

ROOT = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..")
SV   = open (os.path.join (ROOT, "Source/SynthVoice.h"), encoding = "utf-8").read()
PP   = open (os.path.join (ROOT, "Source/PluginProcessor.cpp"), encoding = "utf-8").read()
UI   = open (os.path.join (ROOT, "Source/ui/public/index.html"), encoding = "utf-8").read()
MUT  = os.environ.get ("AWG_MUTATE", "")

def mutate (s, a, b):
    if s.count (a) < 1: print ("  !! control %s cannot fire (pattern gone)" % MUT); sys.exit (2)
    return s.replace (a, b, 1)
if MUT == "names":   UI = mutate (UI, "/* 42 */ 'Asym +',", "/* 42 */ 'Asym -',")
if MUT == "family":  UI = mutate (UI, "idx: [39, 40, 41, 4, 42, 43, 44, 45, 6, 8, 46]", "idx: [39, 40, 41, 4, 42, 43, 44, 6, 8, 46]")
if MUT == "flipidx": SV = mutate (SV, "drawIf (warp2ModeC_, 2, 1), &flipPrev_[5][(size_t) u]);", "drawIf (warp2ModeC_, 2, 1), &flipPrev_[4][(size_t) u]);")
if MUT == "pair":    SV = mutate (SV, "if (oeArmD && altOddEvenGains (warpModeD_, wAmt1Dfm, warp2ModeD_, wAmt2D, oeG0, oeG1))", "if (false)")
if MUT == "fmdc":    SV = mutate (SV, "|| (warpModeB_ == 45 && warpAmpNeedsDc (warpModeB_, warpAmountB_, warpVar_[1]))", "")
if MUT == "curve":   PP = mutate (PP, "if (mode >= 9 && ! altWarp)", "if (mode >= 9)")
if MUT == "mip":     SV = mutate (SV, "warpRateFan (0, warp2ModeA_, warp2AmountA_, ", "warpRateMul (warp2ModeA_, warpFan (0, warp2AmountA_), ")

passed = failed = 0; red = []
def bar (ok, bid, msg):
    global passed, failed
    if ok: passed += 1
    else:  failed += 1; red.append (bid)
    print ("  %s  [%s] %s" % ("ok  " if ok else "FAIL", bid, msg))

NEW = ["Bend +", "Bend -", "Bend +/-", "Asym +", "Asym -", "Asym +/-", "Flip", "Odd/Even"]

# ── [1] names, both sides ──────────────────────────────────────────────────────────────────────
i = PP.find ("static juce::StringArray terrainWarpModeNames()")
body = PP[i:PP.find ("\n}", i)]
arr = body[body.find ("juce::StringArray w {"):body.find ("};")]
cnames = []
for line in arr.splitlines():
    c = line.find ("//"); l = line if c < 0 else line[:c]
    cnames += re.findall (r'"([^"]*)"', l)
cap = re.search (r"for \(int i = w\.size\(\); i < (\d+); \+\+i\) w\.add \(\"Reserved \" \+ juce::String \(i\)\)", body)
total = int (cap.group (1)) if cap else -1
cfull = cnames + ["Reserved %d" % k for k in range (len (cnames), total)]
j = UI.find ("const WARP_MODES = [")
jarr = UI[j:UI.find ("];", j)]
jcode = "\n".join (l if l.find ("//") < 0 else l[:l.find ("//")] for l in jarr.splitlines())   # comments may hold apostrophes
jnames = re.findall (r"'([^']*)'", re.sub (r"/\*.*?\*/", "", jcode))
ok1 = (total == 48 and len (cnames) == 47 and cfull[39:47] == NEW and cfull[47] == "Reserved 47"
       and len (jnames) == 48 and jnames[39:47] == NEW and jnames[47] == "Reserved 47"
       and "'Reserved 47'\n    ];" in UI)
bar (ok1, "1", "C++ 39-47 %s · JS 39-47 %s · C++ %d named + pad to %d · JS %d long"
     % (cfull[39:48], jnames[39:48], len (cnames), total, len (jnames)))

# ── [2] filed exactly once ─────────────────────────────────────────────────────────────────────
k = UI.find ("const WARP_FAMILIES = [")
fam = UI[k:UI.find ("];", k)]
filed = {}
for lab, idx in re.findall (r"label:\s*'([^']*)',\s*idx:\s*\[([^\]]*)\]", fam):
    for n in re.findall (r"\d+", idx): filed.setdefault (int (n), []).append (lab)
ok2 = all (len (filed.get (n, [])) == 1 for n in range (39, 47)) and 47 not in filed
bar (ok2, "2", "39-46 filed in %s; 47 filed: %s" % ({n: filed.get (n) for n in range (39, 47)}, 47 in filed))

# ── [3] the flip history, sixteen sites ────────────────────────────────────────────────────────
calls = re.findall (r"applyPhaseWarp \((\w+), wAmt([12])([A-D])(fm)?, (?:warpedPhase|cPh), [^;]*?&flipPrev_\[(\d)\]\[\(size_t\) u\]\);", SV)
bad3 = [c for c in calls if int (c[4]) != "ABCD".index (c[2]) * 2 + int (c[1]) - 1]
reset = "for (auto& fp : flipPrev_) fp[(size_t) u] = -1.0;" in SV
nplain = len (re.findall (r"applyPhaseWarp \(\w+, wAmt[12][A-D](?:fm)?, (?:warpedPhase|cPh), [^;&]*\);", SV))
bar (len (calls) == 16 and not bad3 and reset and nplain == 0, "3",
     "%d/16 slot calls carry their history, %d on the wrong index, %d without it, note-on reset %s"
     % (len (calls), len (bad3), nplain, reset))

# ── [4] the pair read ──────────────────────────────────────────────────────────────────────────
OSC = [("A", "warpMode_",  "warp2ModeA_", "sAu", "currentWavetable_"),
       ("B", "warpModeB_", "warp2ModeB_", "sBu", "currentWavetableB_"),
       ("C", "warpModeC_", "warp2ModeC_", "sCu", "currentWavetableC_"),
       ("D", "warpModeD_", "warp2ModeD_", "sDu", "currentWavetableD_")]
miss4 = []
arm = re.search (r"const bool oeArmA = \(warpMode_  == 46 \|\| warp2ModeA_ == 46\), oeArmB = \(warpModeB_ == 46 \|\| warp2ModeB_ == 46\),\s*"
                 r"oeArmC = \(warpModeC_ == 46 \|\| warp2ModeC_ == 46\), oeArmD = \(warpModeD_ == 46 \|\| warp2ModeD_ == 46\);", SV)
if not arm: miss4.append ("the per-block arm flags")
for L, m1, m2, S, T in OSC:
    need = ["oeArm%s && altOddEvenGains (%s, wAmt1%s, %s, wAmt2%s, oeG0, oeG1)" % (L, m1, L, m2, L),
            "%s = oeG0 * %s + oeG1 * %s->lookup (currentMipLevel%s_, fpf, (float) rq);" % (S, S, T, L),
            "%s = oeG0 * %s + oeG1 * wtBlendRead (blend%s_.data(), blendPrev%s_.data(), blendXf%s_, blendFrac, (float) rq);" % (S, S, L, L, L),
            "if (oeArm%s && altOddEvenGains (%s, wAmt1%sfm, %s, wAmt2%s, oeG0, oeG1))" % (L, m1, L, m2, L),
            "(%s == 46 ? fmO.carrierPhase : cPh) + 0.5;" % m1,
            "(%s == 46 ? uPhase%s_[(size_t) u] : warpedPhase) + 0.5" % (m1, L)]
    for n in need:
        if SV.count (n) != 1: miss4.append ("osc %s: %s" % (L, n[:70]))
if "tw::SynthVoice::altOddEvenGains (D.warpMode, D.warpAmt, D.warp2Mode, D.warp2Amt, oeG0, oeG1)" not in PP:
    miss4.append ("the waterfall")
bar (not miss4, "4", "all four oscs (WT feedback + blend, FM) and the waterfall read the pair" if not miss4 else "; ".join (miss4))

# ── [5] Flip's DC on the FM engine ─────────────────────────────────────────────────────────────
miss5 = []
for L, m1, AM1, I, ENG in [("A", "warpMode_", "warpAmount_", 0, "engine_"), ("B", "warpModeB_", "warpAmountB_", 1, "engineB_"),
                           ("C", "warpModeC_", "warpAmountC_", 2, "engineC_"), ("D", "warpModeD_", "warpAmountD_", 3, "engineD_")]:
    pat = (r"\|\| \(%s == Engine::FM && \(warpAmpNeedsDc \(warp2Mode%s_, warp2Amount%s_, warp2Var_\[%d\]\)\s*"
           r"\|\| \(%s == 45 && warpAmpNeedsDc \(%s, %s, warpVar_\[%d\]\)\)\)\)\)") % (ENG, L, L, I, m1, m1, AM1, I)
    if not re.search (pat, SV): miss5.append (L)
bar (not miss5, "5", "every osc's FM gate asks slot 1 for Flip (45) only" if not miss5 else "missing on osc " + ",".join (miss5))

# ── [6] the curve card ─────────────────────────────────────────────────────────────────────────
g = PP.find ("juce::String TerrainAudioProcessor::getWarpCurveJson")
gb = PP[g:PP.find ("\n}\n", g)]
ok6 = ("const bool altWarp = (mode >= 39 && mode <= 46);" in gb and "if (mode >= 9 && ! altWarp)" in gb
       and gb.find ("const bool altWarp") < gb.find ("if (mode >= 9"))
bar (ok6, "6", "39-46 are routed to the phase branch before `mode >= 9`" if ok6 else "39-46 would be drawn and captured as amp curves")

# ── [7] the mip pick ───────────────────────────────────────────────────────────────────────────
lines = [l for l in SV.splitlines() if "tw::Wavetable::mipLevelForPhaseIncrement (uPhaseInc" in l]
nfan = sum (l.count ("warpRateFan (") for l in lines)
nold = sum (len (re.findall (r"warpRateMul \(\w+, +warpFan", l)) for l in lines)
ok7 = len (lines) == 4 and nfan == 8 and nold == 0 and "if (mode != 41) return hi;" in SV
bar (ok7, "7", "%d mip lines, %d warpRateFan terms, %d bare warpFan terms" % (len (lines), nfan, nold))

print ("\n  %d passed, %d FAILED%s" % (passed, failed, ("   RED: " + " ".join ("[%s]" % r for r in red)) if red else ""))
sys.exit (1 if failed else 0)
