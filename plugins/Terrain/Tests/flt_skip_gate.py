#!/usr/bin/env python3
"""tp38 — SILENT VOICE, SILENT FILTERS: the skip must be inaudible and must actually skip.
  [0] a preset is used only if two identical renders are bit-identical (Second Coming is NOT — its sample/stretch engines
      differ run to run — so it cannot be a reference);
  [1] for each deterministic preset, the fixed 8 s scenario — and the same with every oscillator switched OFF at 1 s and
      back ON at 2.5 s (the skip engages on silence, then WAKES with a state reset) — rendered with the skip and with
      TERRAIN_NO_FLT_SKIP set differs by less than 1e-5 (-100 dBFS) at every sample;
  [2] Second Coming with every oscillator OFF costs less with the skip than without (it actually engages).
    python3 Tests/flt_skip_gate.py            (needs /tmp/aucensus3 built from Tests/au_preset_census.cpp)
"""
import os, subprocess, struct, sys, re
H = '/tmp/aucensus3'; fails = 0
def bar(ok, label, detail=''):
    global fails
    print(('  ✓ ' if ok else '  ✗ ') + label + ('' if ok or not detail else '\n        ' + detail)); fails += 0 if ok else 1
def render(preset, path, env, toggle=False):
    e = dict(os.environ); e.update(env); subprocess.run([H, 'render', preset, path] + (['toggle'] if toggle else []), env=e, capture_output=True, check=True)
    with open(path, 'rb') as f: b = f.read()
    return struct.unpack('<%df' % (len(b) // 4), b)
def maxdiff(a, b): n = min(len(a), len(b)); return max(abs(a[i] - b[i]) for i in range(n))
used = 0
for preset in ['Zelda', 'Perfectionist', 'Surf', 'Baby Boi', 'Magnolia']:
    r1 = render(preset, '/tmp/fs_r1.f32', {}); r2 = render(preset, '/tmp/fs_r2.f32', {})
    if maxdiff(r1, r2) > 0: print('  · %-14s not deterministic run to run (max %.2e) — skipped as a reference' % (preset, maxdiff(r1, r2))); continue
    used += 1
    for toggle in (False, True):
        a = render(preset, '/tmp/fs_skip.f32', {}, toggle); b = render(preset, '/tmp/fs_noskip.f32', {'TERRAIN_NO_FLT_SKIP': '1'}, toggle)
        d = maxdiff(a, b)
        bar(d < 1e-5, '[1] %-14s %-16s skip vs no-skip: max |diff| %.2e' % (preset, 'osc off/on at 1s' if toggle else 'plain', d))
bar(used >= 3, '[0] at least three deterministic reference presets (%d)' % used)
def cost(env):
    e = dict(os.environ); e.update(env); out = subprocess.run([H, 'hold', 'Second Coming', '4', '8', 'alloff'], env=e, capture_output=True, text=True).stdout
    m = re.search(r'median (\d+) us/block = ([\d.]+)%', out); return float(m.group(2)) if m else -1
c1, c0 = cost({}), cost({'TERRAIN_NO_FLT_SKIP': '1'})
bar(c1 < c0 * 0.85, '[2] Second Coming, every oscillator OFF: %.1f%% with the skip vs %.1f%% without' % (c1, c0))
print('flt_skip_gate: ' + ('PASS' if fails == 0 else 'FAIL')); sys.exit(1 if fails else 0)
