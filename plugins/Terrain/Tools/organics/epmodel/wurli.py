"""wurli.py — reed electric piano (Wurlitzer-200A-style), 64-key A1–C7 (MIDI 33–96).

Physics (per key):
  reed      steel clamped-free reed with a solder tip mass: fundamental + the inharmonic 2nd cantilever mode (≈6.3·f0,
            the short metallic "bite" of the attack), two decay components (reed + the reed-bar it is screwed to).
  hammer    felt: contact 1.5 ms (bass) … 0.5 ms (treble), shortened by velocity^0.3.
  pickup    ELECTROSTATIC. The reed is one plate of a DC-biased capacitor and swings in and out of a slot in the
            pickup comb. The coupling peaks with the reed tip centred in the slot and falls off either side:
            C(x) = (1+((x−x0)/w)²)^−q, x0 = the reed's voicing offset. Small swings stay on the slope (pp: nearly a
            sine); a hard hit swings the reed through the peak and out of the slot on both sides → the mostly-odd,
            partly-even clipping = the H3-heavy bark/growl. Output current = V·dC/dt.
  preamp    the 200A's coupling high-pass (thin bass: the low reeds' harmonics outweigh their fundamental, exactly as
            measured), and the output low-pass.
Parameters were fitted by numbers (H2/H1, H3/H1, attack centroid) to MEASUREMENTS of reference recordings; no
reference audio is used by the renderer (see validate.py / VALIDATION.md).
"""
from __future__ import annotations

import numpy as np

from common import (SR, midi_f, interp_key, interp_lin, vel01, contact_pulse, modal, biquad, softplus, trim_tail,
                    pre_roll, knock)

NAME = "ReedEP"
TITLE = "Reed EP (Waves Crate physical model)"
KEY_LO, KEY_HI = 33, 96
ROOTS = list(range(34, 96, 3))                     # 21 zones
LAYERS = [(1, 22, 18), (23, 44, 36), (45, 66, 57), (67, 88, 78), (89, 108, 98), (109, 127, 122)]
REF_LAYER = 4
RELEASE_ROOTS = list(range(35, 96, 6))

P = {  # fitted by fit.py wurli (42 reference measurements, err 1448 → 105)
    "x0": 0.4769, "s": 1.0, "vexp": 0.9568,
    "a_ff": [[33, 3.5187], [56, 2.1681], [73, 0.5816], [96, 0.5132]],
    "hp": 85.4, "tc": 0.4506, "bite": 0.7097, "q": 0.9756, "honk": 5.058,
}
FIT = [(("x0",), 0.02, 1.5, False), (("vexp",), 0.6, 2.2, False),
       (("a_ff", 0, 1), 0.1, 4.0, True), (("a_ff", 1, 1), 0.1, 4.0, True), (("a_ff", 2, 1), 0.1, 4.0, True),
       (("a_ff", 3, 1), 0.05, 4.0, True), (("hp",), 50.0, 400.0, True), (("tc",), 0.3, 3.0, True),
       (("bite",), 0.05, 3.0, True), (("q",), 0.4, 6.0, True), (("honk",), 0.0, 14.0, False)]


def t60_fund(k):
    return interp_key(k, [(33, 12.0), (45, 9.0), (61, 6.0), (73, 4.0), (85, 2.8), (96, 1.8)])


def reed(k, vel, n, p=P):
    f0 = midi_f(k)
    T1 = t60_fund(k)
    r2 = interp_lin(k, [(33, 6.6), (96, 6.1)])
    T2 = interp_key(k, [(33, 0.8), (96, 0.18)])
    freqs = [f0, f0 * (1 + 0.4 / 1200), f0 * r2]
    amps = [0.8, 0.2, p["bite"] / r2]
    t60s = [0.8 * T1, 1.6 * T1, T2]
    x, v = modal(n, freqs, amps, t60s)
    tc = p["tc"] * interp_key(k, [(33, 1.5e-3), (96, 0.5e-3)]) * vel01(vel) ** -0.3
    pl = contact_pulse(tc, shape=1.5)
    return np.convolve(x, pl)[:n], np.convolve(v, pl)[:n]


def amp_of(k, vel, p=P):
    return interp_lin(k, p["a_ff"]) * 10 ** (-p["vexp"] * (1 - vel01(vel)))


def pickup(k, x, v, p=P):
    u = (x - p["x0"]) / p["s"]
    q = p["q"]
    dC = -2.0 * q * u / (1.0 + u * u) ** (q + 1)         # dC/dx of C(x) = (1+((x−x0)/w)²)^−q
    y = dC * v                                           # current = V·dC/dt
    y = biquad(y, "hp", p["hp"], 0.6)
    y = biquad(y, "hp", 25.0, 0.7)
    y = biquad(y, "peak", 1200.0, 0.8, p["honk"])       # preamp/voicing mid presence
    y = biquad(y, "lp", 6500.0, 0.7)
    return y


def render(k, vel, p=P, dur=None):
    if dur is None:
        dur = min(10.0, max(2.0, 1.05 * t60_fund(k)))
    n = int(dur * SR)
    x, v = reed(k, vel, n, p)
    a = amp_of(k, vel, p)
    y = pickup(k, a * x, a * v, p)
    return trim_tail(pre_roll(y), -66.0)


def render_release(k):
    """Damper felt lands on the reed (absorbed in ~0.1 s) through the same pickup: the soft 'dut' of a 200A key-up."""
    n = int(0.45 * SR)
    f0 = midi_f(k)
    x, v = modal(n, [f0, f0 * 6.3], [1.0, 0.06], [0.1, 0.04])
    pl = contact_pulse(5e-3)
    x, v = np.convolve(x, pl)[:n], np.convolve(v, pl)[:n]
    y = pickup(k, 0.1 * x, 0.1 * v)
    return trim_tail(pre_roll(y), -60.0, min_s=0.15)


NOISE_ZONES = {"low": (33, 54), "mid": (55, 75), "high": (76, 96)}


def render_noises(rng):
    out = []
    for zone, sc in (("low", 0.9), ("mid", 1.0), ("high", 1.15)):
        for i in (1, 2):
            km = [(130 * sc, 1.0, 0.05), (290 * sc, 0.8, 0.04), (640 * sc, 0.5, 0.03), (1500 * sc, 0.3, 0.015),
                  (3300 * sc, 0.15, 0.008)]
            out.append((f"keydown_{zone}_{i}.wav", "on", zone,
                        knock(rng, 0.3, km, 1.0e-3, burst_db=-8, burst_band=(2500, 10000), burst_ms=2.0),
                        "plastic key bottoms + the felt hammer's action clack"))
            um = [(170 * sc, 0.8, 0.04), (420 * sc, 0.5, 0.03), (950 * sc, 0.3, 0.015)]
            out.append((f"keyup_{zone}_{i}.wav", "off", zone,
                        knock(rng, 0.25, um, 2.5e-3, burst_db=-15, burst_band=(500, 3000), burst_ms=5.0),
                        "key returns + damper felt lands on the reed"))
    return out
