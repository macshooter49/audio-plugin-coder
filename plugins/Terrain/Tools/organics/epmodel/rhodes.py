"""rhodes.py — tine electric piano (Rhodes-style), 73-key E1–E7 (MIDI 28–100).

Physics (per key):
  tine      stiff clamped cantilever with a tuning-spring mass: fundamental f0 plus the inharmonic cantilever modes
            (r2 ≈ 6.2–7.6·f0, the "bell"; r3 ≈ 2.8·r2), each with its own decay (the bell dies in < 1–2 s).
            Displacement amplitude of mode k for an impulsive strike ∝ shape_k / ratio_k.
  tonebar   the tuned bar the tine is bolted to: a second, slightly detuned partial at f0 with a longer decay
            → the two-stage decay and the slow ~0.2–0.4 Hz beating of a real tine/tonebar pair.
  hammer    neoprene tip; contact time 1.6 ms (bass) … 0.4 ms (treble), shortened by velocity^0.3 → brighter bell at ff.
  pickup    electromagnetic: flux Φ(x) = 1/((x−x0)²+d²)^1.5 of the pole piece for tine-tip displacement x; the voicing
            offset x0 puts the tine beside the pole axis. Output voltage = dΦ/dt = Φ'(x)·ẋ. Small swings are ~linear,
            big swings cross the pole axis → the asymmetric even-harmonic bark. Then the coil/cable lowpass.
  decay     T60 of the fundamental 16 s (E1) … 1.3 s (E7).
"""
from __future__ import annotations

import math

import numpy as np

from common import (SR, midi_f, interp_key, interp_lin, vel01, contact_pulse, modal, biquad, trim_tail, pre_roll,
                    knock)

NAME = "TineEP"
TITLE = "Tine EP (Waves Crate physical model)"
KEY_LO, KEY_HI = 28, 100
ROOTS = list(range(29, 99, 3))                     # 24 zones, one sample every minor third
LAYERS = [(1, 22, 18), (23, 44, 36), (45, 66, 57), (67, 88, 78), (89, 108, 98), (109, 127, 122)]
REF_LAYER = 4
RELEASE_ROOTS = list(range(30, 99, 6))
AMP_RELEASE = 0.35


def t60_fund(k):
    return interp_key(k, [(28, 16.0), (48, 11.0), (64, 7.0), (76, 4.2), (88, 2.4), (100, 1.3)])


def tine(k, vel, n, var=0):
    f0 = midi_f(k)
    T1 = t60_fund(k)
    beat = interp_lin(k, [(28, 0.18), (64, 0.3), (100, 0.5)])
    r2 = interp_lin(k, [(28, 7.6), (60, 6.8), (100, 6.2)])
    r3 = r2 * 2.83
    T2 = interp_key(k, [(28, 1.8), (60, 0.9), (100, 0.3)])
    T3 = T2 * 0.3
    s2 = interp_lin(k, [(28, 0.8), (55, 1.2), (80, 1.0), (100, 0.7)])   # bell strongest mid-keyboard
    freqs = [f0, f0 + beat, f0 * r2, f0 * r3]
    amps = [0.74, 0.26, s2 / r2, 0.28 / r3]
    t60s = [0.55 * T1, 1.25 * T1, T2, T3]
    x, v = modal(n, freqs, amps, t60s)
    tc = interp_key(k, [(28, 0.9e-3), (64, 0.5e-3), (100, 0.25e-3)]) * vel01(vel) ** -0.3
    p = contact_pulse(tc, shape=1.0)
    x = np.convolve(x, p)[:n]
    v = np.convolve(v, p)[:n]
    return x, v


def amp_of(k, vel):
    a_ff = interp_lin(k, [(28, 1.5), (60, 1.35), (100, 0.7)])
    return a_ff * 10 ** (-1.4 * (1 - vel01(vel)))


def pickup(k, x, v):
    x0 = interp_lin(k, [(28, 0.45), (100, 0.3)])
    u = x - x0
    y = -3.0 * u / (u * u + 1.0) ** 2.5 * v            # dΦ/dt, d = 1 (displacement is in units of the pole gap)
    y = biquad(y, "hp", 28.0, 0.7)
    y = biquad(y, "lp", interp_lin(k, [(28, 4500.0), (100, 8000.0)]), 0.9)
    return y


def render(k, vel, var=0):
    dur = min(12.0, max(2.5, 1.1 * t60_fund(k)))
    n = int(dur * SR)
    x, v = tine(k, vel, n, var)
    a = amp_of(k, vel)
    y = pickup(k, a * x, a * v)
    return trim_tail(pre_roll(y), -66.0)


def render_release(k):
    """Damper felt landing on the ringing tine: a soft (4 ms) felt contact through the same tine + pickup,
    absorbed within ~0.1 s, plus the felt pad's own faint scrape. Played at note-off while the note fades."""
    n = int(0.5 * SR)
    f0 = midi_f(k)
    r2 = interp_lin(k, [(28, 7.6), (60, 6.8), (100, 6.2)])
    x, v = modal(n, [f0, f0 * r2], [1.0, 0.3 / r2], [0.12, 0.05])
    p = contact_pulse(4e-3)
    x = np.convolve(x, p)[:n]
    v = np.convolve(v, p)[:n]
    a = 0.12
    y = pickup(k, a * x, a * v)
    return trim_tail(pre_roll(y), -60.0, min_s=0.15)


# mechanical (acoustic) noises: rendered for the shared noise map; the DI tone above never contains them
NOISE_ZONES = {"low": (28, 51), "mid": (52, 75), "high": (76, 100)}


def render_noises(rng):
    out = []
    for zone, sc in (("low", 0.85), ("mid", 1.0), ("high", 1.2)):
        for i in (1, 2):
            key_modes = [(105 * sc, 1.0, 0.07), (230 * sc, 0.75, 0.05), (505 * sc, 0.5, 0.035),
                         (1140 * sc, 0.28, 0.02), (2650 * sc, 0.12, 0.012)]
            out.append((f"keydown_{zone}_{i}.wav", "on", zone,
                        knock(rng, 0.35, key_modes, 1.2e-3, burst_db=-9, burst_band=(2000, 9000), burst_ms=2.5),
                        "key bottoms on the keybed felt + the neoprene hammer tock on the tine"))
            up_modes = [(140 * sc, 0.8, 0.05), (330 * sc, 0.6, 0.035), (780 * sc, 0.3, 0.02)]
            out.append((f"keyup_{zone}_{i}.wav", "off", zone,
                        knock(rng, 0.3, up_modes, 3.0e-3, burst_db=-16, burst_band=(400, 2500), burst_ms=6.0),
                        "key returns to the rail + damper felt pad lands"))
    return out
