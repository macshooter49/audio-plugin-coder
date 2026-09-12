#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  extract_altwarp.py — fb636 ALT WARP · SLICE THE SHIPPING WARP STATICS VERBATIM into a header that
#  Tests/altwarp_cert.cpp compiles (the extract_import_life.py idiom: the cert drives the bytes that ship,
#  never a transcription of them).
#
#      python3 Tests/extract_altwarp.py <SynthVoice.h> <out.h> [--ref <old SynthVoice.h>] [--mutate M]
#
#  WHAT IS CUT, brace-balanced, bytes as they ship: altSigned, altOddEvenS, altOddEvenGains, warpReadRate,
#  applyPhaseWarp, applyAmpWarp, warpAmpNeedsDc, polyBlep, plus `struct DrawCurve`, kDrawPts, kWarpModeMax and
#  the k* constants they read. They land in `struct awx::NEW`. The SAME cut of the reference file (default:
#  git HEAD's SynthVoice.h — the fb565 "the floor is git's business" rule; --ref names another) lands in
#  `struct awx::OLD`, so the cert can prove modes 0-38 did not move a bit. On a clean tree HEAD IS the
#  shipped file and that bar degrades to self-identity; pass --ref to floor against an older commit.
#
#  THE MUTATIONS (the generated header only; Source is never written). Each must fire exactly once, or this
#  exits 2 — a control that cannot fire is not a control. The cert bar each one must redden is in [..]:
#      copysign  Bend's sign back to the first draft's copysign-on-the-delta        -> [4a] [4c] [3b]
#      exp2      Bend's law back to the design's r = 2^(3s)                         -> [3a]
#      pmsign    the +/- modes put "+" on the HIGH half of the knob                 -> [3b] [3c]
#      nodead    no dead band on Bend/Asym (the glide stalls at 0.49999821)         -> [2]
#      oedead    no dead band on Odd/Even                                           -> [2]
#      asymdir   Asym + moves the knee LATER (the guide's wording, measured wrong)  -> [3c]
#      asymskew  Asym's constant back to Skew's 0.45                                -> [3c]
#      fliplaw   Flip's design law P = 1 − a                                        -> [3d]
#      noblep    Flip's edges hard (no step history used)                           -> [7a] [7c]
#      fwdonly   Flip's residual only on FORWARD steps                              -> [7c]
#      oeweights Odd/Even's design weights (1 − |s|/2, s/2)                         -> [3e]
#      case      Asym +/-'s case label slides to 47 (the fb470 silent mode)         -> [11] [3c]
#      rate      Bend +'s read rate back to 1x                                      -> [3f] [7d]
#      dc        Flip stops asking for the DC blocker                               -> [9]
#
#  ⚠️ If any piece moves or is renamed this exits non-zero with the reason: a LOUD red, never a cert that
#  quietly compiles nothing.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import re, sys, subprocess, os

FUNCS = ["static inline double altSigned (", "static inline float altOddEvenS (",
         "static inline bool altOddEvenGains (", "static double warpReadRate (",
         "static double applyPhaseWarp (", "static float applyAmpWarp (",
         "static inline bool warpAmpNeedsDc (", "static double polyBlep ("]
ALT_ONLY = {"static inline double altSigned (", "static inline float altOddEvenS (",
            "static inline bool altOddEvenGains ("}
CONSTS = ["kSyncExp2", "kFormantExp2", "kFractalMul", "kAltBendC", "kAltAsymC", "kAltPmSign",
          "kAltDeadBand", "kWarpModeMax", "kDrawPts"]
ALT_CONSTS = {"kAltBendC", "kAltAsymC", "kAltPmSign", "kAltDeadBand"}

