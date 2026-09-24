"""clav.py — tangent-struck electric clavichord (Clavinet-D6-style), 60-key F1–E6 (MIDI 29–88).

Physics (per key):
  string    stiff string, modes f_n = n·f0·√(1+B·n²) (B 2e-5 bass … 6e-4 treble), up to 16 kHz (≤ 150 modes); loss grows
            with frequency: T60_n = T60_1 / (1 + (f_n/fd)^1.5) → the bright attack that mellows as it rings.
  tangent   a rubber-tipped tangent strikes the string and STAYS on it, becoming the new termination; the impact is
            localised within ~1–2 % of the active length next to it, so the initial string velocity puts modal velocity
            ∝ sin(nπp) (p ≈ 0.012–0.02): a nearly flat, very bright spectrum up to n ≈ 1/(2p). Contact 1.4 ms (pp) …
            0.35 ms (ff); a hard strike also raises the tension → a +2…7 cent pitch glide that settles in ~0.15 s.
  pickups   two single-coil magnetic pickups under the string at fractions q1, q2 of the active length from the bridge:
            modal pickup gain = sin(nπq1) + sin(nπq2) (both in phase, the classic funky setting) → the comb-filtered,
            nasal quack; output ∝ string velocity with a slight flux nonlinearity (1 + β·x); coil resonance ≈ 4.5 kHz.
  release   key up: the tangent leaves the string, which then vibrates over its FULL length (longer → a lower pitch)
            and the yarn damper wound on the far end kills it within ~0.1–0.3 s: the famous release 'blip',
            rendered as release samples.
No reference recording exists for this model; it is validated against the physics (see validate.py).
"""
from __future__ import annotations

import math

import numpy as np

from common import (SR, midi_f, interp_key, interp_lin, vel01, contact_pulse, modal, biquad, trim_tail, pre_roll,
                    knock)

NAME = "Clav"
TITLE = "Clav (Waves Crate physical model)"
KEY_LO, KEY_HI = 29, 88
ROOTS = list(range(30, 88, 3))                     # 20 zones
LAYERS = [(1, 22, 18), (23, 44, 36), (45, 66, 57), (67, 88, 78), (89, 108, 98), (109, 127, 122)]
REF_LAYER = 4
RELEASE_ROOTS = list(range(30, 88, 3))             # the key-up blip is part of the instrument: one per zone
FMAX = 16000.0


def t60_fund(k):
    return interp_key(k, [(29, 9.0), (48, 6.5), (64, 4.5), (76, 3.0), (88, 1.8)])


def inharm(k):
    return interp_key(k, [(29, 2e-5), (60, 1.2e-4), (88, 6e-4)])


def pickup_pos(k):
    # straight pickup bars under strings of different length: the fractions grow toward the (short) treble strings
    return interp_lin(k, [(29, 0.075), (88, 0.16)]), interp_lin(k, [(29, 0.2), (88, 0.38)])


def string_modes(f0, B, fmax=FMAX):
    n = np.arange(1, 400)
    f = n * f0 * np.sqrt(1 + B * n * n)
    keep = f < fmax
    return n[keep][:150], f[keep][:150]


def amp_of(k, vel):
    return 10 ** (-1.3 * (1 - vel01(vel)))


def voice(k, n_frames, f0, B, p, q1, q2, T1, fd, tc, glide=None, amp=1.0, beta=0.25):
    n, f = string_modes(f0, B)
    vel_amp = np.sin(n * np.pi * p)                        # initial modal velocity from the localised strike
    disp = vel_amp / n                                     # → displacement amplitude ∝ 1/ω
    g = np.sin(n * np.pi * q1) + np.sin(n * np.pi * q2)
    t60 = T1 / (1.0 + (f / fd) ** 1.5)
    x, v = modal(n_frames, f, disp * g, t60, glide=glide)
    pl = contact_pulse(tc, shape=1.5)
    x = np.convolve(x, pl)[:n_frames]
    v = np.convolve(v, pl)[:n_frames]
    scale = 1.0 / (np.abs(x).max() + 1e-12)
    xn = x * scale * amp
    y = v * scale * amp * (1.0 + beta * xn)               # EM pickup: flux slope rises as the string nears the pole
    y = biquad(y, "hp", 40.0, 0.7)
    y = biquad(y, "peak", 4500.0, 2.0, 5.0)                # single-coil coil/cable resonance
    y = biquad(y, "lp", 9000.0, 0.7)
    return y


