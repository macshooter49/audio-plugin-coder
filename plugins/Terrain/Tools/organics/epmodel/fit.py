#!/usr/bin/env python3
"""fit.py — fit a model's free pickup/amplitude parameters to MEASURED numbers of a reference recording set.

    python3 fit.py wurli [--iters 300]

Only scalar measurements of the reference files are used (H2/H1, H3/H1 in dB and the attack spectral centroid
0–30 ms / f0, from measure.summary). The reference audio itself never enters the renderer, the fitted model or the
shipped samples. Dynamics marks map to MIDI velocity as pp 28 · p 36 · mp 60 · f 92 · ff 120.
"""
from __future__ import annotations

import argparse
import copy
import glob
import importlib
import json
import math
import os
import re
import sys

import numpy as np
import soundfile as sf
from scipy.optimize import minimize

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import common as C          # noqa: E402
import measure as M         # noqa: E402

REF = os.path.expanduser("~/Developer/VST-Plugins/organics-library/raw/sfzinstruments/GregSullivan.E-Pianos")
DYN = {"pp": 28, "p": 36, "mp": 60, "f": 92, "ff": 120}
NAMES = {'c': 0, 'db': 1, 'd': 2, 'eb': 3, 'e': 4, 'f': 5, 'gb': 6, 'g': 7, 'ab': 8, 'a': 9, 'bb': 10, 'b': 11}


def ref_numbers(which: str):
    """[(midi, vel, {H2,H3,cA,…})] measured from the DO-NOT-SHIP reference set (numbers only)."""
    out = []
    if which == "wurli":
        for f in sorted(glob.glob(os.path.join(REF, "Wurlitzer EP200", "Samples", "*.flac"))):
            b = os.path.basename(f)[:-5]
            m = re.match(r"([a-g]b?)(\d)(pp|mp|f|ff)$", b)
            midi = 12 * (int(m.group(2)) + 1) + NAMES[m.group(1)]
            x, sr = sf.read(f)
            out.append((midi, DYN[m.group(3)], M.summary(x, sr, midi)))
    elif which == "pianet":
        for f in sorted(glob.glob(os.path.join(REF, "Pianet T", "Samples", "*.flac"))):
            b = os.path.basename(f)[:-5]
            if "release" in b:
                continue
            midi, _, d = b.split("_")
            x, sr = sf.read(f)
            out.append((int(midi), DYN[d.lower()], M.summary(x, sr, int(midi))))
    elif which == "cp":
        for f in sorted(glob.glob(os.path.join(REF, "CP80", "Samples", "*.flac"))):
            b = os.path.basename(f)[:-5]
            midi, _, d = b.split("-")
            x, sr = sf.read(f)
            out.append((int(midi), DYN[d.lower()], M.summary(x, sr, int(midi))))
    return out


def err_of(mod, P, refs, dur=0.9):
    e = 0.0
    for midi, vel, r in refs:
        y = mod.render(midi, vel, p=P, dur=dur)
        s = M.summary(y, C.SR, midi)
        for k in ("H2", "H3"):
            a, b = max(s[k], -60.0), max(r[k], -60.0)
            e += (a - b) ** 2
        e += 100.0 * (math.log(max(s["cA"], 0.1) / max(r["cA"], 0.1))) ** 2
        e += 100.0 * (math.log(max(s["cS"], 0.1) / max(r["cS"], 0.1))) ** 2
    return e / max(1, len(refs))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--iters", type=int, default=250)
    ap.add_argument("--every", type=int, default=1, help="use every Nth reference measurement (speed)")
    a = ap.parse_args()
    mod = importlib.import_module(a.model)
    refs = ref_numbers(a.model)[::a.every]
    print(f"{len(refs)} reference measurements")
    P0 = copy.deepcopy(mod.P)
    spec = mod.FIT                              # [(path, lo, hi, log?)]

    def get(P, path):
        o = P
        for p in path[:-1]:
            o = o[p]
        return o[path[-1]]

    def put(P, path, val):
        o = P
        for p in path[:-1]:
            o = o[p]
        o[path[-1]] = val

    def unpack(z):
        P = copy.deepcopy(P0)
        for (path, lo, hi, lg), zi in zip(spec, z):
            u = 1 / (1 + math.exp(-zi))
            val = math.exp(math.log(lo) + u * (math.log(hi) - math.log(lo))) if lg else lo + u * (hi - lo)
            put(P, path, val)
        return P

    def pack(P):
        z = []
        for path, lo, hi, lg in spec:
            v = get(P, path)
            u = (math.log(v) - math.log(lo)) / (math.log(hi) - math.log(lo)) if lg else (v - lo) / (hi - lo)
            u = min(0.98, max(0.02, u))
            z.append(math.log(u / (1 - u)))
        return np.array(z)

    f = lambda z: err_of(mod, unpack(z), refs)   # noqa: E731
    z0 = pack(P0)
    print("start err", round(f(z0), 2))
    res = minimize(f, z0, method="Nelder-Mead", options={"maxiter": a.iters, "xatol": 1e-3, "fatol": 1e-2})
    Pb = unpack(res.x)
    print("best err", round(res.fun, 2))
    print(json.dumps(Pb, indent=1))


if __name__ == "__main__":
    main()
