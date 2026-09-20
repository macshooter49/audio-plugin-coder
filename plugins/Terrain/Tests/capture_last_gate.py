#!/usr/bin/env python3
"""fb636e — THE EXPORT CAPTURE IS THE LAST THING THAT TOUCHES THE OUTPUT (clean-up: every rule can fail).

Max: "the terra capture only captures the stuff that isn't affected by the four modes ... we must hear the glitch ...
it captures everything, including the patcher." The capture used to copy the block BEFORE the FLOW Chop/Glitch stages
rewrote it. This gate reads processBlock (comments, string and char literals blanked) and fails unless:
  [1] captureBuffer.writeBlock comes after the FLOW dispatch calls chopStage(); / glitchStage(); — where the stages
      RUN, not where their lambdas are defined;
  [2] NOTHING follows the capture but the profiler tail (TI_PROF(...); tiProf_.end();) — a stage added below it (the
      patcher, a gain, anything) would be outside the export;
  [3] no return at processBlock scope sits between the first output write and the capture, so every block is captured
      (a return inside a lambda only leaves the lambda and does not count);
  [4] writeToMasterFxRing (the WET-stem reference) comes after the dispatch calls and before any later output write —
      the browser auditions are the user checking a sound, not part of any layer.

    python3 Tests/capture_last_gate.py              # from anywhere; exit 0 PASS, 1 FAIL, 2 stale anchors
    python3 Tests/capture_last_gate.py --controls   # every mutation below must behave as stated
    CAPTURE_MUT=early | late | lategain | return | ringlate   -> must exit 1 with its rule red
    CAPTURE_MUT=lambdaret                                     -> must exit 0 (a lambda's return is harmless)
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC  = os.path.join(HERE, '..', 'Source', 'PluginProcessor.cpp')
MUT  = os.environ.get('CAPTURE_MUT', '')

CONTROLS = {'early': (1, '1'), 'late': (1, '2'), 'lategain': (1, '2'), 'return': (1, '3'),
            'ringlate': (1, '4'), 'lambdaret': (0, None)}
if '--controls' in sys.argv:
    bad = 0
    for mut, (want, tag) in CONTROLS.items():
        r = subprocess.run([sys.executable, os.path.abspath(__file__)], env={**os.environ, 'CAPTURE_MUT': mut},
                           capture_output=True, text=True)
        red = re.findall(r'✗ \[(\d)\]', r.stdout)
        ok = r.returncode == want and (tag is None or tag in red)
        bad += not ok
        print(f"  {'✓' if ok else '✗'} CAPTURE_MUT={mut:<9} exit {r.returncode} (want {want}"
              f"{', rule [' + tag + '] red' if tag else ', all green'})  red={red}")
    print('capture_last_gate controls:', 'ALL AS EXPECTED' if not bad else f'{bad} WRONG')
    sys.exit(1 if bad else 0)

def strip_code(code):
    """Blank comments and string/char literals (raw strings too): nothing inside them may count as code or braces."""
    out, i, n = [], 0, len(code)
    while i < n:
        c = code[i]; d = code[i + 1] if i + 1 < n else ''
        if c == '/' and d == '/':
            j = code.find('\n', i); j = n if j < 0 else j
            out.append(' ' * (j - i)); i = j
        elif c == '/' and d == '*':
            j = code.find('*/', i + 2); j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', code[i:j])); i = j
        elif c == 'R' and d == '"' and (i == 0 or not (code[i - 1].isalnum() or code[i - 1] == '_')):
            m = re.match(r'R"([^()\\\s]{0,16})\(', code[i:])
            if not m: out.append(c); i += 1; continue
            close = ')' + m.group(1) + '"'
            j = code.find(close, i + m.end()); j = n if j < 0 else j + len(close)
            out.append('""' + re.sub(r'[^\n]', '', code[i:j])); i = j
        elif c == '"':
            j = i + 1
            while j < n and code[j] not in '"\n':
                j += 2 if code[j] == '\\' else 1
            out.append('""'); i = j + 1
        elif c == "'":
            if i > 0 and code[i - 1].isdigit() and d.isalnum():   # a digit separator (1'000), not a char literal
                out.append(c); i += 1; continue
            j = i + 1
            while j < n and code[j] not in "'\n":
                j += 2 if code[j] == '\\' else 1
            out.append("' '"); i = j + 1
        else:
            out.append(c); i += 1
    return ''.join(out)

def stmt_end(s, pos):
    depth = 0
    for k in range(pos, len(s)):
        ch = s[k]
        if ch in '([{': depth += 1
        elif ch in ')]}': depth -= 1
        elif ch == ';' and depth == 0: return k + 1
    return len(s)

LAMBDA_RE = re.compile(r'\[[^\[\]\n;]*\]\s*(?:\((?:[^()]|\([^()]*\))*\)\s*)?(?:mutable\s*)?(?:noexcept\s*)?'
                       r'(?:->\s*[\w:<>,&*\s]+?\s*)?\{')
def blank_lambdas(s):
    out = list(s)
    for m in LAMBDA_RE.finditer(s):
        prev = s[max(0, m.start() - 80):m.start()].rstrip()
        if not prev or not (prev[-1] in '=(,{;:?' or re.search(r'\breturn$', prev)):
            continue                                            # an array index, not a lambda
        ob = m.end() - 1; depth = 0
        for j in range(ob, len(s)):
            if s[j] == '{': depth += 1
            elif s[j] == '}':
                depth -= 1
                if depth == 0:
                    for t in range(ob + 1, j):
                        if out[t] != '\n': out[t] = ' '
                    break
    return ''.join(out)

code = strip_code(open(SRC, encoding='utf-8').read())
m = re.search(r'\bvoid\s+TerrainAudioProcessor::processBlock\s*\(\s*juce::AudioBuffer\s*<\s*float\s*>', code)
if not m: print('✗ STALE: processBlock (float) not found'); sys.exit(2)
ob = code.index('{', m.end()); depth = 0
for j in range(ob, len(code)):
    if code[j] == '{': depth += 1
    elif code[j] == '}':
        depth -= 1
        if depth == 0: end = j; break
body = code[ob + 1:end]
line0 = code[:ob + 1].count('\n') + 1

CAP_RE   = r'\bcaptureBuffer\s*\.\s*writeBlock\s*\('
RING_RE  = r'\bwriteToMasterFxRing\s*\('
# tp61 — ⚠️ THIS ANCHOR HAD BEEN STALE SINCE tp20 AND THE GATE WAS PROTECTING NOTHING.
#  It wanted `chopStage();` with EMPTY parentheses; tp20 made the stages per-instance, so the real
#  dispatch has read `chopStage (ce.inst - 1);` since 2026-09-16 and this file has exited 2 (stale)
#  on every run since — which is not a failure, so a sweep that only greps for FAIL never saw it.
#  The argument list is now whatever fits on one line.
DISP_RE  = r'\b(?:chop|glitch)Stage\s*\([^;\n]*\)\s*;'
WRITE_RE = (r'getWritePointer\s*\(|\bo[LR]\s*\[[^\]\n]*\]\s*[-+*/]?=(?!=)'
            r'|\b(?:leftChannel|rightChannel)\s*\[[^\]\n]*\]\s*[-+*/]?=(?!=)'
            r'|\bbuffer\s*\.\s*(?:applyGain|applyGainRamp|addFrom|addFromWithRamp|copyFrom|copyFromWithRamp|clear'
            r'|setSample|addSample|reverse)\s*\(')

def anchors(b):
    caps, rings = list(re.finditer(CAP_RE, b)), list(re.finditer(RING_RE, b))
    disp = list(re.finditer(DISP_RE, b))
    if len(caps) != 1 or len(rings) != 1 or not any(x.group(0).startswith('chop') for x in disp) \
            or not any(x.group(0).startswith('glitch') for x in disp):
        print(f'✗ STALE: want 1 capture, 1 ring, chopStage(); and glitchStage(); in processBlock — found '
              f'{len(caps)}, {len(rings)}, {[x.group(0) for x in disp]}'); sys.exit(2)
    return caps[0].start(), rings[0].start(), disp

if MUT:
    if MUT not in CONTROLS: print(f'✗ unknown CAPTURE_MUT={MUT}'); sys.exit(2)
    cs, rs, disp = anchors(body); ce, re_ = stmt_end(body, cs), stmt_end(body, rs)
    if MUT == 'early':        # the capture back above the FLOW stages, where it sat before fb636e
        stmt = body[cs:ce]; b2 = body[:cs] + body[ce:]
        loop = b2.rfind('for', 0, re.search(DISP_RE, b2).start())
        body = b2[:loop] + stmt + '\n    ' + b2[loop:]
    elif MUT == 'late':       # a stage that rewrites the output below the capture
        body = body[:ce] + '\n    for (int i = 0; i < numSamples; ++i) leftChannel[i] *= 0.5f;' + body[ce:]
    elif MUT == 'lategain':
        body = body[:ce] + '\n    buffer.applyGain (0.5f);' + body[ce:]
    elif MUT == 'return':     # a processBlock-scope return that would drop blocks from the export
        body = body[:re_] + '\n    if (numSamples < 0) return;' + body[re_:]
    elif MUT == 'ringlate':   # the ring back below the auditions (the preview leak)
        stmt = body[rs:re_]; b2 = body[:rs] + body[re_:]
        c2 = re.search(CAP_RE, b2).start()
        body = b2[:c2] + stmt + '\n    ' + b2[c2:]
    elif MUT == 'lambdaret':  # a return inside a lambda between the stages and the capture: harmless, must stay green
        body = body[:re_] + '\n    auto capGateProbe = [&] (int k) { if (k < 0) return; };' + body[re_:]

cs, rs, disp = anchors(body)
ce = stmt_end(body, cs)
dN = disp[-1].end()
ln = lambda pos: line0 + body[:pos].count('\n')
fails = []
def rule(tag, ok, text, detail=''):
    print(f"  {'✓' if ok else '✗'} [{tag}] {text}" + (f'\n        {detail}' if detail and not ok else ''))
    if not ok: fails.append(tag)

print(f'  processBlock: {body.count(chr(10))} lines (from line {line0}); capture @{ln(cs)}, ring @{ln(rs)}, '
      f'FLOW dispatch {len(disp)} calls ending @{ln(dN)}' + (f'   [CAPTURE_MUT={MUT}]' if MUT else ''))
rule(1, cs > dN, 'the capture comes after the FLOW dispatch (chopStage(); / glitchStage(); — where the stages run)',
     f'capture @{ln(cs)} but the last dispatch call ends @{ln(dN)}')
tail = re.sub(r'\bTI_PROF\s*\([^;]*\)\s*;|\btiProf_\s*\.\s*end\s*\(\s*\)\s*;|;', ' ', body[ce:])
rule(2, not tail.strip(), 'nothing after the capture but the profiler tail — the export is exactly what the host receives',
     'found after it: ' + ' '.join(tail.split())[:160])
first = re.search(WRITE_RE, body)
w0 = first.start() if first else 0
rets = [x.start() for x in re.finditer(r'\breturn\b', blank_lambdas(body)[w0:cs])]
rule(3, not rets, 'no processBlock-scope return between the first output write and the capture (lambdas excluded)',
     'return @' + ', '.join(str(ln(w0 + r)) for r in rets[:4]))
# tp61 — ⚠️ AND THE WINDOW FOR [4] WAS WRONG TOO, WHICH THE STALE ANCHOR HID.
#  It started at the LAST chopStage/glitchStage CALL, but the deferred-dispatch block continues past
#  that call: the non-flow slots in the same loop run through applySlot (which writes L[i]/R[i]), and
#  the claim-sum that follows adds every unconsumed slot back with buffer.addFrom. Those writes ARE
#  the dispatch — the very thing the WET ring is supposed to be taken after — so measuring from the
#  call site made the rule red the moment it could run at all. The window starts where that block
#  ENDS: `juce::ignoreUnused (chopStage, glitchStage, applySlot);`, the line that exists precisely
#  because those three are the dispatch's whole vocabulary. If it is ever removed this goes STALE
#  (exit 2) rather than quietly measuring the wrong span again.
DEND_RE = r'juce::ignoreUnused\s*\(\s*chopStage\s*,\s*glitchStage\s*,\s*applySlot\s*\)\s*;'
_dend = re.search(DEND_RE, body)
if _dend is None:
    print('✗ STALE: the deferred-dispatch end marker '
          '`juce::ignoreUnused (chopStage, glitchStage, applySlot);` is gone — rule [4] has no window'); sys.exit(2)
dEnd = _dend.end()
between = [x.group(0).strip() for x in re.finditer(WRITE_RE, body[dEnd:rs])] if rs > dEnd else []
rule(4, dN < dEnd < rs < cs and not between,
     'the WET-stem ring is after the FLOW dispatch and before any later output write (the auditions)',
     f'ring @{ln(rs)}, dispatch block ends @{ln(dEnd)}, capture @{ln(cs)}; output writes before the ring: {between[:3]}')
print('capture_last_gate:', 'PASS' if not fails else 'FAIL ' + ' '.join(f'[{f}]' for f in fails))
sys.exit(0 if not fails else 1)
