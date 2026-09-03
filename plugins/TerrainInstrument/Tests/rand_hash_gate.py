#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  rand_hash_gate.py — fb572: ONE RANDOM, ONE HASH. The page draws a Random route's comet from the
#  sounding note's SEED with a JS twin of SynthModConfig.h's randForRoute; the comet is a display of
#  the DSP's draw only while the two functions agree to the bit. 512 (seed, dest, ordinal) triples,
#  seeds spanning 0 · 1 · 2^31 · 2^32-1 · a spread, dests 0..2000, ordinals 0..3.
#
#    python3 Tests/rand_hash_gate.py            # from the plugin root; needs clang++ and node
#    RHG_MUTATE=1 python3 Tests/rand_hash_gate.py   # the JS twin's second multiplier off by one → red
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
html = open(os.path.join(ROOT, 'Source', 'ui', 'public', 'index.html'), encoding='utf-8').read()
m1 = re.search(r'function modHashMix\(h\)\{[^\n]*\n', html); m2 = re.search(r'function randForRoute\(seed,dest,k\)\{[^\n]*\n', html)
if not (m1 and m2): print('  ❌ rand_hash_gate: the page has no modHashMix / randForRoute twin'); sys.exit(1)
js = m1.group(0) + m2.group(0)
if os.environ.get('RHG_MUTATE') == '1': js = js.replace('0xC2B2AE35', '0xC2B2AE34')
triples = []
seeds = [0, 1, 2**31, 2**32 - 1, 0xDEADBEEF, 12345, 987654321, 0x7FFFFFFF]
for s in seeds:
    for d in (0, 1, 4, 42, 64, 697, 1878, 2000):
        for k in range(4): triples.append((s, d, k))
for i in range(512 - len(triples)): triples.append(((i * 2654435761) % 2**32, (i * 7) % 2100, i % 4))
triples = triples[:512]
tmp = tempfile.mkdtemp()
with open(os.path.join(tmp, 't.js'), 'w') as f:
    f.write(js + '\nconst T=' + str([list(t) for t in triples]).replace('(', '[').replace(')', ']') + ';\nfor(const t of T) console.log(randForRoute(t[0]>>>0,t[1],t[2]).toFixed(9));\n')
with open(os.path.join(tmp, 't.cpp'), 'w') as f:
    f.write('#include <cstdio>\n#include <cstdint>\n#include "SynthModConfig.h"\nint main(){ const uint32_t S[]={' + ','.join(str(t[0]) + 'u' for t in triples) + '}; const int D[]={' + ','.join(str(t[1]) for t in triples) + '}; const int K[]={' + ','.join(str(t[2]) for t in triples) + '};\n for(int i=0;i<512;++i) printf("%.9f\\n",(double)wc::randForRoute(S[i],D[i],K[i])); return 0; }\n')
try:
    subprocess.run(['clang++', '-std=c++17', '-O2', '-I', os.path.join(ROOT, 'Tests', 'shim'), '-I', os.path.join(ROOT, 'Source'), os.path.join(tmp, 't.cpp'), '-o', os.path.join(tmp, 't')], check=True, capture_output=True)
except subprocess.CalledProcessError as e:
    print('  ❌ rand_hash_gate: the JUCE-free compile of SynthModConfig.h failed\n' + e.stderr.decode()[:600]); sys.exit(1)
cpp = subprocess.run([os.path.join(tmp, 't')], capture_output=True, text=True).stdout.split()
node = subprocess.run(['node', os.path.join(tmp, 't.js')], capture_output=True, text=True).stdout.split()
bad = [(t, c, j) for t, c, j in zip(triples, cpp, node) if c != j]
if len(cpp) != 512 or len(node) != 512 or bad:
    print('  ❌ rand_hash_gate: %d of 512 triples DISAGREE between C++ and the page (e.g. %s)' % (len(bad), bad[:2])); sys.exit(1)
vals = sorted(float(x) for x in cpp)
print('  ✅ rand_hash_gate: C++ randForRoute and the page\'s twin agree on 512/512 triples  (range %.4f..%.4f, %d distinct)' % (vals[0], vals[-1], len(set(cpp))))
