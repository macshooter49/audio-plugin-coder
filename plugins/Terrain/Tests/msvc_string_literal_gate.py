#!/usr/bin/env python3
"""fb636b — MSVC CAPS ONE STRING LITERAL AT 16,380 BYTES (error C2026).

The editor injects its overlay as juce::String pieces of R"TIHX(...)TIHX" raw literals, each kept under
~15 KB for exactly this reason. fb636's page work grew one piece to 18,502 bytes: the Mac (clang) built it,
the Windows CI died with "string too big, trailing characters truncated". This gate fails if ANY raw string
literal in Terrain's Source exceeds the limit, so the next time it is caught on the Mac, not after a push.

    python3 Tests/msvc_string_literal_gate.py              # from plugins/Terrain
    MSVC_LIT_MUT=tight python3 Tests/msvc_string_literal_gate.py   # control: a 10 KB limit must go red
"""
import glob, os, re, sys

LIMIT = 10000 if os.environ.get('MSVC_LIT_MUT') == 'tight' else 16380
RAW = re.compile(r'R"([^(\s"\\]{0,16})\(')
over = []
count = 0
for f in sorted(glob.glob('Source/**/*.cpp', recursive=True) + glob.glob('Source/**/*.h', recursive=True)):
    text = open(f, encoding='utf-8', errors='replace').read()
    for m in RAW.finditer(text):
        end = text.find(')' + m.group(1) + '"', m.end())
        if end < 0:
            continue
        count += 1
        n = len(text[m.end():end].encode('utf-8'))
        if n > LIMIT:
            over.append((f, text.count('\n', 0, m.start()) + 1, n))
for f, line, n in over:
    print(f'  ✗ {f}:{line}  raw string literal of {n} bytes > {LIMIT} (MSVC C2026)')
print(f'{count} raw string literals scanned, {len(over)} over {LIMIT} bytes')
sys.exit(1 if over else 0)
