#!/usr/bin/env python3
"""summary.py — one line per compiled instrument: size, layers/RR, key range, noise, tfix, loudness, repairs, flags.

    python3 summary.py [--out DIR] [ids …]
"""
import argparse
import glob
import json
import os

DEFAULT_OUT = os.path.expanduser("~/Developer/VST-Plugins/organics-library/compiled")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("ids", nargs="*")
    a = ap.parse_args()
    for p in sorted(glob.glob(os.path.join(a.out, "*", "build-report.json"))):
        r = json.load(open(p))
        if a.ids and r["id"] not in a.ids:
            continue
        tf = r.get("tfix", {})
        tfs = " ".join(f"{k[:10]}:{v.get('worst', 0):+.1f}@{v.get('worstRoot', '-')}/{v.get('unreliableRegions', 0)}u"
                       for k, v in tf.items() if isinstance(v, dict))
        if tf.get("rootFixes"):
            tfs += f" rootFix×{len(tf['rootFixes'])}"
        L = r.get("loudness", {})
        print(f"{r['id']:36s} {r.get('sizeMB', 0):6.1f}MB L{r.get('maxLayers')}/RR{r.get('maxRR')} keys{r.get('keyRange')} "
              f"att{r.get('attackRegions')} rel{r.get('releaseRegions')} nz{r.get('noiseRegions')} | "
              f"loud {L.get('before', 0):+.1f}→{L.get('verify', 0):+.1f} pk{L.get('peak127Db', 0):+.1f} lim{L.get('peakLimitedDb', 0)} | "
              f"tfix {tfs} | rr{r.get('rrRepair')} silent{r.get('silentRegionsDropped')}/{r.get('silentRegionsKept')} | "
              f"{'; '.join(r.get('flags', []))} | drop {r.get('dropped')}")


if __name__ == "__main__":
    main()
