#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
#  eq/extract_labels.py — rebuild shipped_labels.inc for the NO-DOUBLES gate.
#
#  fb422. The fb420 EQ had NO doubles gate at all and shipped 23 collisions,
#  including `Tilt` and `Sculpt`, which are shipped TAPE FRONT KNOBS.
#  The dynamics agent's extractor existed but was blind twice over
#  (RENAMES.md "The gate that has to exist afterwards"):
#    1. it required the quote to open on a capital, so `" Motion"` and `" Route"` —
#       the two fb418 strings, built as "Chorus" + sfxD + " Motion" — were skipped.
#       Fixed here: the literal is STRIPPED before the capitalisation test, and the
#       stripped form is what gets recorded.
#    2. it read Source/ only, so five cross-sibling fx4 collisions survived.
#       Fixed here: the two sibling fx4 directories are sources too.
# ─────────────────────────────────────────────────────────────────────────────
import os, re, sys

TI  = os.path.abspath (os.path.join (os.path.dirname (__file__), '..', '..', '..'))
ME  = os.path.abspath (os.path.dirname (__file__))

ROOTS = [ (os.path.join (TI, 'Source'),               'shipped'),
          (os.path.join (TI, 'Design', 'fx3'),        'shipped'),
          (os.path.join (TI, 'Design', 'fx4', 'widen'),    'sibling fx4'),
          (os.path.join (TI, 'Design', 'fx4', 'dynamics'), 'sibling fx4') ]
EXT = ('.h', '.hpp', '.cpp', '.js', '.html', '.inc')

# a quoted literal, single or double quoted, no escapes inside
LIT = re.compile (r'"([^"\\\n]{1,40})"' r"|'([^'\\\n]{1,40})'")

def looks_like_label (s):
    s = s.strip()
    if not (1 <= len (s) <= 24):        return None
    if not re.match (r"^[A-Z][A-Za-z0-9 /+\-\.']*$", s): return None
    if s.count (' ') > 3:               return None
    return s

def main():
    found = {}
    for root, kind in ROOTS:
        for dp, dn, fn in os.walk (root):
            if os.path.abspath (dp).startswith (ME): continue      # never eat our own
            for f in fn:
                if not f.endswith (EXT): continue
                p = os.path.join (dp, f)
                try: txt = open (p, encoding='utf-8', errors='ignore').read()
                except Exception: continue
                for m in LIT.finditer (txt):
                    lit = m.group (1) if m.group (1) is not None else m.group (2)
                    lab = looks_like_label (lit)
                    if lab is None: continue
                    found.setdefault (lab, kind)
    out = os.path.join (ME, 'shipped_labels.inc')
    with open (out, 'w') as fh:
        fh.write ('// AUTO-EXTRACTED by eq/extract_labels.py — DO NOT HAND-EDIT.\n'
                  '// Sources: Source/ (incl. ui/public/index.html) · Design/fx3/ ·\n'
                  '//          Design/fx4/widen/ · Design/fx4/dynamics/   (the two siblings).\n'
                  '// Leading/trailing space is STRIPPED before the capitalisation test, so the\n'
                  '// fb418 strings " Motion" / " Route" are visible to the gate.\n'
                  'static const char* const kShippedLabels[] = {\n')
        for k in sorted (found):
            fh.write ('    "%s",\n' % k.replace ('\\', '\\\\').replace ('"', '\\"'))
        fh.write ('};\n')
    print ('wrote %s : %d labels' % (out, len (found)))
    for probe in (' Motion', ' Route', 'Motion', 'Route', 'Tilt', 'Sculpt', 'Slant', 'Chisel'):
        print ('   %-10r %s' % (probe, 'present' if probe.strip() in found else 'ABSENT'))

main()
