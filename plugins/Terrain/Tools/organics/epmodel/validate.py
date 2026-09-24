#!/usr/bin/env python3
"""validate.py — listen-by-numbers validation of the EP models against physics and (numbers-only) reference measurements.

    python3 validate.py [--md VALIDATION.md]

Reference recordings (DO-NOT-SHIP, raw/sfzinstruments/GregSullivan.E-Pianos) are only MEASURED here — a few scalars
per file — and only when present on disk.
"""
from __future__ import annotations

import argparse
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import common as C          # noqa: E402
import measure as M         # noqa: E402
import fit                  # noqa: E402

LINES = []


def out(s=""):
    print(s)
    LINES.append(s)


def partial_db(y, f, t0, t1, fref):
    S, fr = M.spectrum(y, C.SR, t0, t1)
    return M.peak_db(S, fr, f) - M.peak_db(S, fr, fref)


def f_peak(y, t0, t1, f_guess, tol=0.06):
    o = M.onset(y)
    seg = y[o + int(t0 * C.SR): o + int(t1 * C.SR)]
    N = 1 << 20
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), N))
    fr = np.fft.rfftfreq(N, 1 / C.SR)
    m = (fr > f_guess * (1 - tol)) & (fr < f_guess * (1 + tol))
    i = np.nonzero(m)[0][np.argmax(S[m])]
    a, b, c = np.log(S[i - 1:i + 2] + 1e-20)
    d = 0.5 * (a - c) / (a - 2 * b + c)
    return fr[i] + d * (fr[1] - fr[0])


def cents(a, b):
    return 1200 * math.log2(a / b)


def rhodes():
    import rhodes as R
    out("## Tine EP (Rhodes-style) — physics checks (no cleared reference recording exists)")
    out("")
    out("| key | r2 (bell/f0) | bell dB re H1, 0–50 ms (v98) | bell dB, 1.0–1.5 s | H2/H1 dB pp (v18) → ff (v122) | T60 s (fund) |")
    out("|---|---|---|---|---|---|")
    for k in (35, 47, 59, 71, 83, 95):
        f0 = C.midi_f(k)
        r2 = C.interp_lin(k, [(28, 7.6), (60, 6.8), (100, 6.2)])
        y = R.render(k, 98)
        b0 = partial_db(y, r2 * f0, 0.0, 0.05, f0)
        b1 = partial_db(y, r2 * f0, 1.0, 1.5, f0) if len(y) > 1.6 * C.SR else float("nan")
        hp = M.summary(R.render(k, 18), C.SR, k)["H2"]
        hf = M.summary(R.render(k, 122), C.SR, k)
        out(f"| {k} | {r2:.2f} | {b0:+.1f} | {b1:+.1f} | {hp:+.1f} → {hf['H2']:+.1f} | {hf['T60']:.1f} |")
    out("")
    out("Expected physics: cantilever 2nd-mode ratio 6.27 (unloaded) raised by the tuning-spring mass; the bell is loud "
        "only in the attack and decays in ≈1 s; H2 grows with velocity (the tine swings across the pole axis = bark); "
        "T60 falls from ~16 s (E1) to ~1.3 s (E7).")
    out("")


def compare(name, mod, title):
    refs = fit.ref_numbers(name)
    if not refs:
        out(f"(reference set for {name} not on disk — skipped)")
        return
    out(f"## {title} — model vs reference measurements (same note, same dynamic)")
    out("")
    out("| key | vel | H2/H1 ref | H2/H1 model | H3/H1 ref | H3/H1 model | attack centroid/f0 ref | model | sustain centroid/f0 ref | model |")
    out("|---|---|---|---|---|---|---|---|---|---|")
    d2, d3, dc = [], [], []
    for midi, vel, r in sorted(refs):
        s = M.summary(mod.render(midi, vel, dur=0.9), C.SR, midi)
        out(f"| {midi} | {vel} | {r['H2']:+.1f} | {s['H2']:+.1f} | {r['H3']:+.1f} | {s['H3']:+.1f} | {r['cA']:.2f} | "
            f"{s['cA']:.2f} | {r['cS']:.2f} | {s['cS']:.2f} |")
        if r["H2"] > -60:
            d2.append(abs(r["H2"] - s["H2"]))
        if r["H3"] > -60:
            d3.append(abs(r["H3"] - s["H3"]))
        dc.append(abs(math.log2(max(s["cA"], 0.1) / max(r["cA"], 0.1))))
    out("")
    out(f"Median |error|: H2 {np.median(d2):.1f} dB, H3 {np.median(d3):.1f} dB, attack centroid "
        f"{100 * (2 ** np.median(dc) - 1):.0f} % ({len(refs)} reference notes).")
    out("")