MUTS = {
  "copysign":  [("const double w = 0.5 + (d >= 0.0 ? 0.5 * M : -0.5 * M);",
                 "const double w = x + std::copysign (0.5 * (M - u), d);")],
  "exp2":      [("const double g = 4.0 * kAltBendC * s / (1.0 - 2.0 * kAltBendC * s);",
                 "const double g = std::exp2 (3.0 * s) - 1.0;")],
  "pmsign":    [("static constexpr double kAltPmSign   = -1.0;", "static constexpr double kAltPmSign   = 1.0;")],
  "nodead":    [("return std::abs (s) < kAltDeadBand ? 0.0 : s;", "return s;")],
  "oedead":    [("return std::abs (s) < (float) kAltDeadBand ? 0.0f : s;", "return s;")],
  "asymdir":   [("const double m = 0.5 - kAltAsymC * s;", "const double m = 0.5 + kAltAsymC * s;")],
  "asymskew":  [("static constexpr double kAltAsymC    = 0.4;", "static constexpr double kAltAsymC    = 0.45;")],
  "fliplaw":   [("const double lo = juce::jmax (0.0, 2.0 * a - 1.0);", "const double lo = 1.0 - a;"),
                ("const double hi = juce::jmin (1.0, 2.0 * a);", "const double hi = 1.0;")],
  "noblep":    [("if (prev >= 0.0 && ! (lo <= 0.0 && hi >= 1.0))", "if (false)")],
  "fwdonly":   [("sgn = juce::jlimit (-1.0, 1.0, sgn + (dt > 0.0 ? r : -r));",
                 "sgn = juce::jlimit (-1.0, 1.0, sgn + (dt > 0.0 ? r : 0.0));")],
  "oeweights": [("g0 = 1.0f + s1 * s2;", "g0 = 1.0f - 0.5f * std::abs (s1 + s2);"),
                ("g1 = s1 + s2;", "g1 = 0.5f * (s1 + s2);")],
  "case":      [("case 42: case 43: case 44:   // ASYM", "case 42: case 43: case 47:   // ASYM")],
  "rate":      [("return s > 0.0 ? (0.5 + kAltBendC * s) / (0.5 - kAltBendC * s) : 1.0;", "return 1.0;")],
  "dc":        [("return (double) amount <= 1.0 - kAltDeadBand;", "return false;")],
}

def die (msg, code = 1):
    print ("extract_altwarp: " + msg, file = sys.stderr); sys.exit (code)

def balanced (src, start):
    b = src.find ("{", start)
    if b < 0: die ("no body after offset %d" % start)
    depth = 0
    for i in range (b, len (src)):
        if src[i] == "{": depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0: return src[start:i + 1]
    die ("unbalanced body at offset %d" % start)

def cut (src, required, tag):
    out = []
    m = re.search (r"struct DrawCurve \{[^\n]*\};", src)
    if not m: die (tag + ": struct DrawCurve not found")
    out.append ("    " + m.group (0))
    for c in CONSTS:
        m = re.search (r"static constexpr (?:double|int) %s\s*=\s*[^;]+;" % c, src)
        if not m:
            if required or c not in ALT_CONSTS: die ("%s: constant %s not found" % (tag, c))
            continue
        out.append ("    " + m.group (0))
    for f in FUNCS:
        i = src.find (f)
        if i < 0:
            if required or f not in ALT_ONLY: die ("%s: %s not found" % (tag, f.strip()))
            continue
        out.append ("    " + balanced (src, i))
    return "\n".join (out)

def main():
    args = sys.argv[1:]
    mut = None; ref = None; pos = []
    i = 0
    while i < len (args):
        if args[i] == "--mutate": mut = args[i + 1]; i += 2
        elif args[i] == "--ref":  ref = args[i + 1]; i += 2
        else: pos.append (args[i]); i += 1
    if len (pos) != 2: die ("usage: extract_altwarp.py <SynthVoice.h> <out.h> [--ref <old>] [--mutate M]")
    src = open (pos[0], encoding = "utf-8").read()
    if ref:
        old = open (ref, encoding = "utf-8").read()
    else:
        here = os.path.dirname (os.path.abspath (pos[0]))
        top = subprocess.run (["git", "-C", here, "rev-parse", "--show-toplevel"], capture_output = True, text = True)
        rel = os.path.relpath (os.path.abspath (pos[0]), top.stdout.strip())
        r = subprocess.run (["git", "-C", here, "show", "HEAD:" + rel], capture_output = True, text = True)
        if r.returncode != 0: die ("git show HEAD:%s failed: %s" % (rel, r.stderr.strip()))
        old = r.stdout
    new_body = cut (src, True, "shipped")
    old_body = cut (old, False, "reference")
    if mut:
        if mut not in MUTS: die ("unknown mutation " + mut)
        for a, b in MUTS[mut]:
            n = new_body.count (a)
            if n != 1: die ("mutation %s: pattern found %d times (must be exactly 1): %s" % (mut, n, a), 2)
            new_body = new_body.replace (a, b)
    hdr = ["// GENERATED by Tests/extract_altwarp.py from %s%s — do not edit." % (pos[0], (" [MUTATED: %s]" % mut) if mut else ""),
           "#pragma once", "#include <juce_core/juce_core.h>", "#include <cmath>", "#include <cstdint>",
           "#include <algorithm>", "#include \"Shapers.h\"", "namespace awx {",
           "struct NEW {", new_body, "};", "struct OLD {", old_body, "};", "}  // namespace awx",
           "#define AW_MUTATION \"%s\"" % (mut or "")]
    open (pos[1], "w", encoding = "utf-8").write ("\n".join (hdr) + "\n")

main()
