#!/usr/bin/env python3
"""engine_calibrate.py — the loudness calibration, closed THROUGH THE RUNTIME (tp106).

torgc.py calibrates every instrument with its own offline renderer (analyse.Renderer): the centre key at velocity 100
with the Velocity knob's default lands at CALIB_LUFS (−24 LUFS, K-weighted, the first second from the onset). That
renderer is not the runtime: its velocity-layer crossfade, pan law and round-robin pick differ from OrganicEngine's,
and the whole-library audit (Tests/organics_audit.sh lib) measured instruments −3.3 … +3.5 dB off target through the
engine. This pass measures the SAME point through the real engine (Tests/organics_audit.sh calib — OrganicEngine +
OrganicsLibrary, the plugin's own code) and moves every region of the instrument by one offset (noise regions too:
their level is authored relative to the note).

tp113 (Max: "normalize them and turn them up … each one damn near at the same level … I don't care about background noise"):
the engine carries a flat output makeup (organics::kOutputMakeupDb, OrganicsApi.h), so the target THROUGH THE ENGINE is
LIB_CALIB_LUFS + MAKEUP_DB (the library itself stays in library units: −24 LUFS at unity). The calibration is NO LONGER
PEAK-LIMITED: an instrument the old rule held under the target (its centre key's v127 peak would have passed −1 dBFS; tp108:
mbira −3.64, Clean Electric −1.82, kalimba −1.42, ganjo −1.34, Hungarian zither −0.95, water glasses −0.56, timpani −0.33)
is lifted to the target, and the lift is recorded as loudness.peakLiftDb — peaktrim.py's per-key bar moves up by it (the
trim keeps its shape; nothing is clipped or limited). The v127 peak of the key is still measured and reported.

    python3 Tools/organics/engine_calibrate.py [--lib DIR] [--root DIR] [ids…]     (default: every instrument)

--lib  the compiled library to edit (default ~/Developer/VST-Plugins/organics-library/compiled)
--root the installed library the engine reads (default: the resolver of Tests/organics_audit.sh), which must point at
       the same folders (install.sh symlinks them). Idempotent: a second run moves nothing (|offset| < 0.05 dB).
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TERRAIN = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import peaktrim              # noqa: E402  (the makeup constant, read from OrganicsApi.h)
LIB_CALIB_LUFS = -24.0       # library units (the engine at unity) — torgc.CALIB_LUFS
MAKEUP_DB = peaktrim.MAKEUP_DB
CALIB_LUFS = LIB_CALIB_LUFS + MAKEUP_DB      # tp113: what the engine plays (tp114b makeup +12: −24 + 12 = −12 LUFS; ≈ −24 LUFS at the plugin output)
PEAK_CEIL_DB = peaktrim.TARGET_DB            # reported only (tp113: never a limit on the calibration any more)


def measure(root: str, ids):
    env = dict(os.environ)
    if root:
        env["TERRAIN_ORGANICS_DIR"] = root
    out = {}
    for i in (ids or [""]):
        p = subprocess.run([os.path.join(TERRAIN, "Tests", "organics_audit.sh"), "calib", i], env=env,
                           capture_output=True, text=True)
        for line in p.stdout.splitlines():
            f = line.split()
            if len(f) == 5 and f[0] == "CALIB":
                out[f[1]] = {"key": int(f[2]), "lufs": float(f[3]), "peak127": float(f[4])}
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ids", nargs="*")
    ap.add_argument("--lib", default=os.path.expanduser("~/Developer/VST-Plugins/organics-library/compiled"))
    ap.add_argument("--root", default="")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    got = measure(a.root, a.ids)
    if not got:
        print("no measurements (build Tests/organics_audit.sh first / check the library root)")
        return 1
    rc = 0
    for iid, m in sorted(got.items()):
        d = os.path.join(a.lib, iid)
        mp, rp = os.path.join(d, "map.json"), os.path.join(d, "build-report.json")
        if not os.path.exists(mp):
            print(f"  skip {iid}: not in {a.lib}")
            continue
        # tp113: the target, never peak-limited. What the old rule held back (loudness.peakLimitedDb: engine_calibrate's
        # own limit + a peak trim on the calibration key itself) is LIFTED now and recorded as peakLiftDb.
        R0 = json.load(open(rp)) if os.path.exists(rp) else {}
        was_limited = float((R0.get("loudness") or {}).get("peakLimitedDb", 0.0))
        off = CALIB_LUFS - m["lufs"]
        over = m["peak127"] + off - PEAK_CEIL_DB
        print(f"  {iid:40s} engine {m['lufs']:7.2f} LUFS (key {m['key']}) peak127 {m['peak127']:6.2f} dBFS → "
              f"offset {off:+6.2f} dB{'  (lifts a %.2f dB peak limit)' % was_limited if was_limited > 0.005 else ''}"
              f"{'  (v127 peak %.2f dB over the old bar)' % over if over > 0.005 else ''}")
        if a.dry_run or (abs(off) < 0.05 and was_limited <= 0.005):
            continue
        if abs(off) >= 0.05:
            with open(mp) as f:
                M = json.load(f)
            for r in M["regions"]:
                r["gainDb"] = round(r["gainDb"] + off, 3)
            with open(mp, "w") as f:
                json.dump(M, f, indent=1)
        if os.path.exists(rp):
            with open(rp) as f:
                R = json.load(f)
            L = R.setdefault("loudness", {})
            L["engine"] = {"measuredLufs": round(m["lufs"], 2), "offsetDb": round(off, 2),
                           "achievedLufs": round(m["lufs"] + off, 2), "peak127Db": round(m["peak127"] + off, 2),
                           "key": m["key"], "targetLufs": round(CALIB_LUFS, 2), "makeupDb": MAKEUP_DB}
            if was_limited > 0.005:   # the whole instrument (every key, every trim) moved up by off: its peak bar with it
                L["peakLiftDb"] = round(float(L.get("peakLiftDb", 0.0)) + max(0.0, off), 2)
            L["peakLimitedDb"] = 0.0
            if abs(off) >= 0.05:
                R["calibrationDb"] = round(R.get("calibrationDb", 0.0) + off, 2)
            with open(rp, "w") as f:
                json.dump(R, f, indent=1)
    return rc


if __name__ == "__main__":
    sys.exit(main())
