#!/usr/bin/env python3
"""organics_import_compare.py — tp108: the in-plugin import's map.json against the offline compiler's, field by field.

    python3 Tests/organics_import_compare.py <dir>     (after organics_import_fixtures.py + organics_import_test)

Pairs: <dir>/library/User/user.fixture ↔ <dir>/ref/ref.sfz · <dir>/library/User/user.t ↔ <dir>/ref/ref.sf2.
Regions are paired in the compiler's own order (a, kind, lk, lv, rr, rand). Exact: a kind lk hk lv hv xfLo xfHi root
cents pan start loop ls le rr rand grp offBy offMode env rtDecay trig; per sample: channels, rate. Tolerances:
end (frames) ±512 (one 10 ms hop of the −60 dB trim) · onset ±30 frames · gainNorm ±0.02 · velCurve ±0.01 per point ·
tfix ±1 ¢ · gainDb RELATIVE to the first region ±0.3 dB and absolute ±1.5 dB (the calibration renders with a linear
resampler here, the compiler's with an FFT one). By design (OrganicsImport.h): no tail-loop search → a DECAYING region's
tailLs/tailLe/xf are 0 in the import (compared only on looping regions); the name/id/family/category/credit are the user's.
"""
import json
import os
import sys

import soundfile as sf

FAILS, PASSES = [], [0]


def check(c, m):
    if c:
        PASSES[0] += 1
    else:
        FAILS.append(m)


def key(r):
    return (r["a"], ["attack", "release", "noise"].index(r["kind"]), r["lk"], r["lv"], r["rr"], r["rand"])


def compare(tag, mine_dir, ref_dir):
    m = json.load(open(os.path.join(mine_dir, "map.json")))
    r = json.load(open(os.path.join(ref_dir, "map.json")))
    n0 = len(FAILS)
    check(len(m["artics"]) == len(r["artics"]), f"{tag}: artics {m['artics']} vs {r['artics']}")
    if tag == "sfz":
        check(m["artics"] == r["artics"], f"{tag}: artic names {m['artics']} vs {r['artics']}")
    for k in ("torg", "polyMax", "hasNoise", "hasRelease"):
        check(m[k] == r[k], f"{tag}: {k} {m[k]} vs {r[k]}")
    check(len(m["samples"]) == len(r["samples"]), f"{tag}: {len(m['samples'])} samples vs {len(r['samples'])}")
    for i, (a, b) in enumerate(zip(m["samples"], r["samples"])):
        ia, ib = sf.info(os.path.join(mine_dir, "samples", a)), sf.info(os.path.join(ref_dir, "samples", b))
        check(ia.channels == ib.channels and ia.samplerate == ib.samplerate and ia.subtype == "PCM_16",
              f"{tag}: sample {i} {ia.channels}ch {ia.samplerate} {ia.subtype} vs {ib.channels}ch {ib.samplerate}")
        check(abs(ia.frames - ib.frames) <= 512, f"{tag}: sample {i} frames {ia.frames} vs {ib.frames}")
    mr, rr = sorted(m["regions"], key=key), sorted(r["regions"], key=key)
    check(len(mr) == len(rr), f"{tag}: {len(mr)} regions vs {len(rr)}")
    exact = ("a", "kind", "lk", "hk", "lv", "hv", "xfLo", "xfHi", "root", "cents", "pan", "start", "loop", "ls", "le",
             "rr", "rand", "grp", "offBy", "offMode", "env", "rtDecay", "trig")
    g0m, g0r = (mr[0]["gainDb"], rr[0]["gainDb"]) if mr and rr else (0, 0)
    worst = {"end": 0, "onset": 0, "gainNorm": 0.0, "velCurve": 0.0, "tfix": 0.0, "gainDbRel": 0.0, "gainDbAbs": 0.0}
    for i, (x, y) in enumerate(zip(mr, rr)):
        for k in exact:
            check(x.get(k) == y.get(k), f"{tag}: region {i} {k} {x.get(k)} vs {y.get(k)}")
        if x["loop"] in ("continuous", "sustain"):
            check((x["tailLs"], x["tailLe"], x["xf"]) == (y["tailLs"], y["tailLe"], y["xf"]),
                  f"{tag}: region {i} looping tail/xf {x['tailLs']}-{x['tailLe']}/{x['xf']} vs {y['tailLs']}-{y['tailLe']}/{y['xf']}")
        else:   # no tail-loop search in the import: a decaying region has no tail loop and no crossfade
            check(x["tailLs"] == 0 and x["tailLe"] == 0 and x["xf"] == 0, f"{tag}: region {i} a decaying region carries a tail loop {x['tailLs']}-{x['tailLe']}/{x['xf']}")
        d = {"end": abs(x["end"] - y["end"]), "onset": abs(x["onset"] - y["onset"]), "gainNorm": abs(x["gainNorm"] - y["gainNorm"]),
             "velCurve": max(abs(p[1] - q[1]) for p, q in zip(x["velCurve"], y["velCurve"])) if len(x["velCurve"]) == len(y["velCurve"]) else 99,
             "tfix": abs(x.get("tfix", 0) - y.get("tfix", 0)),
             "gainDbRel": abs((x["gainDb"] - g0m) - (y["gainDb"] - g0r)), "gainDbAbs": abs(x["gainDb"] - y["gainDb"])}
        for k, v in d.items():
            worst[k] = max(worst[k], v)
        check(d["end"] <= 512, f"{tag}: region {i} end {x['end']} vs {y['end']}")
        check(d["onset"] <= 30, f"{tag}: region {i} onset {x['onset']} vs {y['onset']}")
        check(d["gainNorm"] <= 0.02, f"{tag}: region {i} gainNorm {x['gainNorm']} vs {y['gainNorm']}")
        check(d["velCurve"] <= 0.01, f"{tag}: region {i} velCurve {x['velCurve']} vs {y['velCurve']}")
        check(d["tfix"] <= 1.0, f"{tag}: region {i} tfix {x.get('tfix')} vs {y.get('tfix')}")
        check(d["gainDbRel"] <= 0.3, f"{tag}: region {i} relative gainDb {x['gainDb'] - g0m:.3f} vs {y['gainDb'] - g0r:.3f}")
        check(d["gainDbAbs"] <= 1.5, f"{tag}: region {i} gainDb {x['gainDb']} vs {y['gainDb']}")
    ok = len(FAILS) == n0
    print(f"  {'PASS' if ok else 'FAIL'}  {tag}: {len(mr)} regions / {len(m['samples'])} samples field by field — worst "
          + ", ".join(f"{k} {v:.3g}" for k, v in worst.items()))
    return ok


def main():
    d = sys.argv[1]
    user = os.path.join(d, "library", "User")
    a = compare("sfz", os.path.join(user, "user.fixture"), os.path.join(d, "ref", "ref.sfz"))
    b = compare("sf2", os.path.join(user, "user.t"), os.path.join(d, "ref", "ref.sf2"))
    for f in FAILS[:40]:
        print("        " + f)
    print(f"  {PASSES[0]} field checks passed, {len(FAILS)} failed")
    return 0 if (a and b and not FAILS) else 1


if __name__ == "__main__":
    sys.exit(main())
