#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  extract_imports_scan.py — fb606 · SLICE THE SHIPPING IMPORTS-REGISTRY SCAN OUT OF
#  Source/PluginProcessor.cpp VERBATIM into a header Tests/wt_folder_scan_cert.cpp compiles.
#
#      python3 Tests/extract_imports_scan.py                    # regenerate the header
#      python3 Tests/extract_imports_scan.py --mutate           # …with the recursion TURNED OFF
#                                                               #   (the fb605 bug, restored)
#      python3 Tests/extract_imports_scan.py <src.cpp> <out.h> [--mutate]
#
#  WHY A SLICE AND NOT A COPY (the extract_helpers.py / extract_halfband.py idiom).  The owner's
#  complaint is about ONE boolean in ONE line of PluginProcessor.cpp:
#
#        d.findChildFiles (juce::File::findFiles, false, kImportWild);
#                                                 ^^^^^ searchRecursively
#
#  fb606 replaced that line with a hand-rolled, depth-capped walk (JUCE's own recursion cannot
#  carry each hit's relative subfolder and has no depth limit), so the boolean is no longer what
#  you look for — which is precisely why the cert must not be a transcription of either shape.
#
#  A cert that re-types that scan tests a copy, and a copy drifts — which is exactly how
#  Tests/flt_measure.h ended up measuring an oversampler the plugin does not have (see
#  extract_halfband.py's header). So the cert compiles the SHIPPING BYTES: this file cuts the
#  whole IMPORTS REGISTRY section (the anonymous namespace + every TerrainAudioProcessor member
#  in it) and rewrites only the class qualifier, so a helper the fix ADDS inside that section is
#  sliced too and no edit is needed here.
#
#  ⚠️ WHAT IT WILL NOT SURVIVE, ON PURPOSE.  If the scan is moved OUT of the banner-delimited
#  section, or getImportsJson stops being a TerrainAudioProcessor member, this exits non-zero with
#  the reason. That is a LOUD red, not a silent green: the failure mode this whole file exists to
#  prevent is a cert that quietly compiles nothing.
#
#  THE MUTATION.  --mutate turns the recursion off in the GENERATED header only (PluginProcessor.cpp
#  is never written): `searchRecursively` -> false on a build that uses JUCE's flag, or the depth
#  cap -> 0 on a build whose walk is hand-rolled. If NEITHER is present it raises rather than
#  producing a mutant that cannot fire — a control that cannot go red is not a control.
#  wt_folder_scan_cert must then FAIL.
#
#  ⚠️ COMPILE THE MUTANT WITH -DTI_SLICE_HEADER='"<path>"', NEVER WITH -I<dir>. A quoted #include
#  searches the including file's OWN directory first, before every -I, so an -I build silently
#  compiles the healthy Tests/ copy and reports a gate that cannot go red. Measured: 11/0.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys

ROOT    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEF_SRC = os.path.join(ROOT, "Source", "PluginProcessor.cpp")
DEF_OUT = os.path.join(ROOT, "Tests", "ti_imports_scan_extracted.h")

SECTION_START = re.compile(r"^// IMPORTS REGISTRY\b", re.M)
# the slice runs from that banner to the closing brace of the LAST registry member, so a helper
# the fix adds anywhere between them is sliced too and this file needs no edit.
SECTION_LAST  = "TerrainAudioProcessor::loadImportsRegistry"
# ⚠️ builtinWtCatItems reads the APVTS (juce_audio_processors + ParameterIDs + the whole plugin).
# It is CUT OUT and stubbed by the cert, which says so on bar [0]. This cert is about the folder
# WALK; the built-in taxonomy is a different claim with a different gate.
EXCISE = ["TerrainAudioProcessor::builtinWtCatItems"]

# every member the cert has to be able to call, by the name it is declared with in
# PluginProcessor.h. If one of these is not in the slice, the cert would compile a stub of it and
# pass against nothing — so this is checked, not assumed.
REQUIRED = ["getImportsJson", "getManagedWavetablesJson", "addImportPath", "removeImportPath",
            "saveImportsRegistry", "loadImportsRegistry"]


