#!/usr/bin/env python3
"""THE CONSCIOUSNESS-THROUGH-SOUND MARK — static SVG, no raster, no filter.

Geometry measured off Max's artwork (1080 canvas): the innermost head is 0.49x the outermost in
both axes, so five layers give a ratio of 0.49**0.25 = 0.837 per step. The pivot is biased RIGHT
and DOWN of centre, which is what makes the ripple grow forward past the face and up over the
crown -- the thing that reads as sound leaving a head, rather than as a flat onion.

Each layer is a true ANNULUS (a mask of head_i with head_i+1 punched out), never a stacked solid.
Two reasons: the rings stay independently translucent, and THE CORE IS A REAL HOLE -- whatever
surface is behind the mark shows through it. On a header that is itself transparent, the mark
changes with the page exactly like everything else does.

The waveform is the CURRENT logo's, bar for bar: the mark Terrain already ships is this same
9-bar wave, and the new symbol wraps it in the head rather than replacing it.
"""
import json, sys

VB_W, VB_H = 100.0, 126.0
VB_X       = 9.0          # the ink starts at the nose tip; crop to it
PX, PY     = 63.0, 82.0        # pivot: right and low, so the ripple grows forward and up
R          = 0.837
N          = 5

HEAD = ("M50 1"
        "C77.5 1 99 20 99 44.5"               # crown -> back of skull
        "C99 58.5 96 67.5 92.2 74.2"          # skull -> nape
        "C89.2 79.2 87.4 83.4 87.4 89"        # nape -> back of neck
        "C87.4 100.5 88.1 114 89.2 126"       # neck down -- a CURVE, not an L: the straight
        "L47.6 126"                           #   segment kinked visibly against the nape curve
        "C47.6 113.6 48.4 103.4 45.6 96.6"    # neck front rising
        "C43.2 90.8 37.2 88.2 31.4 84.8"      # JAW sweeping forward
        "C27.6 82.6 25.2 80.6 23.6 77.8"      # toward the chin
        "C22.5 75.8 23.8 74 22.2 71.8"        # CHIN, protruding a touch
        "C19.8 68.6 15.2 67.4 14.2 64.2"      # under the nose
        "C13.1 61 9.6 60 10 57"               # NOSE tip
        "C10.5 54.4 18 55 19.1 51"            # bridge, back in
        "C20.1 47.4 17.6 44.8 18.7 40"        # BROW notch
        "C20.2 28.2 23.2 13 34 6"             # forehead
        "C39.4 2.4 44 1 50 1Z")

# the shipping logo's own rhythm, read off the 128px source
BARS = [0.40, 0.62, 0.88, 0.55, 0.60, 1.00, 0.72, 0.50, 0.38]

def tf(i):
    s = R ** i
    return "translate(%.4g %.4g) scale(%.4f) translate(%.4g %.4g)" % (PX, PY, s, -PX, -PY)

def core():
    s = R ** (N - 1)
    return PX + s * (55.0 - PX), PY + s * (50.0 - PY), s

def wave(cx, cy, s, fill, k=1.62):
    span, tall, w = 31.0 * s * k, 27.5 * s * k, 2.65 * s * k
    gap = span / (len(BARS) - 1)
    return "".join('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="%.2f"/>'
                   % (cx - span/2 + i*gap - w/2, cy - tall*h/2, w, tall*h, w/2)
                   for i, h in enumerate(BARS))

def build(uid, cls, rings, coreFill, waveGrad, glow, glowA=.5):
    cx, cy, s = core()
    p = ['<svg class="tmk %s" viewBox="%g 0 %g %g" xmlns="http://www.w3.org/2000/svg" '
         'xmlns:xlink="http://www.w3.org/1999/xlink" role="img" aria-label="Terrain">'
         % (cls, VB_X, VB_W - VB_X, VB_H)]
    p.append('<defs><path id="h%s" d="%s"/>' % (uid, HEAD))
    p.append('<radialGradient id="gl%s"><stop offset="0" stop-color="%s" stop-opacity="%.2f"/>'
             '<stop offset="1" stop-color="%s" stop-opacity="0"/></radialGradient>' % (uid, glow, glowA, glow))
    # the wave carries the shipping logo's lavender -> white -> lavender sweep
    p.append('<linearGradient id="wv%s" x1="0" y1="0" x2="1" y2="0">'
             '<stop offset="0" stop-color="%s"/><stop offset=".5" stop-color="%s"/>'
             '<stop offset="1" stop-color="%s"/></linearGradient>' % ((uid,) + waveGrad))
    for i in range(N - 1):
        p.append('<mask id="m%s%d" maskUnits="userSpaceOnUse" x="0" y="0" width="%g" height="%g">'
                 '<use href="#h%s" xlink:href="#h%s" fill="#fff"%s/>'
                 '<use href="#h%s" xlink:href="#h%s" fill="#000" transform="%s"/></mask>'
                 % (uid, i, VB_W, VB_H, uid, uid, '' if i == 0 else ' transform="%s"' % tf(i), uid, uid, tf(i + 1)))
    p.append('</defs>')
    for i in range(N - 1):
        p.append('<rect width="%g" height="%g" fill="%s" mask="url(#m%s%d)"/>' % (VB_W, VB_H, rings[i], uid, i))
    if coreFill:
        p.append('<use href="#h%s" xlink:href="#h%s" fill="%s" transform="%s"/>' % (uid, uid, coreFill, tf(N - 1)))
    p.append('<ellipse cx="%.2f" cy="%.2f" rx="%.1f" ry="%.1f" fill="url(#gl%s)"/>'
             % (cx, cy, 31*s*1.62*.70, 27.5*s*1.62*.66, uid))
    p.append('<g fill="url(#wv%s)">%s</g>' % (uid, wave(cx, cy, s, None)))
    p.append('</svg>')
    return "".join(p)

LAV = ('#b9a5f5', '#ffffff', '#b9a5f5')          # the shipping logo's sweep
W2 = ['rgba(255,255,255,.15)', 'rgba(255,255,255,.28)', 'rgba(255,255,255,.45)', 'rgba(255,255,255,.68)']
W3 = ['rgba(255,255,255,.20)', 'rgba(255,255,255,.36)', 'rgba(255,255,255,.56)', 'rgba(255,255,255,.80)']
out = {
  'D3': build('tkd', 'tmk-dark', W3, None, LAV, '#ffffff'),
  'P':  build('tkl', 'tmk-lite', ['#ece7fa', '#d7cdf3', '#b9a8e8', '#8f76d8'], '#33246e',
              ('#c9b8ff', '#ffffff', '#c9b8ff'), '#a78bfa'),
}
json.dump(out, open(sys.argv[1], 'w'))
for k, v in out.items(): print("%-4s %d bytes" % (k, len(v)))