def wurli_bark():
    import wurli as W
    out("Reed EP bark (model): H2/H1 and H3/H1 at pp (v18) vs ff (v122)")
    out("")
    out("| key | H2 pp | H2 ff | H3 pp | H3 ff |")
    out("|---|---|---|---|---|")
    for k in (40, 52, 61, 73, 85):
        p = M.summary(W.render(k, 18), C.SR, k)
        f = M.summary(W.render(k, 122), C.SR, k)
        out(f"| {k} | {p['H2']:+.1f} | {f['H2']:+.1f} | {p['H3']:+.1f} | {f['H3']:+.1f} |")
    out("")


def clav():
    import clav as K
    out("## Clav — physics checks (no cleared reference recording exists)")
    out("")
    out("| key | attack centroid/f0 pp → ff | pitch glide at ff, 10–60 ms vs 0.6–1.0 s (cents) | release pitch / f0 (expected) | T60 s |")
    out("|---|---|---|---|---|")
    for k in (33, 45, 57, 69, 81):
        f0 = C.midi_f(k)
        pp = M.summary(K.render(k, 18), C.SR, k)
        yf = K.render(k, 122)
        ff = M.summary(yf, C.SR, k)
        g = cents(f_peak(yf, 0.01, 0.06 if f0 > 150 else 0.12, f0), f_peak(yf, 0.6, 1.0, f0))
        yr = K.render_release(k)
        ratio = C.interp_lin(k, [(29, 1 / 1.12), (88, 1 / 1.2)])
        fr = f_peak(yr, 0.0, 0.12, f0 * ratio, 0.04)
        out(f"| {k} | {pp['cA']:.1f} → {ff['cA']:.1f} | {g:+.1f} | {fr / f0:.3f} ({ratio:.3f}) | {ff['T60']:.1f} |")
    # pickup comb: measured harmonic profile vs the predicted pickup weighting
    k = 60
    f0 = C.midi_f(k)
    y = K.render(k, 98)
    S, fr = M.spectrum(y, C.SR, 0.3, 0.8)
    n = np.arange(1, 25)
    B = K.inharm(k)
    meas = np.array([M.peak_db(S, fr, i * f0 * math.sqrt(1 + B * i * i), 0.01) for i in n])
    q1, q2 = K.pickup_pos(k)
    g = 20 * np.log10(np.abs(np.sin(n * np.pi * q1) + np.sin(n * np.pi * q2)) + 1e-6)
    r = np.corrcoef(meas - meas.max(), g)[0, 1]
    out("")
    out(f"Pickup comb (C4, harmonics 1–24): correlation between the measured harmonic levels and the two-pickup "
        f"weighting |sin(nπq1)+sin(nπq2)| = {r:.2f} (the comb notches are audible in the output).")
    out("")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--md")
    ap.add_argument("--skip-cp", action="store_true")
    a = ap.parse_args()
    import wurli
    import pianet
    out("# EP models — validation by numbers")
    out("")
    out("Generated by `validate.py`. Measurements: harmonic levels in a 0.15–0.45 s window, spectral centroid 0–30 ms "
        "(attack) and 0.3–0.8 s (sustain) over f0, T60 from an envelope line fit. Reference recordings (DO-NOT-SHIP) "
        "were only measured; none of their audio is used by the renderer. Dynamics marks → velocity: pp 28, p 36, "
        "mp 60, f 92, ff 120.")
    out("")
    rhodes()
    compare("wurli", wurli, "Reed EP (Wurlitzer-style)")
    wurli_bark()
    clav()
    compare("pianet", pianet, "Pad Reed EP (Pianet-style)")
    if not a.skip_cp:
        import cp
        compare("cp", cp, "Electric Grand (CP-style)")
    if a.md:
        with open(a.md, "w") as f:
            f.write("\n".join(LINES) + "\n")


if __name__ == "__main__":
    main()
