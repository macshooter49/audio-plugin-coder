#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
#  widen/extract_labels.py — build shipped_labels.inc, the corpus the NO-DOUBLES
#  gate in widen_cert.cpp §S checks EVERY published Widen label against.
#
#  fb423.  The fb420 and fb422 Widen had NO no-doubles gate at all: FINDINGS §9
#  was a one-off markdown grep over the 15 strings RENAMES.md had just changed,
#  so the other 80 published labels had never been checked against anything.
#  The family audit then found 12 collisions in this device that I did not.
#  That is round one's failure repeating verbatim — CHECKING WHAT I CHANGED
#  INSTEAD OF THE WHOLE CARD.
#
#  Three blindnesses are fixed here, each one of which hid a real collision
#  from somebody:
#    1. LEADING SPACES.  PluginProcessor.cpp builds the fb418 back-panel labels
#       as `"Chorus" + sfxD + " Motion"`, so the literal is `" Motion"`.  A
#       "quote followed by a capital" pattern skips BOTH strings R6 is named
#       after.  Literals are STRIPPED before the capitalisation test here, and
#       the stripped form is what is recorded.  Self-checked at the bottom.
#    2. SINGLE QUOTES.  index.html option arrays are JS: `'Leaky'`, a SHIPPED
#       Distortion character, hid in single quotes and the dynamics agent only
#       caught it late.  Both quote styles are harvested.
#    3. THE SIBLING fx4 DIRECTORIES + Design/fx3 + *_test.cpp name tables.  A
#       C++-literal grep over Source/ reaches none of index.html's option
#       arrays and none of the *_test.cpp name tables; the integration owner
#       said so himself in the fb423 ruling after making the same mistake.
#
#  Run:  python3 extract_labels.py     (writes shipped_labels.inc beside it)
# ─────────────────────────────────────────────────────────────────────────────
import os, re

ME = os.path.abspath(os.path.dirname(__file__))
TI = os.path.abspath(os.path.join(ME, "..", "..", ".."))          # .../TerrainInstrument

ROOTS = [
    (os.path.join(TI, "Source"),                     "shipped"),      # incl. ui/public/index.html
    (os.path.join(TI, "Tests"),                      "shipped"),
    (os.path.join(TI, "Design", "fx3"),              "shipped fx3"),
    (os.path.join(TI, "Design", "fx4", "eq"),        "sibling fx4"),
    (os.path.join(TI, "Design", "fx4", "dynamics"),  "sibling fx4"),
]
EXT = (".h", ".hpp", ".cpp", ".cc", ".js", ".html", ".htm")

# A quoted literal, double OR single, no escapes inside. The leading space of
# `" Motion"` is INSIDE the capture and removed by .strip() below, never by the
# pattern — that is the whole fb418 fix.
LIT = re.compile(r'"([^"\\\n]{1,48})"' r"|'([^'\\\n]{1,48})'")
OK  = re.compile(r"^[A-Z][A-Za-z0-9 /+&.\-']*$")

def looks_like_label(raw):
    s = raw.strip()
    if not (1 <= len(s) <= 24):   return None
    if not OK.match(s):           return None
    if s.count(" ") > 3:          return None
    return s

def harvest(path, found, kind):
    try:
        txt = open(path, encoding="utf-8", errors="ignore").read()
    except OSError:
        return
    for m in LIT.finditer(txt):
        raw = m.group(1) if m.group(1) is not None else m.group(2)
        lab = looks_like_label(raw)
        if lab is not None:
            found.setdefault(lab, kind)