def render(k, vel, var=0):
    f0 = midi_f(k)
    T1 = t60_fund(k)
    dur = min(8.0, max(2.0, 1.1 * T1))
    nfr = int(dur * SR)
    q1, q2 = pickup_pos(k)
    p = interp_lin(k, [(29, 0.012), (88, 0.022)])
    v01 = vel01(vel)
    tc = interp_lin(k, [(29, 1.6e-3), (88, 0.9e-3)]) * (0.2 + 0.8 * (1 - v01) ** 1.2)
    a = amp_of(k, vel)
    gl = (0.0042 * a * a, 0.15)
    y = voice(k, nfr, f0, inharm(k), p, q1, q2, T1, fd=interp_lin(k, [(29, 900.0), (88, 3000.0)]), tc=tc,
              glide=gl, amp=a)
    return trim_tail(pre_roll(y), -66.0)


def render_release(k):
    """Key up: the whole string (tangent-to-bridge + the yarn-damped segment, ~12–20 % longer) is released from the
    tangent's deflection — a pluck at the tangent point — and the yarn damps it within 0.1–0.3 s."""
    f0 = midi_f(k)
    ratio = interp_lin(k, [(29, 1 / 1.12), (88, 1 / 1.2)])
    fr = f0 * ratio
    n_fr = int(0.5 * SR)
    n, f = string_modes(fr, inharm(k))
    pos = 1.0 - ratio                                         # tangent position on the full string
    disp = np.sin(n * np.pi * pos) / (n * n)                  # pluck (released deflection): ∝ sin(nπp)/n²
    q1, q2 = pickup_pos(k)
    g = np.sin(n * np.pi * q1 * ratio) + np.sin(n * np.pi * q2 * ratio)
    t60 = interp_lin(k, [(29, 0.3), (88, 0.12)]) / (1.0 + (f / 1500.0) ** 1.2)
    x, v = modal(n_fr, f, disp * g, t60, phases=np.full(len(f), math.pi / 2))
    x = np.convolve(x, contact_pulse(1.5e-3))[:n_fr]
    v = np.convolve(v, contact_pulse(1.5e-3))[:n_fr]
    s = 1.0 / (np.abs(x).max() + 1e-12)
    y = v * s * 0.3
    y = biquad(y, "hp", 40.0, 0.7)
    y = biquad(y, "peak", 4500.0, 2.0, 5.0)
    y = biquad(y, "lp", 9000.0, 0.7)
    t = np.arange(len(y)) / SR
    y *= 1 - np.exp(-t / 0.0008)                               # the lift is not instantaneous
    return trim_tail(pre_roll(y), -60.0, min_s=0.12)


NOISE_ZONES = {"low": (29, 48), "mid": (49, 68), "high": (69, 88)}


def render_noises(rng):
    out = []
    for zone, sc in (("low", 0.9), ("mid", 1.0), ("high", 1.12)):
        for i in (1, 2):
            km = [(160 * sc, 1.0, 0.04), (380 * sc, 0.8, 0.03), (820 * sc, 0.55, 0.02), (1900 * sc, 0.35, 0.01),
                  (4100 * sc, 0.2, 0.006)]
            out.append((f"keydown_{zone}_{i}.wav", "on", zone,
                        knock(rng, 0.25, km, 0.7e-3, burst_db=-6, burst_band=(3000, 12000), burst_ms=1.5),
                        "tangent 'tock' on the string/anvil + the key bottoming (acoustic)"))
            um = [(200 * sc, 0.8, 0.03), (480 * sc, 0.5, 0.02), (1100 * sc, 0.3, 0.012)]
            out.append((f"keyup_{zone}_{i}.wav", "off", zone,
                        knock(rng, 0.2, um, 1.8e-3, burst_db=-12, burst_band=(800, 4000), burst_ms=4.0),
                        "key return click (acoustic)"))
    return out
