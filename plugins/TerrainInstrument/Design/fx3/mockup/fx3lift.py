#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fb399 — the shared LIFT. Every fx3 mockup is built out of the SHIPPED card, never out of a
retyped lookalike (fb395's mistake, and Max's "that's not even our buttons").

Nothing here invents CSS or markup. It pulls, verbatim, from Source/ui/public/index.html:
  theme_vars()  the #syn-panel custom properties  (variables ONLY — the same rule carries the
                panel's own position:absolute/display:flex layout, and dragging that in yanks
                the card out of flow and collapses the page to 0x0)
  card_css()    every rule mentioning .fxr- / .dst-curve / mvBreathe
  knob_svg()    knobSVG() itself
  glyphs()      IC_PLUS / IC_ARROW / IC_X

CSS is tokenised by MATCHING BRACES over comment-stripped text, never by regex: a regex over
raw CSS matches ".fxr-core" inside a /* comment */, runs to the next "{", and emits a fragment
with a stray brace — which left the sheet 9 braces unbalanced, derailed the parser, and
silently dropped every rule after it.
"""
import io, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
FX3  = os.path.dirname(HERE)
PLUG = os.path.dirname(os.path.dirname(FX3))
IDX  = os.path.join(PLUG, 'Source', 'ui', 'public', 'index.html')


def _src():
    return io.open(IDX, encoding='utf-8').read()


def _strip_comments(css):
    return re.sub(r'/\*.*?\*/', '', css, flags=re.S)


def _top_level_rules(css):
    out, i, n = [], 0, len(css)
    while i < n:
        j = css.find('{', i)
        if j < 0:
            break
        sel = css[i:j].strip()
        depth, k = 1, j + 1
        while k < n and depth:
            if css[k] == '{':
                depth += 1
            elif css[k] == '}':
                depth -= 1
            k += 1
        if depth == 0 and sel:
            out.append((sel, css[j:k]))
        i = k
    return out


def theme_vars(src=None):
    """The panel's custom properties, published on :root AND #syn-panel.

    On :root as well because --text-primary is declared on #syn-panel only, so a page that
    writes body{color:var(--text-primary)} resolves an UNDEFINED var, falls back to `initial`,
    and every currentColor consumer inherits BLACK — which is what turned knobSVG's value arc
    black on the fb397 card."""
    src = src or _src()
    block = None
    for m in re.finditer(r'#syn-panel\s*\{[^}]*\}', src):
        if '--purple-400' in m.group(0) and '--border-strong' in m.group(0):
            block = m.group(0)
            break
    assert block, 'could not find the #syn-panel theme-variable block'
    vs = re.findall(r'(--[A-Za-z0-9\-]+\s*:\s*[^;]+;)', block)
    assert len(vs) > 8, 'theme block yielded only %d variables' % len(vs)
    return ':root,#syn-panel{\n  color:var(--text-primary);\n  ' + \
           '\n  '.join(v.strip() for v in vs) + '\n}'


def card_css(src=None):
    src = src or _src()
    rules = []
    for blk in re.findall(r'<style[^>]*>(.*?)</style>', src, re.S):
        for sel, body in _top_level_rules(_strip_comments(blk)):
            keep = ('.fxr-' in sel) or ('.dst-curve' in sel) or sel.startswith('@keyframes mvBreathe')
            if keep and not sel.startswith('@media'):
                rules.append(sel + body)
    assert len(rules) > 60, 'expected the full rack ruleset, got %d' % len(rules)
    css = '\n'.join(rules)
    assert css.count('{') == css.count('}'), 'lifted CSS is brace-unbalanced'
    return css


def knob_svg(src=None):
    src = src or _src()
    k0 = src.index('function knobSVG(pct,label,glyph){')
    depth, i = 0, k0
    while True:
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    fn = src[k0:i + 1]
    assert 'viewBox="0 0 30 30"' in fn, 'knobSVG did not extract cleanly'
    return fn


def glyphs(src=None):
    src = src or _src()
    def grab(name):
        m = re.search(r"var\s+%s\s*=\s*('(?:[^'\\]|\\.)*');" % name, src)
        assert m, 'missing ' + name
        return m.group(1)
    return grab('IC_PLUS'), grab('IC_ARROW'), grab('IC_X')


def worklet(device):
    p = os.path.join(FX3, device, '%s-worklet.js' % device)
    js = io.open(p, encoding='utf-8').read()
    assert '</script' not in js.lower(), 'worklet contains </script>'
    return js
