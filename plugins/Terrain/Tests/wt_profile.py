#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb530 — THE MEASURED CONTENT PROFILER.   python3 Tests/wt_profile.py    (from the plugin ROOT)
#
#  THE LAW IT MEASURES (HARD RULE, Max 2026-08-28):
#
#      A WAVETABLE MAY NOT END ITS CONTENT WHILE ITS LAST HARMONIC IS ABOVE -60 dBc.
#
#  No amplitude law lands on a loud value and then jumps to exactly zero. Only truncation does.
#
#  WHY THIS EXISTS ALONGSIDE harmonic_ceiling_gate.py. That gate GREPS for a hard-coded loop
#  bound — it proves a CAUSE is absent, never that the RESULT is good. `Serum HD` loops to
#  `h <= 96`, DELIVERS 17, and passes any grep: a *multiplicative* Gaussian buried it, no ceiling
#  required. `Formant` declares 96 and delivers 7. A future generator can loop to kMaxHarmonics
#  and still multiply everything to zero at h20 with a perfectly clean grep.
#
#  MEASURED 2026-08-28 — twelve legacy tables delete LOUD content (last harmonic present,
#  next exactly 0.0):
#     Spectral Drift h32 @   0.0 dBc   <- flat spectrum, truncated. The proof case.
#     Static Evolve  h64 @  -1.4       Even        h15 @ -23.5      Prophet Saw h26 @ -25.9
#     Dustbowl       h30 @ -27.4       Jupiter PWM h96 @ -27.8      Juno Str    h30 @ -29.5
#     OB-X Saw       h22 @ -29.7       CS-80 Brass h40 @ -30.5      Moog Sqr    h32 @ -31.0
#     PPG Wave       h64 @ -34.8       Drift       h64 @ -36.1
#  The control group is what makes the metric trustworthy — these END BECAUSE THE MATH ENDED THEM
#  and must keep passing: DX7 EP -82 · Choir -67 · Whisper -63 · Vowel Morph -72 · Serum HD -176 ·
#  Sweep -858 · Formant -823.
#
#  🚨 COUNT IS NOT RICHNESS — READ Neff, NEVER N60. By participation ratio Prophet Saw scores 5.7
#     and Terra Stack 4.4: Terra Stack is CORRECTLY saw-like, more bandwidth, same character. The
#     genuinely dense ones are Terra Cloud 120.7 · Dust 59.8 · Bar 29.2 · Glass 25.3. N60 is gamed
#     by piling partials at -80 dB; Neff is not.
#
#  🔑 WHY THIS IS A PYTHON DRIVER AND NOT A PLAIN .cpp. The harness needs the 46 table NAMES, and
#     the roster already lives in TEN places (see wt_list_gate.py). A checked-in names header would
#     be an ELEVENTH site that can drift. So the names are extracted here from the ONE source —
#     the SYN_OSC_A_WT_PRESET StringArray — exactly as wt_list_gate.py does it, written to a TEMP
#     header, compiled and run. Nothing to keep in sync.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import re, os, sys, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
proc = open(os.path.join(ROOT, 'Source/PluginProcessor.cpp'), encoding='utf-8', errors='replace').read()
a = proc.index('SYN_OSC_A_WT_PRESET, 1')
b = proc.index('juce::StringArray {', a); e = proc.index('},', b)
seg = re.sub(r'//[^\n]*', '', proc[b:e])          # strip comments — they contain quoted words
names = re.findall(r'"([^"]*)"', seg)
print('roster: %d tables (from the SYN_OSC_A_WT_PRESET StringArray)' % len(names), flush=True)

which = sys.argv[1] if len(sys.argv) > 1 else 'profile'
src   = os.path.join(ROOT, 'Tests', 'wt_profile.cpp' if which == 'profile' else 'wt_edge.cpp')

with tempfile.TemporaryDirectory() as td:
    open(os.path.join(td, 'wtnames.h'), 'w').write(
        'static const char* kWtNames[] = {\n' + ',\n'.join('  "%s"' % n for n in names) + '\n};\n')
    exe = os.path.join(td, 'wt')
    cc = ['clang++', '-O2', '-std=c++17', '-I', os.path.join(ROOT, 'Tests/shim'),
          '-I', os.path.join(ROOT, 'Source'), '-I', td, src, '-o', exe, '-framework', 'Accelerate']
    r = subprocess.run(cc, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr[:4000]); sys.exit(1)
    sys.exit(subprocess.run([exe]).returncode)
