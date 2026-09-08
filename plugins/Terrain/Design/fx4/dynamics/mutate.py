#!/usr/bin/env python3
"""mutate.py — FIXES.md §0: prove every protected gate can actually FAIL.

For each protected mechanism this script
  1. copies DynamicsCore.h / TerrainCompressFx.h / TerrainOttFx.h / dynamics_cert.cpp into a
     scratch directory,
  2. DELETES that one mechanism with an exact string replacement — and ABORTS if the text it
     expects is not present, so a mutation can never silently fail to apply (a mutation that
     does not apply produces a false green, which is the whole disease this file treats),
  3. rebuilds the cert against the mutant,
  4. runs only the cert sections that matter, and
  5. requires the named gate to turn FAIL.

A mechanism whose gate stays GREEN under its own mutation is reported as a SURVIVOR — a blocker,
not a footnote.

Usage:  python3 mutate.py            (writes the table to stdout; paste it into MUTATION.md)
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TI   = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SHIM = os.path.join(TI, "Tests", "shim")
SRC  = os.path.join(TI, "Source")
# The worklets and the roster are copied too: section 1 now GATES that they still say what the
# headers say (fb423 §Gate), and it reads them off disk. A mutant run that could not find them
# would report a red gate for the wrong reason.
FILES = ["DynamicsCore.h", "TerrainCompressFx.h", "TerrainOttFx.h", "dynamics_cert.cpp",
         "shipped_labels.inc", "retired_labels.inc", "compress-worklet.js", "ott-worklet.js",
         "ROSTER.md"]

# A mutant is (id, [(file, find, replace), ...], cert sections, substring of the gate that must FAIL).
#
# ⚠️ WHY SOME MUTANTS DELETE **TWO** THINGS. The first run of this file reported five SURVIVORS,
# and four of them were the same story: the COMPRESS transition slew limiter is downstream of the
# smoother seed, the waveshaper-kind crossfade and the discrete-rewiring fades, so deleting any
# ONE of them left the slew limiter holding the click gate green. Defence in depth is good
# engineering and terrible evidence. Each of those mechanisms is therefore mutated WITH the slew
# limiter also removed, which is the only way to show that the mechanism itself is load-bearing;
# and the slew limiter gets its own row, alone, where it is honestly reported as NOT load-bearing.
SLEW_OFF = ("TerrainCompressFx.h",
            "                    gdb = gdbZ_[c] + dyn::clampf (d, -slewPS_, slewPS_);",
            "                    gdb = gdb;   // MUTANT: the transition slew limit is removed")

MUTANTS = [
 ("compress-smoother-seed", [
   ("TerrainCompressFx.h",
    "if (primed_ && newShape != relShape_) seedShape (newShape);",
    "if (false) seedShape (newShape);   // MUTANT: the new smoother is NOT seeded from the live GR")],
  ["4"], "GR is continuous across all 64 Type changes"),

 ("compress-smoother-seed+noslew", [
   ("TerrainCompressFx.h",
    "if (primed_ && newShape != relShape_) seedShape (newShape);",
    "if (false) seedShape (newShape);   // MUTANT"), SLEW_OFF],
  ["4"], "Type transitions"),

 ("compress-transition-slew (alone)", [SLEW_OFF], ["4"], "transitions"),

 # (`compress-heat-kind-fade+noslew` used to live here. It was a WEAKER form of the row below --
 # it deleted TWO mechanisms and still only reached the level gate, where it survived. Cert 4c4
 # measures the CURVATURE instead and the single-mechanism mutant fires on its own, so the
 # two-mechanism version proves strictly less and is gone.)

 ("compress-colour-drive-smoothing", [
   ("TerrainCompressFx.h",
    "                const float grSm = gColG_.proc (0.5f * (gr_[0] + gr_[1]));",
    "                const float grSm = 0.5f * (gr_[0] + gr_[1]);   // MUTANT: the drive follows the RAW ballistic state"),
   SLEW_OFF],
  # fb425: this used to fire on the CLICK gate at 2.11 dB/ms. The fb425 upward-lane ballistics
  # changed which cell is worst, and it now fires on the stronger of the two — §4c3's GR
  # CONTINUITY gate, at 28.35 dB across the Character changes. Needle moved to the gate that
  # actually catches it rather than left pointing at one that no longer does.
  ["4"], "448 Character changes"),

 ("compress-clip-ceiling-tracking", [
   ("TerrainCompressFx.h",
    "            const float clipL = std::max (1.0e-9f, dyn::db2lin (T + lift + mkZ_ + clipHeadDb_) * (1.0f / 1.4f));",
    "            const float clipL = std::max (1.0e-9f, dyn::db2lin (tTgt_ + liftTgt_ + mkTgt_ + clipHeadDb_) * (1.0f / 1.4f));  // MUTANT: the fb421 ceiling, off the TARGETS"),
   SLEW_OFF],
  ["4"], "transitions"),

 ("compress-discrete-fades+noslew", [
   ("TerrainCompressFx.h",
    "        gLeaky_.setTau (0.020f, fs_); gPlat_.setTau (0.020f, fs_); gTwoP_.setTau (0.020f, fs_);\n"
    "        gKnAu_.setTau (0.020f, fs_);  gMs_.setTau (0.020f, fs_);   gDnOn_.setTau (0.020f, fs_);\n"
    "        gTilt_.setTau (0.020f, fs_);  gVarMu_.setTau (0.020f, fs_); gOptoF_.setTau (0.020f, fs_);\n"
    "        gEdge_.setTau (0.020f, fs_);",
    "        gLeaky_.setTau (0.0f, fs_); gPlat_.setTau (0.0f, fs_); gTwoP_.setTau (0.0f, fs_);\n"
    "        gKnAu_.setTau (0.0f, fs_);  gMs_.setTau (0.0f, fs_);   gDnOn_.setTau (0.0f, fs_);\n"
    "        gTilt_.setTau (0.0f, fs_);  gVarMu_.setTau (0.0f, fs_); gOptoF_.setTau (0.0f, fs_);\n"
    "        gEdge_.setTau (0.0f, fs_);   // MUTANT: every discrete re-wiring snaps in one sample"),
   SLEW_OFF],
  ["4"], "transitions"),

 ("compress-sample-rate", [
   ("DynamicsCore.h",
    "    return clampf (1.0f - std::exp (-1.0f / (fs * tauSec)), 0.0f, 1.0f);",
    "    (void) fs; return clampf (1.0f - std::exp (-1.0f / (48000.0f * tauSec)), 0.0f, 1.0f);  // MUTANT: fs dropped")],
  ["4"], "REALISED t63"),

 ("compress-detect-ownership", [
   ("TerrainCompressFx.h",
    "        det_ = (p.axis == 0) ? ts.nativeDet : axisToDet (p.axis);",
    "        det_ = (p.character == 2) ? (int) D_RMS10 : ((p.axis == 0) ? ts.nativeDet : axisToDet (p.axis));  // MUTANT: detForce is back")],
  ["4"], "no Character changes the rectifier"),

 ("ott-tree-swap-fade", [
   ("TerrainOttFx.h",
    "        { pend_ = pin; pendOn_ = true; dipDir_ = -1; return; }",
    "        { dip_ = 0.0f; applyParams (pin); return; }   // MUTANT: the fb421 instant dip_ = 0")],
  ["6"], "TREE SWAP"),

 ("ott-transition-slew", [
   ("TerrainOttFx.h",
    "                        gdb = gdbZ_[c][b] + dyn::clampf (gdb - gdbZ_[c][b], -slewPS_, slewPS_);",
    "                        gdb = gdb;   // MUTANT: OTT's transition slew limit is removed")],
  ["6"], "Character transitions"),

 ("ott-mid-side-fade", [
   ("TerrainOttFx.h",
    "            const float msF = gMs_.proc ((stereo_ == 2) ? 1.0f : 0.0f);",
    "            const float msF = (stereo_ == 2) ? 1.0f : 0.0f;   // MUTANT: the M/S basis rotates in one sample")],
  ["6"], "Mid-Side"),

 # 🚨 fb425 REGRESSION, reported not hidden: this row used to fire and now SURVIVES, because the
 # fb425 clip blend is `clipF * sdn_[b]` and `sdn_` is itself per-sample glided — so deleting the
 # 20 ms fade still leaves a glide in front of the clipper. Defence in depth is good engineering
 # and terrible evidence (the same lesson the slew limiter taught this file). The row stays,
 # honestly labelled, and the row BELOW deletes both and shows the pair is load-bearing.
 ("ott-band-clip-fade (alone)", [
   ("TerrainOttFx.h",
    "            const float clipF = gClip_.proc ((clipHd_ < 900.0f) ? 1.0f : 0.0f);",
    "            const float clipF = (clipHd_ < 900.0f) ? 1.0f : 0.0f;   // MUTANT: the band clipper inserts in one sample")],
  ["6"], "Type transitions"),

 ("ott-band-clip-fade + slope-scaled blend", [
   ("TerrainOttFx.h",
    "            const float clipF = gClip_.proc ((clipHd_ < 900.0f) ? 1.0f : 0.0f);",
    "            const float clipF = (clipHd_ < 900.0f) ? 1.0f : 0.0f;   // MUTANT: inserts in one sample"),
   ("TerrainOttFx.h",
    "                    const float cf = clipF * sdn_[b];",
    "                    const float cf = clipF;   // MUTANT: ...and the slope no longer scales it")],
  ["6"], "Type transitions"),

 ("core-floor-gate", [
   ("DynamicsCore.h",
    "    float t = (xdb - F) / ramp;",
    "    return 1.0f; float t = (xdb - F) / ramp;   // MUTANT: the floor gate is deleted")],
  ["6"], "-96 dBFS floor comes OUT at"),

 # ── fb423 round 3 ────────────────────────────────────────────────────────────────────────
 # The heat-kind crossfade was a SURVIVOR: it is a WAVEFORM mechanism and every gate it faced
 # was a LEVEL gate. Cert 4c4 measures the gain element's CURVATURE (H3 - 3*H1, which cancels
 # gain exactly to cubic order), and the mutation now fires on its own -- no slew-limiter
 # removal needed to make it visible.
 ("compress-heat-kind-fade (alone)", [
   ("TerrainCompressFx.h",
    "if (primed_ && ts.heatKind != heatKind_) { heatKindOld_ = heatKind_; gKind_.snap (0.0f); }",
    "if (primed_ && ts.heatKind != heatKind_) { heatKindOld_ = ts.heatKind; gKind_.snap (1.0f); }  // MUTANT")],
  ["4"], "CURVATURE is continuous"),

 # R11. FIXES.md 0 named these as the last outstanding piece anywhere in the family: the
 # ceilings were ASSERTED. A ceiling nobody has tried to break is fb421's epistemic position.
 ("compress-no-ceiling", [
   ("TerrainCompressFx.h",
    "        sTgt_ = dyn::clampf (s, 0.0f, sCap);",
    "        sTgt_ = dyn::clampf (s, 0.0f, std::min (sCap, 0.5f));  // MUTANT: a POLITE 2:1 maximum")],
  ["4"], "48 dB staircase"),

 ("ott-no-ceiling", [
   ("TerrainOttFx.h",
    "        const float u    = (amt >  0.5f) ? (amt - 0.5f) * 2.0f : 0.0f;",
    "        const float u    = 0.0f;   // MUTANT: the top half of Amount stops closing the thresholds")],
  ["6", "8"], "Amount 100 leaves"),

 # ... and the SAME deletion against the fb425 per-Type ceiling gate, which is a different gate
 # with a different metric (surviving dB off a 36 dB staircase) on all 8 Types instead of one.
 ("ott-no-ceiling (per-Type, §8c)", [
   ("TerrainOttFx.h",
    "        const float u    = (amt >  0.5f) ? (amt - 0.5f) * 2.0f : 0.0f;",
    "        const float u    = 0.0f;   // MUTANT: the top half of Amount stops closing the thresholds")],
  ["8"], "walls at Amount 100"),

 # LAW 1 -- a dead knob. One knob per device is stubbed to a no-op and the 0->100 sweep must
 # go red. Without this, "every knob is night and day" rests on the sweep having been pointed
 # at the right member, which nothing checked.
 ("compress-dead-knob (Burn, P8)", [
   ("TerrainCompressFx.h",
    "        heatTgt_ = dyn::clampf (std::max (dyn::clampf (p.b8, 0.0f, 1.0f), cs.heatFloor), 0.0f, 1.0f);",
    "        heatTgt_ = dyn::clampf (std::max (0.0f, cs.heatFloor), 0.0f, 1.0f);  // MUTANT: Burn is a dead knob")],
  ["4"], "(P8)"),

 ("ott-dead-knob (Treble, P8)", [
   ("TerrainOttFx.h",
    "                                (dyn::clampf (p.b8, 0.0f, 1.0f) - 0.5f) * 24.0f };",
    "                                0.0f };   // MUTANT: Treble is a dead knob")],
  ["6"], "(P8)"),

 # SHEEN's new mechanism (fb423). Put Over Top's high-band thresholds back and both of the
 # gates that replaced the old "more air" gate must fall over.
 ("ott-sheen-upward-lane", [
   ("TerrainOttFx.h",
    "        { 1.0f, 0.55f, 3, {-40,-31,-26}, {-45,-37,-28}, {0.90f,0.907f,1.0f}, {0.8f,0.8f,0.9f},\n"
    "          {12,14,2}, {2.8f,1.4f,0.35f}, {40,28,8}, 2.0f, 0, 999.0f, 0, 0 },",
    "        { 1.0f, 0.55f, 3, {-40,-31,-40}, {-45,-37,-46}, {0.90f,0.907f,1.0f}, {0.8f,0.8f,0.9f},\n"
    "          {12,14,13}, {2.8f,1.4f,0.35f}, {40,28,8}, 2.0f, 0, 999.0f, 0, 0 },  // MUTANT: Over Top's high band")],
  ["5"], "Sheen"),

 # And the NAMES gate itself, which is new and has therefore never failed. Drift one downstream
 # string -- exactly the class of rot that left 22 of them alive across this family.
 ("names-downstream-drift", [
   ("ott-worklet.js", "'Mean Ears'", "'Long Ears'   /* MUTANT: downstream drifts off the header */")],
  ["1"], "ott-worklet CHARS"),

 # ══ fb425 — THE FULL-MATRIX ROUND ═══════════════════════════════════════════════════════════
 # Nine new rows. Every one of them is a mechanism this round ADDED or a hole this round CLOSED,
 # and none of them could have fired before: sections 7/8/9 did not exist, and the three gates in
 # section 1 they aim at were a substring search, a hand-typed blacklist and a list with no size.

 # 1. THE BUG. Put the fb424 clip ceiling back — the one that tracks a makeup `Amount` drives to
 #    zero — and 8d must see the knob running backwards again (THD 35.75 % at Amount 0 on Heavy).
 ("ott-clip-ceiling-backwards", [
   ("TerrainOttFx.h",
    "                    const float cf = clipF * sdn_[b];",
    "                    const float cf = clipF;   // MUTANT: the fb424 blend, not scaled by the slope"),
   ("TerrainOttFx.h",
    "                        const float lim = dyn::db2lin (pinDb + clipHd_ + slack);",
    "                        const float lim = dyn::db2lin (pinDb + clipHd_);   // MUTANT: the fb424 ceiling")],
  ["8"], "runs forwards"),

 # 2. R11 on the feedback Types. Remove the crossover and FET 76 / Opto / Vari-Mu go back to the
 #    authentic — and polite — 0.5 closed-loop slope at Ratio 100.
 ("compress-ratio-wall", [
   ("TerrainCompressFx.h",
    "        const float wall  = dyn::clampf ((rk - 0.90f) * 10.0f, 0.0f, 1.0f);",
    "        const float wall  = 0.0f;   // MUTANT: no wall — the feedback tap stays feedback at 100 %")],
  ["7"], "walls at Push/Ratio 100"),

 # 3. LAW 3 on OTT. This is the mutation FIXES.md §fb425 names: it left ALL 53 fb424 gates green,
 #    because the only gate that touched Mix required out(mix=1) ~ out(mix=0) and forcing wet made
 #    it pass HARDER.
 ("ott-mix-forced-wet", [
   ("TerrainOttFx.h",
    "        mixTgt_  = dyn::clampf (p.mix, 0.0f, 1.0f);",
    "        mixTgt_  = 1.0f;   // MUTANT: the Mix knob is dead, always fully wet")],
  ["8"], "Mix 0 is the DRY path"),

 # 4. THE FRONT PILL. `p.crest` appeared ONCE in the fb424 cert, inside a click list that gets
 #    BETTER when the pill is deleted.
 ("ott-crest-pill-noop", [
   ("TerrainOttFx.h",
    "        upHold_   = (cs.upHold != 0) || p.crest;",
    "        upHold_   = (cs.upHold != 0);   // MUTANT: the Crest pill is a no-op")],
  ["8"], "BIT-IDENTICAL"),

 # 5. OTT's sample-rate gate. `attackMs_[b] = nA*1000/fs_` with `nA = max (5, aMs*0.001*fs_)`
 #    cancels `fs_` algebraically, so the fb424 gate compared a constant to itself and would have
 #    passed on exactly this engine.
 ("ott-sample-rate", [
   ("TerrainOttFx.h",
    "            const float nA = std::max (5.0f, aMs * 0.001f * fs_);\n"
    "            const float nR = std::max (5.0f, rMs * 0.001f * fs_);",
    "            const float nA = std::max (5.0f, aMs * 0.001f * 48000.0f);   // MUTANT: fs dropped\n"
    "            const float nR = std::max (5.0f, rMs * 0.001f * 48000.0f);")],
  ["8"], "REALISED ballistics"),

 # 6. THE WHOLE POINT OF fb425. A knob killed on ONE TYPE ONLY. Section 4's sweep runs on Type 0
 #    and stays green; the matrix must catch it on Vari-Mu. This is the fb424 level, deleted.
 ("compress-dead-cell (Release, Vari-Mu only)", [
   ("TerrainCompressFx.h",
    "        relMs_ = std::max (relMs, 1000.0f / fs_);",
    "        relMs_ = (p.type == 4) ? 250.0f : std::max (relMs, 1000.0f / fs_);  // MUTANT: dead on ONE Type")],
  ["7"], "under the bar"),

 # 7. THE ROSTER DRIFT GATE, positional half. A skeptic moved two Characters under the WRONG
 #    TYPES and the fb424 substring search stayed green.
 ("roster-grid-scramble", [
   ("ROSTER.md", "`Blackface` Burn 0.20", "`Cell Classic` Burn 0.20"),
   ("ROSTER.md", "`Cell Classic` +0.35 detector tilt", "`Blackface` +0.35 detector tilt")],
  ["1"], "WRONG Type"),

 # 8. THE RETIRED-LABEL BLACKLIST, now derived from RENAMES.md instead of typed. Put a retired
 #    name back downstream as a LABEL and it must be found.
 ("retired-label-drift", [
   ("ott-worklet.js", "'Full Crest'", "'Full Bite'   /* MUTANT: a retired label, back downstream */")],
  ["1"], "RETIRED label survives"),

 # 9. THE EXEMPTION LIST. Add an entry that exempts nothing — the shape `Auto` had before fb423
 #    and `Peak`/`Bass`/`Treble`/`Ratio` had until this round.
 ("exemption-not-load-bearing", [
   ("dynamics_cert.cpp",
    '        { "Power",     "the fb266 frozen rack chassis',
    '        { "Grip",      "MUTANT: an exemption that exempts nothing" },\n'
    '        { "Power",     "the fb266 frozen rack chassis')],
  ["1"], "LOAD-BEARING"),

 ("cert-fft-normalisation", [
   ("dynamics_cert.cpp",
    "    e *= 2.0 / ((double) N * (double) N * U);",
    "    // MUTANT: the Parseval normalisation is dropped (this is what fb421 shipped)")],
  ["2"], "CALIBRATED"),
]

def run(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)

def main():
    base = tempfile.mkdtemp(prefix="dyn_mut_")
    rows, survivors = [], []
    print("%-34s %-38s %s" % ("mutant", "gate that must fire", "result"))
    print("-" * 108)
    only = [a for a in sys.argv[1:]]
    for mid, edits, secs, needle in MUTANTS:
        if only and not any (o in mid for o in only):
            continue
        d = os.path.join(base, mid.replace(" ", "_").replace("(", "").replace(")", ""))
        os.makedirs(d, exist_ok=True)
        for f in FILES:
            shutil.copy(os.path.join(HERE, f), d)
        for fname, find, repl in edits:
            path = os.path.join(d, fname)
            txt = open(path).read()
            n = txt.count(find)
            if n != 1:
                print("%-34s ABORT: expected 1 occurrence in %s, found %d" % (mid, fname, n))
                sys.exit(2)
            open(path, "w").write(txt.replace(find, repl))
        exe = os.path.join(d, "cert")
        c = run(["clang++", "-O2", "-std=c++17", "-DDYN_DIR=\"%s\"" % d,
                 "-I", SHIM, "-I", SRC, "-I", d,
                 os.path.join(d, "dynamics_cert.cpp"), "-o", exe])
        if c.returncode != 0:
            print("%-34s BUILD FAILED\n%s" % (mid, c.stderr[:900])); sys.exit(2)
        r = run([exe] + secs)
        fired = [ln.strip() for ln in r.stdout.splitlines()
                 if ln.strip().startswith("FAIL") and needle in ln]
        allfail = [ln.strip() for ln in r.stdout.splitlines() if ln.strip().startswith("FAIL")]
        ok = len(fired) > 0
        rows.append((mid, needle, ok, fired, allfail))
        print("%-34s %-38s %s" % (mid, needle, "RED (good)" if ok else "*** SURVIVED ***"))
        for ln in fired:
            print("        %s" % ln)
        if not ok:
            survivors.append(mid)
            for ln in allfail[:4]:
                print("        (other) %s" % ln)
    print("-" * 108)
    print("%d mutants, %d gates fired, %d SURVIVORS%s"
          % (len(rows), len(rows) - len(survivors), len(survivors),
             ("  → " + ", ".join(survivors)) if survivors else ""))
    print("scratch: %s" % base)

main()
