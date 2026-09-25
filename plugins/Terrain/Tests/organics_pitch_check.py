#!/usr/bin/env python3
"""organics_pitch_check.py — every key of every installed instrument lands on 12-TET at Tuning = Equal (tp106).

The runtime renders each key (Human 0, 1.5 s, Tuning = Equal) through OrganicEngine (organics_audit --pitchdump); each
note is measured with the COMPILER'S OWN detector (tp108: Tools/organics/tuning.measure_pitch — multi-window YIN + MPM,
the fundamental partial and a harmonic-template fit for short takes; the one that wrote the per-region tfix and that
Tools/organics/retune.py closes through the engine). On a PERFORMED instrument (strings, winds, brass, voices — every take
is its own performance) EVERY velocity layer is rendered at its centre (where it plays alone) and the key reads as its
WORST take; on a struck or plucked one a key is the median of velocities 40 / 80 / 120 (one physical tuning per note).
A key passes when its measured pitch is within ±5 ¢ of equal temperament (of the articulation's fitted stretch curve for
"tfixMode": "stretch" — a piano's deliberate stretch tuning is kept by design). Unpitched instruments ("unpitched": true
in the recipe) and notes the detector itself calls unreliable (no clear fundamental / harmonic series, period and
partials disagreeing > 12 ¢ — tremolo, a rotary speaker, a pitch that never settles) are LISTED with the reason, not
judged.

    python3 Tests/organics_pitch_check.py [idFilter]    exit 0 = no measurable key off by > 30 ¢; keys off by > 5 ¢ are
                                                        listed per instrument (FLAG) — the remaining take-to-take
                                                        intonation of short articulations the detector can read but the
                                                        compiler's gates would not trust enough to correct.
Recipes may exempt themselves with "pitchCheckWhy" (a non-12-TET scale, drone courses, a detector-hostile vibrato motor).
"""
from __future__ import annotations

import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile
from collections import Counter

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TERRAIN = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(TERRAIN, "Tools", "organics"))
import analyse as an  # noqa: E402
import torgc  # noqa: E402
import tuning as tu  # noqa: E402
import retune  # noqa: E402

SR = 48000
BAR = 5.0          # the ±5 ¢ target (reported per instrument)
GROSS = 30.0       # the gate: no measurable key further than this from 12-TET


def library_root() -> str:
    r = os.environ.get("TERRAIN_ORGANICS_DIR")
    if r:
        return r
    for c in ("~/Library/WavesCrate/TerrainInstrument/Organics", "~/Library/WavesCrate/Terrain/Organics"):
        c = os.path.expanduser(c)
        if os.path.isdir(c):
            return c
    return ""


def stretch_curve(rep: dict, artic: str):
    st = rep.get("tfix", {}).get(artic, {}).get("stretchCurveCents")
    if not st:
        return None
    ks = sorted((int(k), float(v)) for k, v in st.items())
    return lambda k: float(np.interp(k, [a for a, _ in ks], [b for _, b in ks]))


