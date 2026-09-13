#!/usr/bin/env python3
"""fb636e — THE EXPORT CAPTURE IS THE LAST THING THAT TOUCHES THE OUTPUT.

Max: "the terra capture only captures the stuff that isn't affected by the four modes … we must hear the glitch."
The capture used to copy the block BEFORE the FLOW Chop/Glitch stages rewrote it. This gate reads processBlock and
fails unless captureBuffer.writeBlock and writeToMasterFxRing come AFTER every chop.process / glitch.process call and
every later write into the output buffer, with no return statement in between.

    python3 Tests/capture_last_gate.py                       # from plugins/Terrain
    CAPTURE_MUT=early python3 Tests/capture_last_gate.py     # control: pretend the capture sits before Glitch — must go red
"""
import os, re, sys
src = open('Source/PluginProcessor.cpp', encoding='utf-8').read()
m = re.search(r'\nvoid TerrainAudioProcessor::processBlock\s*\(', src)
if not m: print('✗ processBlock not found'); sys.exit(1)
start = m.start(); depth = 0; i = src.index('{', start)
for j in range(i, len(src)):
    if src[j] == '{': depth += 1
    elif src[j] == '}':
        depth -= 1
        if depth == 0: end = j; break
body = src[start:end]
# comments out: a comment that merely mentions 'return' (or a call) must not count as code
body = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), body, flags=re.S)
body = re.sub(r'//[^\n]*', '', body)
if os.environ.get('CAPTURE_MUT') == 'early':   # the control: move the capture back above Glitch
    cap = body.find('captureBuffer.writeBlock'); gl = body.find('glitch.process')
    body = body[:gl] + 'captureBuffer.writeBlock (EARLY);\n' + body[gl:cap].replace('captureBuffer.writeBlock', 'cb_moved') + body[cap:].replace('captureBuffer.writeBlock', 'cb_moved', 1)
def last(pattern):
    hits = [x.start() for x in re.finditer(pattern, body)]
    return hits[-1] if hits else -1
cap  = last(r'captureBuffer\.writeBlock')
ring = last(r'writeToMasterFxRing\s*\(')
stages = {'chop.process': last(r'\bchop\.process\s*\('), 'glitch.process': last(r'\bglitch\.process\s*\('),
          'last output write': last(r'getWritePointer\s*\(|\bo[LR]\[i\]\s*\+=')}
ok = True
print(f'  processBlock: {body.count(chr(10))} lines')
for name, pos in stages.items():
    good = pos >= 0 and cap > pos and ring > pos
    ok &= good
    print(f"  {'✓' if good else '✗'} capture + masterFx ring after {name}")
rets = [x for x in re.finditer(r'\breturn\b', body[max(stages.values()):cap])] if cap > 0 else []
ok &= not rets
print(f"  {'✓' if not rets else '✗'} no return between the last audio stage and the capture")
print('capture_last_gate:', 'PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)
