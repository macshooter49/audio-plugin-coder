#!/usr/bin/env python3
"""retune.py — re-measure every compiled instrument's tuning correction (tfix) with tuning.measure_pitch (tp108).

The compiler (torgc.py) measures each sample's pitch while it renders it and writes the per-region tfix
(tuning.assign_tfix). This pass does the same measurement on an ALREADY COMPILED instrument — the decoded
samples/NNNN.flac of every attack region, from its onset to its end, against its (root-fixed) root — so a detector or
rule change reaches the library without re-rendering a single sample (loops, fades, levels, peak trims are untouched:
only "tfix" — and a root the new detector proves a semitone off — change).

    python3 Tools/organics/retune.py [--lib DIR] [--jobs N] [--dry-run] [ids…]       (default: every instrument)

Writes map.json (tfix, root) and build-report.json → "tfix" (per-articulation statistics incl. where each value came
from: measured / note / sustained:<artic> / neighbours / none) + "tfixPass": "tp108".
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analyse as an  # noqa: E402
import tuning as tu   # noqa: E402

DEFAULT_LIB = os.path.expanduser("~/Developer/VST-Plugins/organics-library/compiled")
RECIPES = os.path.join(HERE, "recipes")


def measure_instrument(d: str, recipe: dict, m: dict) -> dict:
    """{region index: measurement} for every attack region (one decode per sample)."""
    inh = tu.pitch_kind(recipe.get("category", ""))
    out = {}
    cache = {}
    by_smp = {}
    for i, r in enumerate(m["regions"]):
        if r["kind"] == "attack":
            by_smp.setdefault(r["smp"], []).append(i)
    for smp, idx in by_smp.items():
        x, sr = sf.read(os.path.join(d, "samples", m["samples"][smp]), dtype="float64", always_2d=True)
        mono = an.to_mono(x)
        for i in idx:
            r = m["regions"][i]
            key = (r["onset"], r["end"], r["root"])
            if key not in cache:
                end = min(int(r["end"]), len(mono))
                on = int(max(r["start"], min(end - 1, r["onset"])))
                cache[key] = tu.measure_pitch(mono, sr, on, end, an.midi_hz(r["root"]), kind=inh)
            out[i] = cache[key]
    return out


AUDIT = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "build", "organics_engine_test", "organics_audit"))


def _vels(recipe: dict, m: dict) -> str:
    """The velocities the closure (and Tests/organics_pitch_check.py) render: 40/80/120 on a struck / plucked instrument
    (one physical tuning per note: the median), every velocity layer's CENTRE on a performed one (each layer is its own
    take, heard on its own there; between two centres the runtime crossfades two in-tune takes)."""
    if recipe.get("category") not in tu.PERFORMED_CATEGORIES:
        return "40,80,120"
    c = sorted({(r["lv"] + r["hv"]) // 2 for r in m["regions"] if r["kind"] == "attack"})
    while len(c) > 8:
        c = sorted(set(c[::2]))
    return ",".join(str(v) for v in c)


def on_plateau(m: dict, a: int, key: int, vel: int) -> bool:
    """True when velocity `vel` plays ONE layer of artic `a` at `key` — outside the runtime's equal-power seam between two
    butting layers (OrganicsLibrary: each side reaches 30 % into its own layer, 1.5…8 velocities)."""
    for r in m["regions"]:
        if r["kind"] != "attack" or r["a"] != a or not (r["lk"] <= key <= r["hk"]) or not (r["lv"] <= vel <= r["hv"]):
            continue
        d = min(8.0, max(1.5, 0.3 * (r["hv"] - r["lv"] + 1)))
        if r["lv"] > 1 and vel < r["lv"] - 0.5 + d:
            return False
        if r["hv"] < 127 and vel > r["hv"] + 0.5 - d:
            return False
    return True


def engine_residuals(lib: str, iid: str, recipe: dict, m: dict, stats: dict) -> dict:
    """Render every key (Tuning = Equal) through the runtime (organics_audit --pitchdump) and measure it with the
    same detector: {(artic, key): {vel: deviation from ET (or from the articulation's fitted stretch)}}."""
    import glob
    import subprocess
    import tempfile
    kind = tu.pitch_kind(recipe.get("category", ""))
    performed = recipe.get("category") in tu.PERFORMED_CATEGORIES
    tmp = tempfile.mkdtemp(prefix="retune_")
    out = {}
    try:
        subprocess.run([AUDIT, "--pitchdump", lib, tmp, iid, _vels(recipe, m)], capture_output=True,
                       env=dict(os.environ, TERRAIN_ORGANICS_DIR=lib))
        for f in glob.glob(os.path.join(tmp, "*.f32")):
            parts = [int(v) for v in os.path.basename(f)[:-4].split("_")]
            a, key, vel = parts[:3]
            reg = parts[3] if len(parts) > 3 else -1
            if performed and not on_plateau(m, a, key, vel):
                continue                                   # a crossfade of two takes: judged by its two takes
            x = np.fromfile(f, dtype=np.float32).astype(np.float64)
            on = an.find_onset(x, 0, len(x))
            r = tu.measure_pitch(x, 48000, on, len(x), an.midi_hz(key), kind=kind)
            st = (stats.get(m["artics"][a]) or {}).get("stretchCurveCents")
            ref = 0.0
            if st:
                ks = sorted((int(k), float(v)) for k, v in st.items())
                ref = float(np.interp(key, [k for k, _ in ks], [v for _, v in ks]))
            if r.get("ok") and (abs(r["cents"] - ref) <= 30.0 or r.get("strong")):
                out.setdefault((a, key), {})[vel] = (r["cents"] - ref, reg)
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)
    return out


def engine_close(lib: str, iid: str, recipe: dict, m: dict, stats: dict, iters: int = 3) -> dict:
    """THE CLOSURE THROUGH THE ENGINE (like engine_calibrate.py for loudness): what the player hears is the runtime's
    mix — stacked samples (a rotary organ's two rotors), crossfaded layers, a note repitched across its zone. Each
    attack region moves by the median residual of the keys it plays: struck / plucked = the median over 40/80/120 per key
    (one physical tuning per note, every layer and take moves together); performed = the keys at a velocity inside its
    own layer. A key the detector cannot read through the engine leaves its regions on the sample measurement."""
    performed = recipe.get("category") in tu.PERFORMED_CATEGORIES
    log = []
    prev = {}
    for it in range(iters):
        with open(os.path.join(lib, iid, "map.json"), "w") as f:
            json.dump(m, f, indent=1)
        res = engine_residuals(lib, iid, recipe, m, stats)
        if not res:
            break
        moved, worst = 0, 0.0
        regs = m["regions"]

        def group(ri):
            """performed: the take itself; struck / plucked: the note (its zone: every layer and take, one tuning)."""
            r = regs[ri]
            return ("r", ri) if performed else (r["a"], r["lk"], r["hk"])
        played = {}                                        # group → residuals of the notes that played it
        for (a, k), dv in res.items():
            if performed:
                for v, (d, reg) in dv.items():
                    if 0 <= reg < len(regs):
                        played.setdefault(group(reg), []).append(d)
            else:
                # the pitch check's reading of a struck key: the median over its velocities, credited to every zone
                # that covers the key
                d = float(np.median([dd for dd, _ in dv.values()]))
                for g in {(r["a"], r["lk"], r["hk"]) for r in regs
                          if r["kind"] == "attack" and r["a"] == a and r["lk"] <= k <= r["hk"]}:
                    played.setdefault(g, []).append(d)
        members = {}
        for ri, r in enumerate(regs):
            if r["kind"] == "attack":
                members.setdefault(group(ri), []).append(ri)
        for g, vals in played.items():
            delta = -float(np.median(vals))
            # a reading that flips sign between passes (a note whose pitch glides: the detector sits on one side, then
            # the other) is met halfway instead of chased
            if prev.get(g) is not None and prev[g] * delta < 0:
                delta *= 0.5
            prev[g] = delta
            if abs(delta) < 0.3:
                continue
            for ri in members.get(g, []):
                r = regs[ri]
                new = round(max(-tu.TFIX_MAX, min(tu.TFIX_MAX, r["tfix"] + delta)), 1)
                if new != r["tfix"]:
                    moved += 1
                    worst = max(worst, abs(delta))
                    r["tfix"] = new
        log.append({"pass": it + 1, "keysMeasured": len(res), "regionsMoved": moved, "largestMoveCents": round(worst, 1)})
        if moved == 0:
            break
    # releases follow their note's attack correction
    by_root = {}
    for r in m["regions"]:
        if r["kind"] == "attack":
            by_root.setdefault((r["a"], r["root"]), []).append(r["tfix"])
    for r in m["regions"]:
        if r["kind"] == "release":
            v = by_root.get((r["a"], r["root"]))
            r["tfix"] = round(float(np.median(v)), 1) if v else 0.0
    return {"passes": log}


def retune_one(args):
    lib, iid, dry, closure = args
    d = os.path.join(lib, iid)
    rp = os.path.join(RECIPES, iid + ".json")
    if not os.path.exists(os.path.join(d, "map.json")) or not os.path.exists(rp):
        return iid, None, "skipped (no map or recipe)"
    recipe = json.load(open(rp))
    m = json.load(open(os.path.join(d, "map.json")))
    before = [r.get("tfix", 0.0) for r in m["regions"]]
    roots_before = [r["root"] for r in m["regions"]]
    f0s = {} if recipe.get("unpitched") else measure_instrument(d, recipe, m)
    recs = m["regions"]
    stats = tu.assign_tfix(recipe, m["artics"], recs, f0s)
    for r in recs:
        r.pop("_tfixFrom", None)
    close = None
    if closure and not dry and not recipe.get("unpitched") and not recipe.get("pitchCheckWhy"):
        close = engine_close(lib, iid, recipe, m, stats)
        stats["engineClosure"] = close
    changed = sum(1 for r, b in zip(recs, before) if abs(r["tfix"] - b) > 0.05)
    rootmv = sum(1 for r, b in zip(recs, roots_before) if r["root"] != b)
    if not dry:
        with open(os.path.join(d, "map.json"), "w") as f:
            json.dump(m, f, indent=1)
        bp = os.path.join(d, "build-report.json")
        R = json.load(open(bp)) if os.path.exists(bp) else {}
        old_roots = (R.get("tfix") or {}).get("rootFixes", [])
        if old_roots and "rootFixes" not in stats:
            stats["rootFixes"] = old_roots                     # the compiler's own root fixes stay on record
        elif old_roots:
            stats["rootFixes"] = old_roots + stats["rootFixes"]
        R["tfix"] = stats
        R["tfixPass"] = "tp108"
        with open(bp, "w") as f:
            json.dump(R, f, indent=1)
    summ = {a: {k: v for k, v in s.items() if k in ("measuredRegions", "unreliableRegions", "sources", "worst")}
            for a, s in stats.items() if a not in ("rootFixes", "engineClosure")}
    return iid, summ, (f"{changed} regions changed, {rootmv} roots moved"
                       + (f", engine closure {close['passes']}" if close else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ids", nargs="*")
    ap.add_argument("--lib", default=DEFAULT_LIB)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--no-engine", action="store_true", help="skip the closure through the runtime")
    a = ap.parse_args()
    ids = a.ids or sorted(e["id"] for e in json.load(open(os.path.join(a.lib, "index.json"))))
    if not a.no_engine:
        import peaktrim
        why = peaktrim.ensure_audit()        # builds the audit binary against the current sources
        if why:
            print(f"the closure needs the audit binary: {why} (or pass --no-engine)")
            return 1
    with ProcessPoolExecutor(a.jobs) as ex:
        for iid, summ, msg in ex.map(retune_one, [(a.lib, i, a.dry_run, not a.no_engine) for i in ids]):
            print(f"{iid:40s} {msg}")
            for art, s in (summ or {}).items():
                print(f"    {art:16s} measured {s.get('measuredRegions', 0):4d} unreliable {s.get('unreliableRegions', 0):4d} "
                      f"worst {s.get('worst', 0):+6.1f}  {s.get('sources', {})}")
            sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
