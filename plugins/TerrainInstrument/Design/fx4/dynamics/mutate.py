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
         "shipped_labels.inc", "compress-worklet.js", "ott-worklet.js", "ROSTER.md"]

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
  ["4"], "transitions"),

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

 ("ott-band-clip-fade", [
   ("TerrainOttFx.h",
    "            const float clipF = gClip_.proc ((clipHd_ < 900.0f) ? 1.0f : 0.0f);",
    "            const float clipF = (clipHd_ < 900.0f) ? 1.0f : 0.0f;   // MUTANT: the band clipper inserts in one sample")],
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
  ["6"], "Amount 100 leaves"),

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
    for mid, edits, secs, needle in MUTANTS:
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