def main():
    found, files = {}, 0
    for root, kind in ROOTS:
        for dp, _dn, fn in os.walk(root):
            # NEVER eat our own tail: my engine, my worklet and my cert publish MY
            # names; harvesting them would make every name collide with itself.
            if os.path.abspath(dp).startswith(ME):
                continue
            for f in fn:
                if not f.endswith(EXT):        continue
                if f.endswith(".inc"):         continue   # a corpus, not a source
                if f.endswith("_cert.cpp"):    continue   # a HARNESS: holds sibling rename tables
                if f.startswith("probe_"):     continue
                harvest(os.path.join(dp, f), found, kind)
                files += 1

    out = os.path.join(ME, "shipped_labels.inc")
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("// AUTO-EXTRACTED by widen/extract_labels.py — DO NOT HAND-EDIT.\n"
                 "// Corpus: Source/ (incl. ui/public/index.html and every *_test.cpp name\n"
                 "//         table) · Tests/ · Design/fx3/ · Design/fx4/eq · Design/fx4/dynamics.\n"
                 "// Quoted literals are STRIPPED before the capitalisation test, so the fb418\n"
                 "// strings \" Motion\" / \" Route\" are visible; SINGLE quotes are harvested too,\n"
                 "// which is where the shipped Distortion character 'Leaky' was hiding.\n"
                 "// %d strings from %d files.\n"
                 "static const char* const kShippedLabels[] = {\n" % (len(found), files))
        for k in sorted(found, key=lambda s: (s.lower(), s)):
            fh.write('    "%s",\n' % k.replace("\\", "\\\\").replace('"', '\\"'))
        fh.write("};\n"
                 "static constexpr int kNumShippedLabels ="
                 " (int) (sizeof kShippedLabels / sizeof kShippedLabels[0]);\n")
    print("wrote %s : %d labels from %d files" % (out, len(found), files))
    retired()
    # SELF-CHECK — an extractor that cannot see yesterday's labels cannot protect
    # tomorrow's. These four are the ones that were provably invisible before.
    for probe in ("Motion", "Route", "Leaky", "Tilt", "Sculpt", "Slant", "Chisel", "Gentle"):
        print("   %-8s %s" % (probe, "PRESENT" if probe in found else "ABSENT"))


# ─────────────────────────────────────────────────────────────────────────────
#  retired_labels.inc — the OLD column of every WIDEN row in RENAMES.md, and the
#  NEW column beside it, PARSED FROM RENAMES.md ITSELF.
#
#  Why parse instead of typing the list: 22 stale strings survived downstream into
#  fb422 (`Duo` and `Counter` were still being printed by my own cert, `Stack` was
#  still the Type in the worklet).  A hand-kept list of retired names is exactly the
#  second table that drifts — the geometry the EQ engine deleted.  RENAMES.md is the
#  authority for both columns, so the cert can assert two things it could not before:
#    · every NEW name is actually published by the engine   (the table WAS applied)
#    · no OLD name survives as a label token anywhere downstream (nothing drifted)
# ─────────────────────────────────────────────────────────────────────────────
def retired():
    rn = os.path.join(os.path.dirname(ME), "RENAMES.md")
    rows, inw = [], False
    for line in open(rn, encoding="utf-8"):
        if line.startswith("## "):
            inw = line.startswith("## WIDEN")
            continue
        if not inw or not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 3:
            continue
        old = re.findall(r"`([^`]+)`", cells[1])
        new = re.findall(r"`([^`]+)`", cells[2])
        if not old or not new or "**" not in cells[2]:
            continue                                  # header row / prose row
        for o, n in zip(old, new):
            rows.append((o, n))
    out = os.path.join(ME, "retired_labels.inc")
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("// AUTO-PARSED from Design/fx4/RENAMES.md (both WIDEN tables) by\n"
                 "// widen/extract_labels.py — DO NOT HAND-EDIT. RENAMES.md is the authority.\n"
                 "// kRetiredLabels[i] was renamed to kRenamedTo[i]. %d rows.\n"
                 "static const char* const kRetiredLabels[] = {\n" % len(rows))
        for o, _ in rows: fh.write('    "%s",\n' % o)
        fh.write("};\nstatic const char* const kRenamedTo[] = {\n")
        for _, n in rows: fh.write('    "%s",\n' % n)
        fh.write("};\nstatic constexpr int kNumRenames ="
                 " (int) (sizeof kRetiredLabels / sizeof kRetiredLabels[0]);\n")
    print("wrote %s : %d WIDEN rename rows" % (out, len(rows)))
    for o, n in rows: print("   %-14s -> %s" % (o, n))

main()
