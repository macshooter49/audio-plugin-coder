"""cp.py — electric grand (Yamaha-CP-80-style): short piano strings, felt hammers, piezo bridge pickups, no soundboard.
88 keys A0–C8 (MIDI 21–108).

Physics (per key):
  strings   1 string (bass) or 2 strings (from MIDI 34) per note; stiff-string modes f_n = n·f0·√(1+B n²) with the
            high inharmonicity of short strings (B 1.5e-3 bass … 8e-3 top); frequency-dependent loss
            T60_n = T60_1/(1+(f_n/fd)^1.5).
  unison    each mode = a prompt component (≈60 %, fast decay) + a slightly detuned aftersound component (slow decay):
            the two-stage decay and the beating of detuned unison strings (Weinreich).
  hammer    felt at 1/8 of the string (weak 8th/16th partials); contact 4 ms (bass) … 0.8 ms (treble), strongly
            velocity-dependent (felt stiffens as it is compressed): harder → shorter → brighter.
  pickup    piezo under the bridge: output ∝ bridge force ∝ string slope ∝ n·a_n (no soundboard), + a short thump of
            the action/frame through the piezo.
Hammer contact, loss, pickup tilt and strike point were fitted by numbers (fit.py cp) to MEASUREMENTS of a reference
recording; the renderer never uses reference audio.
"""
from __future__ import annotations

import math

import numpy as np

from common import (SR, midi_f, interp_key, interp_lin, vel01, contact_pulse, modal, biquad, trim_tail, pre_roll,
                    knock)

NAME = "EGrand"
TITLE = "Electric Grand (Waves Crate physical model)"
KEY_LO, KEY_HI = 21, 108
ROOTS = list(range(22, 108, 3))                    # 29 zones
LAYERS = [(1, 30, 24), (31, 58, 45), (59, 84, 72), (85, 106, 96), (107, 127, 120)]
REF_LAYER = 3
RELEASE_ROOTS = list(range(22, 96, 6))              # dampers stop above ~C7 like a real grand
FMAX = 18000.0

P = {  # fitted by fit.py cp (41 reference measurements, err 786 → 236): NOT shipped, see VALIDATION.md
     "tc": [[21, 7.233e-3], [64, 4.419e-3], [108, 0.704e-3]], "tcv": 0.4936,
     "fd": [[21, 700.8], [64, 1778.0], [108, 679.8]], "fexp": 1.485, "beta": 0.1137, "tilt": 0.998,
     "vexp": 0.5, "thump": 0.15}
FIT = [(("tc", 0, 1), 0.3e-3, 10e-3, True), (("tc", 1, 1), 0.2e-3, 8e-3, True), (("tc", 2, 1), 0.1e-3, 6e-3, True),
       (("tcv",), 0.05, 0.9, True), (("fd", 0, 1), 100.0, 8000.0, True), (("fd", 1, 1), 150.0, 10000.0, True),
       (("fd", 2, 1), 300.0, 16000.0, True), (("fexp",), 0.7, 3.5, False), (("beta",), 0.05, 0.25, False),
       (("tilt",), 0.2, 1.8, False)]


def t60_fund(k):
    return interp_key(k, [(21, 30.0), (50, 25.0), (64, 18.0), (76, 8.0), (88, 4.0), (100, 2.4), (108, 1.6)])


def inharm(k):
    return interp_key(k, [(21, 1.5e-3), (40, 3e-4), (60, 4e-4), (84, 1.5e-3), (108, 8e-3)])