def brace_end(src, i):
    """index one past the } that closes the first { at or after i"""
    o = src.index("{", i)
    d = 0
    for j in range(o, len(src)):
        if src[j] == "{":
            d += 1
        elif src[j] == "}":
            d -= 1
            if d == 0:
                return j + 1
    raise LookupError("unbalanced braces after offset %d" % i)


def slice_section(src):
    m = SECTION_START.search(src)
    if not m:
        raise LookupError("NOT FOUND: the '// IMPORTS REGISTRY' banner in PluginProcessor.cpp — "
                          "the section this cert slices has been renamed or removed")
    k = src.find(SECTION_LAST, m.end())
    if k < 0:
        raise LookupError("NOT FOUND: " + SECTION_LAST + " after the IMPORTS REGISTRY banner — "
                          "the slice has no end and would cut the file in half")
    body = src[m.start(): brace_end(src, k)]

    for name in EXCISE:                        # cut it out, and leave a sign saying so
        j = body.find(name)
        if j < 0:
            raise LookupError("EXCISE target not in the slice: " + name
                              + " — this extractor is out of date with PluginProcessor.cpp")
        ls = body.rfind("\n", 0, j) + 1                     # start of the line holding the name
        cut_end = brace_end(body, j)
        body = (body[:ls]
                + "// [EXCISED by the extractor: this member reads the APVTS; the cert stubs it]\n"
                + body[cut_end:])
        if name in body:
            raise LookupError("EXCISE left a second definition of " + name + " in the slice")

    first_line = src[: m.start()].count("\n") + 1
    return first_line, body


