#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  tp58 — EVERY FEED THE EDITOR PUSHES INTO THE PAGE MUST BE PARSEABLE.
#
#  Max: "you now broke the distortion paint smh! now there's no white shaper at all, wtf?"
#
#  🚨 WHY A JS GATE COULD NEVER CATCH IT.  _tp57_curve_gate.js proved the PAINTER works by
#  assigning `window.__dstVizPush = {…}` as a JavaScript object from the test.  The real plugin
#  does not do that: PluginEditor writes the literal text
#        window.__dstVizPush=<getDistortionCurveVizJson()>;
#  into the page and the webview EVALUATES it.  tp57 appended `,"e":[…]` to that builder without
#  closing the `"o":[` array it was inside, so the pushed statement read
#        window.__dstVizPush={…,"o":[0.1,…,0.9,"e":[{…}]]};
#  which is a SYNTAX ERROR, not bad data.  The assignment never ran, `__dstVizPush` stayed
#  undefined, and every distortion card — instance 1 included — fell back to the native poll
#  fb354 had replaced precisely because it dies silently.  Seven green JS bars, no curve anywhere.
#
#  So the gate has to read the C++ that BUILDS the string.  It concatenates every string literal
#  each builder streams, in source order (a `<< expr` carries values, never punctuation, and a
#  loop body's literal is a bare comma), and balances the brackets.  One missing `]` fails it.
#
#      python3 Tests/json_feed_gate.py
# ══════════════════════════════════════════════════════════════════════════════════════════════
import io, re, sys, os

ROOT = os.path.dirname(os.path.abspath(__file__)) + '/..'
FILES = ['Source/PluginProcessor.cpp', 'Source/PluginEditor.cpp']

# a builder is a function body that streams a literal beginning with '{' or '[' into a juce::String
SIG = re.compile(r'^(?:juce::String|static\s+juce::String)\s+[A-Za-z_:]*::?([A-Za-z_][A-Za-z0-9_]*)\s*\(', re.M)
LIT = re.compile(r'<<\s*"((?:[^"\\]|\\.)*)"')
CHR = re.compile(r"<<\s*'((?:[^'\\]|\\.))'")

def body_of(src, start):
    """text of the { … } block that follows the signature at `start`"""
    i = src.find('{', start)
    if i < 0: return ''
    d, j = 0, i
    while j < len(src):
        c = src[j]
        if c == '{': d += 1
        elif c == '}':
            d -= 1
            if d == 0: return src[i:j + 1]
        j += 1
    return ''

def strip_noise(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)         # block comments (they are full of brackets)
    t = re.sub(r'//[^\n]*', '', t)                      # line comments
    return t

def skeleton(body):
    """every streamed literal, in order, unescaped"""
    out = []
    # a builder that seeds its string in the CONSTRUCTOR — juce::String j ("{\"s\":") — opens its
    # object there; without this the skeleton starts mid-object and the builder is skipped entirely.
    seed = re.search(r'juce::String\s+\w+\s*\(\s*"((?:[^"\\]|\\.)*)"\s*\)', body)
    if seed: out.append(seed.group(1).replace('\\"', '"'))
    # `<< "…"` and `+ "…"` both put literal punctuation into the string a builder returns
    for m in re.finditer(r'(?:<<|\+)\s*(?:"((?:[^"\\]|\\.)*)"|\'((?:[^\'\\]|\\.))\')', body):
        s = m.group(1) if m.group(1) is not None else m.group(2)
        out.append(s.replace('\\"', '"').replace("\\\\", "\\").replace("\\n", "\n"))
    return ''.join(out)

def balance(sk):
    """bracket depth walk over the skeleton, ignoring brackets inside JSON strings"""
    stack, inq, esc = [], False, False
    for ch in sk:
        if esc: esc = False; continue
        if ch == '\\': esc = True; continue
        if ch == '"': inq = not inq; continue
        if inq: continue
        if ch in '{[': stack.append(ch)
        elif ch in '}]':
            # A builder with several `return j + "]}"` branches contributes each branch's closers
            # to one flat skeleton, so a SURPLUS of closers says nothing — only a container that is
            # never closed at all is a bug, and that is the one shape that breaks the page.
            if stack: stack.pop()
    return (None if not stack else 'never closed: ' + ''.join(stack)), stack

bad, seen = [], 0
for rel in FILES:
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path): continue
    src = io.open(path, encoding='utf-8').read()
    for m in SIG.finditer(src):
        name = m.group(1)
        body = strip_noise(body_of(src, m.end()))
        sk = skeleton(body)
        if not sk.lstrip().startswith(('{', '[')): continue      # not a JSON builder
        seen += 1
        err, _ = balance(sk)
        line = src[:m.start()].count('\n') + 1
        if err:
            bad.append('  ✗ %s:%d  %s()  %s\n        skeleton: %s' %
                       (rel, line, name, err, (sk[:150] + '…') if len(sk) > 150 else sk))

print('%d JSON feed builder(s) scanned' % seen)
for b in bad: print(b)
if seen == 0:
    print('  ✗ the scanner found NO builders — it has stopped looking at the right thing')
    sys.exit(1)
print('  all balanced' if not bad else '  %d UNBALANCED' % len(bad))
sys.exit(1 if bad else 0)
