#!/usr/bin/env python3
"""engine_calibrate.py — the loudness calibration, closed THROUGH THE RUNTIME (tp106).

torgc.py calibrates every instrument with its own offline renderer (analyse.Renderer): the centre key at velocity 100
with the Velocity knob's default lands at CALIB_LUFS (−24 LUFS, K-weighted, the first second from the onset). That
renderer is not the runtime: its velocity-layer crossfade, pan law and round-robin pick differ from OrganicEngine's,
and the whole-library audit (Tests/organics_audit.sh lib) measured instruments −3.3 … +3.5 dB off target through the
engine. This pass measures the SAME point through the real engine (Tests/organics_audit.sh calib — OrganicEngine +
OrganicsLibrary, the plugin's own code) and moves every region of the instrument by one offset (noise regions too:
their level is authored relative to the note), keeping the velocity-127 peak of that key at or under PEAK_CEIL_DB.

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
CALIB_LUFS = -24.0
PEAK_CEIL_DB = -1.0


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
        off = CALIB_LUFS - m["lufs"]
        limited = 0.0
        if m["peak127"] + off > PEAK_CEIL_DB:
            limited = m["peak127"] + off - PEAK_CEIL_DB
            off -= limited
        print(f"  {iid:40s} engine {m['lufs']:7.2f} LUFS (key {m['key']}) peak127 {m['peak127']:6.2f} dBFS → "
              f"offset {off:+6.2f} dB{'  (peak-limited %.2f dB)' % limited if limited > 0.005 else ''}")
        if a.dry_run or abs(off) < 0.05:
            continue
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
                           "key": m["key"]}
            L["peakLimitedDb"] = round(limited, 2)
            R["calibrationDb"] = round(R.get("calibrationDb", 0.0) + off, 2)
            with open(rp, "w") as f:
                json.dump(R, f, indent=1)
    return rc


if __name__ == "__main__":
    sys.exit(main())