def render(k, vel, p=P, dur=None):
    f0 = midi_f(k)
    T1 = t60_fund(k)
    if dur is None:
        dur = min(10.0, max(2.0, 0.8 * T1))
    nfr = int(dur * SR)
    B = inharm(k)
    n = np.arange(1, 200)
    f = n * f0 * np.sqrt(1 + B * n * n)
    keep = f < FMAX
    n, f = n[keep][:120], f[keep][:120]
    v01 = vel01(vel)
    a_n = np.sin(n * np.pi * p["beta"]) / n                    # impulsive felt strike at beta
    force = a_n * n ** p["tilt"]                                # piezo: bridge force ∝ slope
    fd = interp_key(k, p["fd"])
    t60 = T1 / (1.0 + (f / fd) ** p["fexp"])
    two = k >= 34
    cents = interp_lin(k, [(34, 1.2), (108, 0.6)]) if two else 0.0
    fr = np.concatenate([f, f * (1 + cents / 1731.2)]) if two else f
    am = np.concatenate([0.6 * force, 0.4 * force]) if two else force
    tt = np.concatenate([0.35 * t60, 1.3 * t60]) if two else t60
    rng = np.random.default_rng(k * 131 + vel)
    ph = rng.uniform(0, 0.3, len(fr))                           # tiny phase spread between unison strings
    x, _ = modal(nfr, fr, am, tt, phases=ph, want_v=False)
    tc = interp_key(k, p["tc"]) * (p["tcv"] + (1 - p["tcv"]) * (1 - v01) ** 1.3)
    x = np.convolve(x, contact_pulse(tc, shape=1.5))[:nfr]
    x /= np.abs(x).max() + 1e-12
    x *= 10 ** (-p["vexp"] * 2.0 * (1 - v01))
    # action/frame thump seen by the piezo (only the first ~30 ms)
    th = np.zeros(nfr)
    m = int(0.06 * SR)
    t = np.arange(m) / SR
    th[:m] = (np.sin(2 * np.pi * 110 * t) * np.exp(-t / 0.012) + 0.5 * np.sin(2 * np.pi * 270 * t) * np.exp(-t / 0.007))
    y = x + p["thump"] * (x.max() if x.max() > 0 else 1.0) * th * v01
    y = biquad(y, "hp", 35.0, 0.7)
    y = biquad(y, "lp", 12000.0, 0.7)
    return trim_tail(pre_roll(y), -66.0)


def render_release(k):
    """Damper felt landing on the strings: soft contact, strings damped within ~0.15 s; faint felt thud."""
    n = int(0.45 * SR)
    f0 = midi_f(k)
    x, _ = modal(n, [f0, 2 * f0, 3 * f0], [1.0, 0.3, 0.15], [0.15, 0.1, 0.08], want_v=False)
    x = np.convolve(x, contact_pulse(5e-3))[:n]
    t = np.arange(n) / SR
    thud = np.sin(2 * np.pi * 150 * t) * np.exp(-t / 0.015)
    y = 0.08 * x / (np.abs(x).max() + 1e-12) + 0.02 * thud
    y = biquad(y, "hp", 35.0, 0.7)
    return trim_tail(pre_roll(y), -60.0, min_s=0.12)


NOISE_ZONES = {"low": (21, 50), "mid": (51, 79), "high": (80, 108)}


def render_noises(rng):
    out = []
    for zone, sc in (("low", 0.85), ("mid", 1.0), ("high", 1.2)):
        for i in (1, 2):
            km = [(95 * sc, 1.0, 0.08), (210 * sc, 0.8, 0.06), (470 * sc, 0.5, 0.04), (1050 * sc, 0.3, 0.02),
                  (2400 * sc, 0.12, 0.01)]
            out.append((f"keydown_{zone}_{i}.wav", "on", zone,
                        knock(rng, 0.4, km, 1.5e-3, burst_db=-12, burst_band=(1500, 7000), burst_ms=3.0),
                        "grand action: key bottoms, hammer shank / let-off (acoustic)"))
            um = [(130 * sc, 0.8, 0.06), (300 * sc, 0.6, 0.04), (700 * sc, 0.3, 0.02)]
            out.append((f"keyup_{zone}_{i}.wav", "off", zone,
                        knock(rng, 0.35, um, 3.0e-3, burst_db=-16, burst_band=(500, 3000), burst_ms=6.0),
                        "key returns + damper felt lands (acoustic)"))
    return out
