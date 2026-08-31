#!/usr/bin/env python3
"""fb551 — THE BLEND MODE LIST LIVES IN THREE PLACES AND THEY MUST AGREE.

C++  createParameterLayout()'s `modes` StringArray  — defines the choice cardinality.
JS   `MODES`  in index.html                          — the menu + the readback; BOTH sides
                                                       normalise with (length - 1).
JS   `PILL`   in index.html                          — the compact pill token, one per mode.

fb373's law is exactly this mismatch: a choice param normalised on the DROPDOWN's option
count instead of the PARAM's cardinality builds clean, shows the right menu, and writes the
wrong mode. And fb551 added two modes, so the next person to add one gets a red build instead
of a silent one.
"""
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
cpp  = (root / 'Source' / 'PluginProcessor.cpp').read_text()
js   = (root / 'Source' / 'ui' / 'public' / 'index.html').read_text()

fails = []
m = re.search(r'const juce::StringArray modes \{([^}]*)\}', cpp, re.S)
if not m: fails.append("could not find the C++ `modes` StringArray")
c = [x.strip().strip('"') for x in m.group(1).replace('\n', ' ').split(',') if x.strip()] if m else []

m2 = re.search(r"var MODES = \[([^\]]*)\]", js)
if not m2: fails.append("could not find the JS `MODES` array")
j = [x.strip().strip("'") for x in m2.group(1).split(',')] if m2 else []

m3 = re.search(r"var PILL  = \[([^\]]*)\]", js)
if not m3: fails.append("could not find the JS `PILL` array")
p = [x.strip().strip("'") for x in m3.group(1).split(',')] if m3 else []

if c and j and c != j:
    fails.append("C++ modes != JS MODES\n     C++: %s\n     JS : %s" % (c, j))
if c and p and len(c) != len(p):
    fails.append("PILL has %d entries for %d modes — every mode needs a pill token" % (len(p), len(c)))

# every mode with a DSP law must also be reachable: JS famMode() decides whether the menu
# offers a SOURCE, and a mode with no source picker is a mode nobody can use (fb470).
mf = re.search(r"function famMode \(m\) \{ return ([^;]*); \}", js)
if not mf: fails.append("could not find the JS famMode() predicate")
else:
    fam = mf.group(1)
    for idx, name in enumerate(c):
        if name in ('Off', 'Sync', 'Warp', 'Dist', 'Filter'):   # frozen / not offered
            continue
        ok = ("m === %d" % idx) in fam or (idx <= 4 and "m >= 1 && m <= 4" in fam)
        if not ok:
            fails.append('mode %d "%s" is live but famMode() excludes it — the menu would offer no SOURCE' % (idx, name))

if fails:
    print("  ❌ blend mode gate")
    for f in fails: print("     " + f)
    sys.exit(1)
print("  ✅ %d blend modes — C++ == JS, every mode has a pill token and a source picker" % len(c))
