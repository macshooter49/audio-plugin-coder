#!/usr/bin/env python3
# fb395 — inline the three worklets into the mockup so it is ONE self-contained .html that
# opens by double-click, forever, with no server. The worklets stay single-source in their
# own directories; this only copies them in at build time.
#   python3 build.py   ->  fx3-mockup.html
import io, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FX3  = os.path.dirname(HERE)
SRC  = os.path.join(HERE, 'fx3-mockup.src.html')
OUT  = os.path.join(HERE, 'fx3-mockup.html')

WK = [('/*CHORUS*/',  'chorus/chorus-worklet.js'),
      ('/*FLANGER*/', 'flanger/flanger-worklet.js'),
      ('/*PHASER*/',  'phaser/phaser-worklet.js')]

html = io.open(SRC, encoding='utf-8').read()
for token, rel in WK:
    path = os.path.join(FX3, rel)
    js = io.open(path, encoding='utf-8').read()
    # a literal </script> inside the worklet would close the host tag early; verified absent,
    # but assert rather than trust — this is the kind of thing that fails silently.
    assert '</script' not in js.lower(), 'worklet %s contains </script>' % rel
    assert token in html, 'placeholder %s missing from source' % token
    html = html.replace(token, js)
    print('  inlined %-28s %6d lines' % (rel, js.count('\n') + 1))

io.open(OUT, 'w', encoding='utf-8').write(html)
print('wrote %s  (%.1f KB)' % (OUT, len(html.encode('utf-8')) / 1024.0))