def fnv1a64(s):
    h = 0xCBF29CE484222325
    for b in s.encode("utf-8"):
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def generate(src_path, out_path, mutate=False):
    with open(src_path, "r", encoding="utf-8") as f:
        src = f.read()
    line0, body = slice_section(src)

    missing = [w for w in REQUIRED if ("TerrainAudioProcessor::" + w) not in body]
    if missing:
        raise LookupError("the IMPORTS REGISTRY slice does not define: " + ", ".join(missing)
                          + " — they moved out of the section, so the cert would test nothing")

    # ── the ONE rewrite: the class qualifier. Nothing else in the bytes is touched. ────────────
    n_qual = body.count("TerrainAudioProcessor::")
    body = body.replace("TerrainAudioProcessor::", "ImportsReg::")

    # ── HOW DOES THIS BUILD RECURSE, AND HOW DO I TURN THAT OFF? ──────────────────────────────
    # Two shapes exist and the mutation has to handle both, because which one is in the file is
    # exactly what fb606 changed:
    #   FLAG   findChildFiles (findFiles, true, …)   — JUCE walks it (fb605 shipped `false`: the bug)
    #   WALK   a hand-rolled recursion bounded by a depth cap — the only way to carry `rel`
    # --mutate turns the flag off, or clamps the depth cap to 0, whichever is there. Both make a
    # master folder show only what sits directly in it: the owner's bug, restored on purpose.
    # (?:/\*.*?\*/\s*)? — the argument may carry an inline /* searchRecursively */ label
    scan = re.compile(r"(findChildFiles\s*\(\s*juce::File::findFiles\s*,\s*(?:/\*.*?\*/\s*)?)(true|false)(\s*,)")
    flags = [h[1] for h in scan.findall(body)]
    if not flags:
        raise LookupError("NOT FOUND: a findChildFiles (juce::File::findFiles, <bool>, …) call in "
                          "the IMPORTS REGISTRY section — the folder scan has changed shape")
    depth_re = re.compile(r"(\bk[A-Za-z_]*(?:Max|MAX)[A-Za-z_]*Depth[A-Za-z_]*\s*=\s*)(\d+)")
    dm = depth_re.search(body)
    recursive = flags + (["hand-rolled walk, depth cap " + dm.group(2)] if dm else [])

    mutated, how = 0, "nothing"
    if mutate:
        if "true" in flags:
            body, mutated = scan.subn(r"\1false\3", body)
            how = "%d findChildFiles searchRecursively -> false" % mutated
        elif dm:
            body, mutated = depth_re.subn(r"\g<1>0", body, count=1)
            how = "%s -> 0 (the walk cannot enter a subfolder)" % dm.group(0).split("=")[0].strip()
        else:
            raise LookupError("--mutate has NOTHING TO TURN OFF: no `true` recursion flag and no "
                              "depth-cap constant in the slice. A mutation that cannot fire is not "
                              "a control — fix this extractor before trusting the gate.")

    # ── caps: a depth/count limit that silently truncates is a second invisible-missing-tables
    #    bug, so wt_folder_scan_cert has a bar for it — but that bar can only run against a build that
    #    HAS a cap. Report what is declared so the cert can arm the bar or say PENDING and print
    #    exactly what it searched for. (A detector that can no-op must print whether it fired.)
    CAP_RE = re.compile(r"\b(k?[A-Za-z_]*(?:Max|MAX|Cap|CAP|Limit|LIMIT)[A-Za-z_]*)\s*=\s*(\d+)")
    caps = [(n, int(v)) for n, v in CAP_RE.findall(body)]
    depth_cap = next((v for n, v in caps if re.search(r"depth", n, re.I)), 0)
    count_cap = next((v for n, v in caps if not re.search(r"depth", n, re.I)), 0)

    h = fnv1a64(re.sub(r"\s+", " ", body).strip())
    rel = os.path.relpath(src_path, ROOT)
    out = [
        "// GENERATED by Tests/extract_imports_scan.py — a VERBATIM slice of " + rel,
        "// (the '// IMPORTS REGISTRY' section, first line " + str(line0) + "). DO NOT EDIT.",
        "//   TerrainAudioProcessor:: -> ImportsReg::   x" + str(n_qual) + "   (the only rewrite)",
        "//   searchRecursively as sliced: " + ", ".join(recursive),
        "//   caps: " + (", ".join("%s=%d" % c for c in caps) if caps else "(none declared)"),
        "//   MUTATED: " + ((how + "  [" + str(mutated) + " site(s)]") if mutate else "no"),
        "#pragma once",
        "#define TI_IMPORTS_SLICE_HASH 0x%016Xull" % h,
        "#define TI_IMPORTS_SLICE_RECURSIVE_AS_SLICED \"" + ",".join(recursive) + "\"",
        "#define TI_IMPORTS_SLICE_MUTATED " + str(mutated),
        '#define TI_IMPORTS_SLICE_CAPS "' + ",".join("%s=%d" % c for c in caps) + '"',
        "#define TI_IMPORTS_SLICE_DEPTH_CAP " + str(depth_cap),
        "#define TI_IMPORTS_SLICE_COUNT_CAP " + str(count_cap),
        "",
        '#line %d "PluginProcessor.cpp"' % line0,
        body.rstrip(),
        "",
    ]
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print("sliced IMPORTS REGISTRY (%s:%d, %d lines, %d qualifiers) -> %s"
          % (rel, line0, body.count("\n") + 1, n_qual, out_path))
    print("  recursion as sliced: %s%s"
          % ("; ".join(recursive), ("   [--mutate: " + how + "]") if mutate else ""))
    print("  cap constants declared in the section: %s"
          % (", ".join("%s=%d" % c for c in caps) if caps else
             "(none — wt_folder_scan_cert bar [6] will report PENDING)"))
    return 0


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--mutate"]
    mut  = "--mutate" in sys.argv[1:]
    s = args[0] if len(args) > 0 else DEF_SRC
    o = args[1] if len(args) > 1 else DEF_OUT
    try:
        sys.exit(generate(s, o, mut))
    except LookupError as e:
        print("EXTRACT FAILED: %s" % e, file=sys.stderr)
        sys.exit(2)