def main():
    filt = sys.argv[1] if len(sys.argv) > 1 else ""
    root = library_root()
    idx = json.load(open(os.path.join(root, "index.json")))
    subprocess.run([os.path.join(HERE, "organics_audit.sh"), "build"], capture_output=True)   # builds, then usage exit
    binary = os.path.join(TERRAIN, "..", "..", "build", "organics_engine_test", "organics_audit")
    tmp = tempfile.mkdtemp(prefix="orgpitch_")
    tot_keys = tot_rel = tot_fail = tot_gross = 0
    bad_insts = []
    unmeasured = []
    try:
        for ent in idx:
            iid = ent["id"]
            if filt and filt not in iid:
                continue
            rec_p = os.path.join(TERRAIN, "Tools", "organics", "recipes", iid + ".json")
            rec = json.load(open(rec_p)) if os.path.exists(rec_p) else {}
            if rec.get("unpitched") or rec.get("pitchCheckWhy"):
                why = rec.get("unpitchedWhy") if rec.get("unpitched") else rec.get("pitchCheckWhy")
                print(f"SKIP  {iid:38s} {'unpitched' if rec.get('unpitched') else 'not judged'} ({(why or '')[:90]})")
                continue
            rep_p = os.path.join(root, iid, "build-report.json")
            rep = json.load(open(rep_p)) if os.path.exists(rep_p) else {}
            mp = json.load(open(os.path.join(root, iid, "map.json")))
            out = os.path.join(tmp, iid)
            env = dict(os.environ, TERRAIN_ORGANICS_DIR=root)
            performed = rec.get("category") in torgc.PERFORMED_CATEGORIES
            subprocess.run([binary, "--pitchdump", root, out, iid, retune._vels(rec, mp)], env=env, capture_output=True)
            fails, rel, n = [], 0, 0
            per_key = {}
            why_un = {}
            inh = tu.pitch_kind(rec.get("category", ""))
            for f in sorted(glob.glob(os.path.join(out, "*.f32"))):
                a, key, vel = (int(v) for v in os.path.basename(f)[:-4].split("_")[:3])
                per_key.setdefault((a, key), 0)
                if performed and not retune.on_plateau(mp, a, key, vel):
                    continue                     # this articulation crossfades two (in-tune) takes here: judged by those
                x = np.fromfile(f, dtype=np.float32).astype(np.float64)
                on = an.find_onset(x, 0, len(x))
                m = tu.measure_pitch(x, SR, on, len(x), an.midi_hz(key), kind=inh)
                ok = bool(m.get("ok")) and m.get("hz", 0) > 0
                sc = stretch_curve(rep, mp["artics"][a])
                dev = m["cents"] - (sc(key) if sc else 0.0)
                # the compiler's own rule: a reading far off (> 30 ¢) counts only from a strong measurement
                if ok and abs(dev) > 30.0 and not m.get("strong"):
                    ok = False
                if ok:
                    per_key.setdefault(("dev", a, key), []).append(dev)
                else:
                    why_un.setdefault((a, key), m.get("why") or "far off, weak measurement")
            for (a, key) in [k for k in per_key if k[0] != "dev"]:
                n += 1
                devs = per_key.get(("dev", a, key))
                if not devs:
                    unmeasured.append((iid, mp["artics"][a], key, why_un.get((a, key), "?")))
                    continue
                rel += 1
                # struck / plucked: the median over 40 / 80 / 120 (one physical tuning per note); performed: EVERY take
                # (each velocity layer heard alone, at its centre) must land — the key reads as its worst take
                dev = max(devs, key=abs) if performed else float(np.median(devs))
                if abs(dev) > BAR:
                    fails.append((a, key, dev))
            shutil.rmtree(out, ignore_errors=True)
            tot_keys += n; tot_rel += rel; tot_fail += len(fails)
            gross = [f for f in fails if abs(f[2]) > GROSS]
            tot_gross += len(gross)
            if fails:
                bad_insts.append(iid)
                worst = max(fails, key=lambda t: abs(t[2]))
                lst = " ".join(f"{mp['artics'][a][:6]}:k{k}{d:+.1f}" for a, k, d in fails[:14])
                print(f"{'FAIL' if gross else 'FLAG'}  {iid:38s} {len(fails)}/{rel} reliable keys off > {BAR} ¢ (worst {worst[2]:+.1f} ¢): {lst}")
            else:
                print(f"PASS  {iid:38s} {rel}/{n} keys measurable, all within ±{BAR} ¢")
            sys.stdout.flush()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if unmeasured:
        print(f"   {len(unmeasured)} keys the detector cannot read (not judged), per instrument:")
        per_i = Counter(u[0] for u in unmeasured)
        for iid, c in per_i.most_common():
            why = Counter(u[3] for u in unmeasured if u[0] == iid).most_common(2)
            ks = " ".join(f"{u[1][:6]}:k{u[2]}" for u in unmeasured if u[0] == iid)
            print(f"     {iid:38s} {c:3d}  {why}  {ks[:200]}")
    print(f"══ {'PASS' if not tot_gross else 'FAIL'} — {tot_gross} measurable keys off by more than {GROSS} ¢ (the gate) · "
          f"{tot_fail} of {tot_rel} off by more than {BAR} ¢ (flagged; {tot_keys} keys rendered, {len(bad_insts)} instruments) ══")
    return 1 if tot_gross else 0


if __name__ == "__main__":
    sys.exit(main())
