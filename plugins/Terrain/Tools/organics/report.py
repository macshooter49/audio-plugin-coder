#!/usr/bin/env python3
"""report.py — one table over every compiled instrument's build-report.json (budget + listen-by-numbers QA).

    python3 report.py [--out DIR] [--md FILE] [--brief]
"""
import argparse
import glob
import json
import os

DEFAULT_OUT = os.path.expanduser("~/Developer/VST-Plugins/organics-library/compiled")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--md")
    ap.add_argument("--brief", action="store_true")
    a = ap.parse_args()
    idx = {e["id"]: e for e in json.load(open(os.path.join(a.out, "index.json")))}
    rows = []
    tot_ram = tot_disk = 0.0
    for p in sorted(glob.glob(os.path.join(a.out, "*", "build-report.json"))):
        r = json.load(open(p))
        e = idx.get(r["id"], {})
        qa = r.get("qa", {})
        seam = r.get("seam", {})
        tot_ram += r.get("sizeMB", 0)
        disk = sum(os.path.getsize(f) for f in glob.glob(os.path.join(os.path.dirname(p), "**", "*"), recursive=True)
                   if os.path.isfile(f)) / 1048576.0
        tot_disk += disk
        rows.append([r["id"], e.get("category", ""), e.get("family", ""), f'{r.get("sizeMB", 0):.1f}', f"{disk:.1f}",
                     f'{r.get("maxLayers")}/{r.get("maxRR")}', str(len(r.get("artics", []))), e.get("licence", ""),
                     f'{qa.get("v40", {}).get("peakDb", "")}/{qa.get("v80", {}).get("peakDb", "")}/{qa.get("v120", {}).get("peakDb", "")}',
                     f'{qa.get("step40to80Db", "")}/{qa.get("step80to120Db", "")}', str(qa.get("maxVelJumpDb", "")),
                     str(qa.get("noiseFloorRelDb", "")), str(r.get("sourceClippedSamples", "")),
                     f'{seam.get("n", 0)}:{seam.get("worst", "-")}',
                     "; ".join(r.get("flags", []))])
    hdr = ["id", "category", "family", "RAM MB", "disk MB", "layers/RR", "artics", "licence",
           "peak dB v40/80/120", "RMS step dB 40→80/80→120", "max vel jump dB", "floor dB rel", "clipped src",
           "loops n:worst seam", "flags"]
    lines = ["| " + " | ".join(hdr) + " |", "|" + "---|" * len(hdr)]
    for row in rows:
        lines.append("| " + " | ".join(row) + " |")
    lines.append(f"\n{len(rows)} instruments · {tot_ram:.1f} MB RAM (int16) · {tot_disk / 1024:.2f} GB on disk")
    txt = "\n".join(lines)
    if a.md:
        open(a.md, "w").write(txt + "\n")
    print(txt)


if __name__ == "__main__":
    main()
