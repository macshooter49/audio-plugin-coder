#!/usr/bin/env python3
"""tp61 — CAPTURE OFF MEANS NO RINGS, AND THE MEMORY COMES BACK.

Max (2026-09-20): "whenever I have capture off in the settings, the stem capture should just be kind of
greyed out ... make sure nothing ghostly is running in the background taking up CPU or memory, especially
memory. We want people to load up multiple instances ... and if I duplicate it then it's two times the memory."

tp43 built the DAW-capture setting and it released the EXPORT ring (~202 MB). The FIVE STEM rings — four
layers plus the master-FX ring, `kStemSeconds` x SR x 2ch x float32 each — were never part of that switch:
they are armed from timerCallback the moment a layer receives a sample, whatever the setting said.

WHY THIS IS A SOURCE GATE AND NOT A RUNTIME ONE, PLAINLY: the rings arm only when a chop layer HAS A
SAMPLE, and nothing in the AU's parameter surface can load one — a sample reaches a layer through a native
function the editor calls (Tests/au_chopsend.cpp documents the same wall for the same reason). So there is
no headless way to make the allocation happen and then weigh it. What CAN be pinned exactly is the set of
places that allocate and the guard on each, which is what this reads — every arm site, the release, and the
arithmetic. A new arm site added without the guard fails here.

    python3 Tests/stem_memory_gate.py              # exit 0 PASS, 1 FAIL, 2 stale anchors
    python3 Tests/stem_memory_gate.py --controls   # every mutation below must go red
    STEM_MUT=noguard_layer | noguard_all | norelease | noctor  -> must exit 1
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CPP  = os.path.join(HERE, '..', 'Source', 'PluginProcessor.cpp')
HDR  = os.path.join(HERE, '..', 'Source', 'PluginProcessor.h')
MUT  = os.environ.get('STEM_MUT', '')

def read(p):
    with open(p, encoding='utf-8') as f: return f.read()

def strip_comments(s):
    s = re.sub(r'/\*[\s\S]*?\*/', lambda m: '\n' * m.group(0).count('\n'), s)
    s = re.sub(r'//[^\n]*', '', s)
    return s

def body_of(src, sig):
    """The braced body of a function, by its definition line."""
    i = src.find(sig)
    if i < 0: return None
    j = src.find('{', i)
    if j < 0: return None
    depth, k = 0, j
    while k < len(src):
        if src[k] == '{': depth += 1
        elif src[k] == '}':
            depth -= 1
            if depth == 0: return src[j:k + 1]
        k += 1
    return None

cpp_raw, hdr_raw = read(CPP), read(HDR)
if MUT == 'noguard_layer':
    cpp_raw = cpp_raw.replace('    if (! captureEnabled_.load (std::memory_order_acquire)) return;\n    if (stemLayerArmed_', '    if (stemLayerArmed_')
elif MUT == 'noguard_all':
    cpp_raw = cpp_raw.replace('    if (! captureEnabled_.load (std::memory_order_acquire)) return;   // tp61 — a rate change must not re-arm what the setting turned off\n', '')
elif MUT == 'norelease':
    cpp_raw = cpp_raw.replace('    releaseStemBuffers();', '    //releaseStemBuffers();')
elif MUT == 'noctor':
    cpp_raw = cpp_raw.replace('    if (captureOffMarker().existsAsFile()) captureEnabled_.store (false, std::memory_order_release);\n\n    // Spectral-morph', '\n    // Spectral-morph')

cpp, hdr = strip_comments(cpp_raw), strip_comments(hdr_raw)
fails, checks = [], 0
def rule(ok, what, detail=''):
    global checks; checks += 1
    print(('  PASS  ' if ok else '  FAIL  ') + what + (('\n        ' + detail) if detail else ''))
    if not ok: fails.append(what)

print('\ntp61 — THE STEM RINGS FOLLOW THE CAPTURE SETTING\n')

# ── the arithmetic, from the source's own constants ───────────────────────────────────────────
m = re.search(r'kStemSeconds\s*=\s*(\d+)', hdr)
if m is None:
    print('  STALE  kStemSeconds not found in PluginProcessor.h'); sys.exit(2)
secs = int(m.group(1))
per  = secs * 44100 * 2 * 4
rule(True, 'the arithmetic, at 44.1 kHz',
     '%d s x 44100 x 2ch x float32 = %.1f MB per ring; 4 layers + the master ring = %.0f MB'
     % (secs, per / 1e6, 5 * per / 1e6))

# ── [1] every arm site is guarded ─────────────────────────────────────────────────────────────
for fn in ('void TerrainAudioProcessor::ensureStemLayerAllocated',
           'void TerrainAudioProcessor::allocateStemBuffers'):
    b = body_of(cpp, fn)
    if b is None:
        print('  STALE  %s not found' % fn); sys.exit(2)
    guarded = re.search(r'if\s*\(\s*!\s*captureEnabled_\.load[^)]*\)\s*\)?\s*return\s*;', b) is not None
    sizes   = 'ring.setSize' in b
    rule(guarded or not sizes,
         '[1] %s allocates only while capture is ON' % fn.split('::')[-1],
         'sizes a ring: %s   guarded: %s' % (sizes, guarded))

# ── [2] there is no THIRD way in ──────────────────────────────────────────────────────────────
sites = [ln for ln in cpp.splitlines() if 'ring.setSize' in ln]
rule(len(sites) <= 4, '[2] a ring is sized in the two guarded arms and the release, nowhere else',
     '%d ring.setSize lines' % len(sites))

# ── [3] turning it OFF gives the memory back ──────────────────────────────────────────────────
b = body_of(cpp, 'void TerrainAudioProcessor::setCaptureEnabled')
if b is None:
    print('  STALE  setCaptureEnabled not found'); sys.exit(2)
rule('releaseStemBuffers();' in b, '[3] setCaptureEnabled(false) releases the stem rings, not just the export ring')
rule('ring.setSize (0, 0)' in b, '[3b] and hands the pages back rather than only un-publishing them')
ir, iw = b.find('releaseStemBuffers();'), b.find('juce::Thread::sleep')
rule(0 <= ir < iw, '[3c] the rings are un-published BEFORE the sleep the writer needs to finish inside',
     'release at %d, sleep at %d' % (ir, iw))
isz = b.find('ring.setSize (0, 0)')
rule(iw < isz, '[3d] and the memory is freed AFTER it', 'sleep at %d, setSize at %d' % (iw, isz))

rb = body_of(cpp, 'void TerrainAudioProcessor::releaseStemBuffers')
if rb is None:
    print('  STALE  releaseStemBuffers not found'); sys.exit(2)
rule('totalSize.store (0' in rb and 'stemLayerArmed_' in rb and 'stemBuffersArmed_.store (false' in rb,
     '[4] the release un-publishes every ring AND forgets the arms, so capture back ON re-arms clean')

# ── [5] a hidden instance must not pay it either ──────────────────────────────────────────────
ctor = body_of(cpp, 'TerrainAudioProcessor::TerrainAudioProcessor()')
if ctor is None:
    print('  STALE  the constructor was not found'); sys.exit(2)
ic, it = ctor.find('captureOffMarker().existsAsFile()'), ctor.find('startTimerHz')
rule(0 <= ic < it, '[5] the preference is read in the CONSTRUCTOR, before the timer that arms can tick',
     'marker read at %d, startTimerHz at %d' % (ic, it))

print('\n  %d checked, %d FAILED\n' % (checks, len(fails)))
sys.exit(1 if fails else 0)
