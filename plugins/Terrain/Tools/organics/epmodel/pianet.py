"""pianet.py — sticky-pad reed electric piano (Hohner-Pianet-T-style), 61-key F1–F6 (MIDI 29–89).

Physics (per key):
  pluck     no hammer: a sticky leather/foam pad rests on the reed; the key lifts it, the adhesion drags the reed up
            (a slow ~8 ms pre-pull) until it peels off (release over ~1–3 ms, faster for a faster key). The reed then
            rings from a static tip deflection → almost only the fundamental (the 2nd cantilever mode gets
            ≈ 1/r2² of a static load) = the round, mellow Pianet tone. Deflection grows only gently with key speed
            (the adhesion limits it) → a narrow dynamic range.
  reed      clamped-free steel reed, long sustain (T60 ≈ 40 s at F1 … 1.6 s at E6, measured on a reference Pianet).
  pickup    electromagnetic: Φ(x) = 1/((x−x0)²+1)^1.5, output dΦ/dt, then the passive output's high-pass.
  release   the pad falls back onto the reed: a soft pad contact that damps it within ~0.1 s.
Pickup offset, drive, peel time and output high-pass were fitted by numbers (fit.py pianet) to MEASUREMENTS of a
reference recording; the renderer never uses reference audio.
"""
from __future__ import annotations

import numpy as np

from common import (SR, midi_f, interp_key, interp_lin, vel01, contact_pulse, modal, biquad, trim_tail, pre_roll,
                    knock)

NAME = "PadReedEP"
TITLE = "Pad Reed EP (Waves Crate physical model)"
KEY_LO, KEY_HI = 29, 89
ROOTS = list(range(30, 89, 3))
LAYERS = [(1, 25, 20), (26, 50, 40), (51, 75, 63), (76, 100, 88), (101, 127, 118)]
REF_LAYER = 3
RELEASE_ROOTS = list(range(31, 89, 6))

P = {  # fitted by fit.py pianet (33 reference measurements, err 775 → 48); vexp raised from the fitted 0.15 to 0.3
    "x0": 0.2229, "vexp": 0.3,     # so the key speed still moves the level a little (the adhesion caps the pluck)
    "a_ff": [[29, 0.8798], [57, 0.399], [89, 0.0353]], "hp": 22.69, "m2": 0.0308, "peel": 0.989}
FIT = [(("x0",), 0.05, 1.5, False), (("vexp",), 0.15, 1.5, False),
       (("a_ff", 0, 1), 0.05, 3.0, True), (("a_ff", 1, 1), 0.05, 3.0, True), (("a_ff", 2, 1), 0.02, 3.0, True),
       (("hp",), 20.0, 400.0, True), (("m2",), 0.01, 3.0, True), (("peel",), 0.2, 5.0, True)]


def t60_fund(k):
    return interp_key(k, [(29, 40.0), (37, 22.0), (45, 13.0), (57, 10.4), (65, 6.4), (73, 5.0), (81, 3.6),
                          (89, 1.6)])


def amp_of(k, vel, p=P):
    return interp_lin(k, p["a_ff"]) * 10 ** (-p["vexp"] * (1 - vel01(vel)))


def reed(k, vel, n, p=P):
    f0 = midi_f(k)
    T1 = t60_fund(k)
    r2 = interp_lin(k, [(29, 6.5), (89, 6.0)])
    freqs = [f0, f0 * (1 + 0.3 / 1200), f0 * r2]
    amps = [0.85, 0.15, p["m2"] / (r2 * r2)]
    t60s = [0.8 * T1, 1.5 * T1, interp_key(k, [(29, 1.0), (89, 0.15)])]
    ph = np.full(3, np.pi / 2)                         # released from a static deflection: cos phase
    x, v = modal(n, freqs, amps, t60s, phases=ph)
    peel = p["peel"] * interp_key(k, [(29, 2.5e-3), (89, 0.8e-3)]) * (1.4 - 0.8 * vel01(vel))
    pl = contact_pulse(peel, shape=1.0)
    D = float(x[0])                                    # the static deflection at the moment of peeling
    x = D + np.convolve(x - D, pl)[:n]                 # held at D, let go over the peel time
    v = np.convolve(v, pl)[:n]
    # the pre-pull: the pad drags the reed up to its deflection over ~8 ms before it lets go
    npre = int(0.008 * SR)
    tt = np.linspace(0, 1, npre)
    xpre = D * (3 * tt ** 2 - 2 * tt ** 3)
    vpre = np.gradient(xpre) * SR
    return np.concatenate([xpre, x]), np.concatenate([vpre, v])


def pickup(k, x, v, p=P):
    u = x - p["x0"]
    y = -3.0 * u / (u * u + 1.0) ** 2.5 * v
    y = biquad(y, "hp", p["hp"], 0.7)
    y = biquad(y, "lp", 7000.0, 0.7)
    return y


def render(k, vel, p=P, dur=None):
    if dur is None:
        dur = min(10.0, max(2.0, 0.9 * t60_fund(k)))
    n = int(dur * SR)
    x, v = reed(k, vel, n, p)
    a = amp_of(k, vel, p)
    y = pickup(k, a * x, a * v, p)
    return trim_tail(pre_roll(y), -66.0)


def render_release(k):
    """The pad settles back on the ringing reed: soft contact (6 ms), reed damped within ~0.1 s."""
    n = int(0.4 * SR)
    f0 = midi_f(k)
    x, v = modal(n, [f0], [1.0], [0.09])
    pl = contact_pulse(6e-3)
    x, v = np.convolve(x, pl)[:n], np.convolve(v, pl)[:n]
    y = pickup(k, 0.08 * x, 0.08 * v)
    return trim_tail(pre_roll(y), -60.0, min_s=0.12)


NOISE_ZONES = {"low": (29, 49), "mid": (50, 69), "high": (70, 89)}


def render_noises(rng):
    out = []
    for zone, sc in (("low", 0.9), ("mid", 1.0), ("high", 1.12)):
        for i in (1, 2):
            km = [(150 * sc, 1.0, 0.05), (340 * sc, 0.7, 0.035), (760 * sc, 0.45, 0.02), (1700 * sc, 0.25, 0.01)]
            out.append((f"keydown_{zone}_{i}.wav", "on", zone,
                        knock(rng, 0.3, km, 1.5e-3, burst_db=-14, burst_band=(1200, 6000), burst_ms=3.0),
                        "key bottoms + the pad peeling off the reed (acoustic)"))
            um = [(190 * sc, 0.9, 0.04), (450 * sc, 0.5, 0.025)]
            out.append((f"keyup_{zone}_{i}.wav", "off", zone,
                        knock(rng, 0.25, um, 3.5e-3, burst_db=-18, burst_band=(400, 2000), burst_ms=6.0),
                        "key returns, the pad lands on the reed (acoustic)"))
    return out
