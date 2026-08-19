#!/usr/bin/env python3
"""Regenerate shipped_labels.inc — the corpus the no-doubles gate checks against.

Two things were wrong with the first version and both are fixed here:

  1. LEADING SPACES.  PluginProcessor.cpp:4022 builds the fb418 back-panel labels as
     `"Chorus" + sfxD + " Motion"`, so the literal in the source is `" Motion"`.  The old
     "capitalised quoted string" pattern required the first character after the quote to be
     a capital, so it skipped BOTH fb418 strings — `Motion` and `Route`, the two labels R6 is
     named after.  `grep -c '" ' shipped_labels.inc` returned 0.  Strings are now STRIPPED
     before the capitalisation test.

  2. THE SIBLING fx4 DIRECTORIES.  The corpus was Source/ only, so three agents building four
     devices in parallel could not see each other and five cross-sibling collisions survived to
     the audit.  Design/fx4/eq and Design/fx4/widen are now scanned too.

Run:  python3 gen_shipped_labels.py            (writes shipped_labels.inc in this directory)
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TI   = os.path.abspath(os.path.join(HERE, "..", "..", ".."))          # .../TerrainInstrument
ROOTS = [
    (os.path.join(TI, "Source"), (".h", ".hpp", ".cpp", ".html", ".js")),
    # SIBLINGS: their ENGINE HEADERS and worklets only. Their own shipped_labels.inc and their
    # *_cert.cpp are CORPORA and RENAME TABLES — they contain MY names, and scanning them made
    # the gate report `Leaky`, `Gentle` and `Low Split` as collisions with itself.
    (os.path.join(TI, "Design", "fx4", "eq"),    (".h", ".js")),
    (os.path.join(TI, "Design", "fx4", "widen"), (".h", ".js")),
]

# "..." and '...' with no embedded quote or backslash; the leading space is INSIDE the capture.
LIT = re.compile(r'"([^"\\\n]{1,48})"' r"|'([^'\\\n]{1,48})'")
OK  = re.compile(r'^[A-Za-z0-9 ./+&%#\-–’]+$')

def harvest(path):
    out = set()
    try:
        txt = open(path, encoding="utf-8", errors="ignore").read()
    except OSError:
        return out
    for m in LIT.finditer(txt):
        raw = m.group(1) if m.group(1) is not None else m.group(2)
        s = raw.strip()                       # ← the fb418 fix, in one call
        if not s or len(s) > 40:              continue
        if not OK.match(s):                   continue
        if not s[0].isupper():                continue   # tested AFTER stripping
        if s.isdigit():                       continue
        out.add(s)
    return out

def main():
    labels, files = set(), 0
    for root, exts in ROOTS:
        for dirpath, _dirnames, filenames in os.walk(root):
            for fn in filenames:
                if fn.endswith((".inc",)) or fn.endswith("_cert.cpp") or fn.startswith("probe_"):
                    continue
                if fn.endswith(exts):
                    labels |= harvest(os.path.join(dirpath, fn))
                    files += 1
    ordered = sorted(labels, key=lambda s: (s.lower(), s))
    with open(os.path.join(HERE, "shipped_labels.inc"), "w", encoding="utf-8") as f:
        f.write("// AUTO-EXTRACTED by gen_shipped_labels.py — do not hand-edit.\n"
                "// Corpus: Source/ (incl. ui/public/index.html) + the two SIBLING fx4 directories\n"
                "// (Design/fx4/eq, Design/fx4/widen), which the first version could not see —\n"
                "// five cross-sibling collisions survived to the family audit because of it.\n"
                "// Quoted strings are STRIPPED before the capitalisation test, so the fb418\n"
                "// labels `Motion` and `Route` (built as \"Chorus\" + sfxD + \" Motion\") are in.\n"
                "// %d strings from %d files.\n"
                "static const char* const kShippedLabels[] = {\n" % (len(ordered), files))
        for s in ordered:
            f.write('    "%s",\n' % s.replace('\\', '\\\\').replace('"', '\\"'))
        f.write("};\n"
                "static constexpr int kNumShippedLabels = (int) (sizeof kShippedLabels / sizeof kShippedLabels[0]);\n")
    print("%d labels from %d files" % (len(ordered), files))
    for probe in ("Motion", "Route", "Slant", "Chisel", "Steady", "Twofold", "Roam"):
        print("   %-8s %s" % (probe, "PRESENT" if probe in labels else "absent"))

main()
