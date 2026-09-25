#!/usr/bin/env python3
"""peaktrim.py — the per-key velocity-127 peak trim, closed THROUGH THE RUNTIME (tp108).

Max: every key at velocity 127 (Human 0, one player, every knob at its default) peaks at or under −1 dBFS through the real
engine — the flute's top keys reached +9.6 dBFS, the Jazz Pizz Bass +9.1, the mbira +8.9. Never a limiter or a clipper:
the recording's own level is trimmed, per key, in the library.

    python3 Tools/organics/peaktrim.py [--lib DIR] [--jobs N] [--dry-run] [ids…]       (default: every instrument)

1. MEASURE — Tests/organics_audit.sh's binary, `organics_audit --peaks`: every key of every articulation at velocity 127
   through OrganicEngine + OrganicsLibrary (the plugin's own code), the loudest of every round-robin take / random slot
   the key can play, Noise at its default 0.5 (a key-down thump rides on the note), 0.6 s held + the release.
2. NEED — per articulation, the keys are cut into ATOMS: the finest runs of keys that every attack region either fully
   covers or does not touch (a zone of one sample, on a consistently zoned instrument). An atom needs
   min(0, TARGET − its loudest key) dB (TARGET = −1.3 dBFS: 0.3 dB of margin under the −1 dBFS bar).
3. SMOOTH — the trim curve is the LARGEST curve that meets every need and never steps more than STEP dB (1.4) from one
   atom to the next: t(i) = min(0, min_j need(j) + STEP·|i − j|). Only the hot keys and a ramp into them move; the rest of
   the register keeps its natural balance, and no two adjacent keys differ by more than STEP from the trim.
4. KEEP THE CALIBRATION — the calibration key (artic 0's centre key, −24 LUFS through the engine, engine_calibrate.py)
   already peaks ≤ −1 dBFS (engine_calibrate limits it), so it needs no trim of its own; a ramp from a hot neighbour that
   would still reach it is REPORTED as a warning (then engine_calibrate.py must run again) — none in the factory library.
5. APPLY — each region's gainDb moves by its atom's trim (attack, release and noise regions alike: a key's release and
   mechanical noise keep their level against its note). A region that spans atoms with different trims is SPLIT into
   one region per run of equal trim (same sample, same fields) — only on inconsistently zoned instruments.
6. VERIFY — re-measure; repeat (≤ 3 passes) until no key is over the bar. The trims are CUMULATIVE and the smoothing is
   done on the cumulative curve, so repeated runs never stack steps. build-report.json → "peakTrim" records the curve
   per articulation (dB per key), the worst peak before / after, and the regions split.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LIB = os.path.expanduser("~/Developer/VST-Plugins/organics-library/compiled")
AUDIT = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "build", "organics_engine_test", "organics_audit"))
CEIL_DB = -1.0              # the bar
TARGET_DB = -1.3            # what the trim aims for (margin for the noise draw and the release's own transient)
STEP_DB = 1.4               # the largest key-to-key step the trim may add (Max: ≤ 1.5 dB)
NOISE = "0.5"               # the Noise knob's default


AUDIT_SH = os.path.normpath(os.path.join(HERE, "..", "..", "Tests", "organics_audit.sh"))


def ensure_audit() -> str:
    """(Re)build the audit binary against the CURRENT engine + test sources through Tests/organics_audit.sh (a stale
    binary from before tp108 has no --peaks mode and prints nothing). Returns "" when it is ready, else why not."""
    p = subprocess.run(["bash", AUDIT_SH, "build"], capture_output=True, text=True)   # builds, then the usage exit
    out = (p.stdout + p.stderr).strip()
    for bad in ("COMPILE FAIL", "LINK FAIL", "JUCE modules not found"):
        if bad in out:
            return out.splitlines()[0] if out else bad
    if not os.path.exists(AUDIT):
        return f"no binary at {AUDIT} after the build"
    q = subprocess.run([AUDIT, "--peaks", "/nonexistent-organics-root", "no-such-id", "127"], capture_output=True, text=True)
    if "PEAKSUMMARY" not in q.stdout:
        return f"{AUDIT} has no --peaks mode (exit {q.returncode})"
    return ""


def measure(lib: str, iid: str) -> dict:
    """{(artic, key): peak dBFS} at velocity 127 through the runtime."""
    env = dict(os.environ, TERRAIN_ORGANICS_DIR=lib, ORG_PEAK_NOISE=NOISE)
    p = subprocess.run([AUDIT, "--peaks", lib, iid, "127"], env=env, capture_output=True, text=True)
    out = {}
    for line in p.stdout.splitlines():
        f = line.split()
        if len(f) == 7 and f[0] == "PEAK" and f[1] == iid:
            out[(int(f[2]), int(f[3]))] = float(f[5])
    return out


def atoms_of(regs: list, a: int, keys: list) -> list:
    """The finest runs of consecutive keys that no attack region of artic a splits."""
    cuts = set()
    for r in regs:
        if r["kind"] == "attack" and r["a"] == a:
            cuts.add(r["lk"])
            cuts.add(r["hk"] + 1)
    out, cur = [], []
    for k in keys:
        if cur and (k in cuts or k != cur[-1] + 1):
            out.append(cur)
            cur = []
        cur.append(k)
    if cur:
        out.append(cur)
    return out


def envelope(need: list, step: float) -> list:
    """The largest t ≤ need (and ≤ 0) with |t(i) − t(i+1)| ≤ step."""
    n = len(need)
    t = [min(0.0, v) for v in need]
    for i in range(1, n):
        t[i] = min(t[i], t[i - 1] + step)
    for i in range(n - 2, -1, -1):
        t[i] = min(t[i], t[i + 1] + step)
    return t


def centre_key(regs: list) -> int:
    lo = min((r["lk"] for r in regs if r["kind"] == "attack" and r["a"] == 0), default=60)
    hi = max((r["hk"] for r in regs if r["kind"] == "attack" and r["a"] == 0), default=60)
    return 60 if lo <= 60 <= hi else (lo + hi + 1) // 2


def apply_trim(m: dict, trim: dict) -> int:
    """trim = {(artic, key): dB to ADD}. Moves gainDb; splits a region whose keys need different trims. → regions split."""
    new_regs, split = [], 0
    for r in m["regions"]:
        a = r["a"]
        ks = list(range(r["lk"], r["hk"] + 1))
        ts = [round(trim.get((a, k), 0.0), 3) for k in ks]
        if all(abs(t) < 1e-4 for t in ts):
            new_regs.append(r)
            continue
        runs = []
        for k, t in zip(ks, ts):
            if runs and abs(runs[-1][2] - t) < 1e-4:
                runs[-1][1] = k
            else:
                runs.append([k, k, t])
        if len(runs) > 1:
            split += 1
        for lk, hk, t in runs:
            q = dict(r)
            q["lk"], q["hk"] = lk, hk
            q["gainDb"] = round(r["gainDb"] + t, 3)
            new_regs.append(q)
    m["regions"] = new_regs
    return split


def merge_split(m: dict) -> int:
    """Undo apply_trim's splits: consecutive regions identical but for lk/hk, with contiguous keys, become one again."""
    out, merged = [], 0
    for r in m["regions"]:
        if out:
            p = out[-1]
            if (p["hk"] + 1 == r["lk"] and abs(p["gainDb"] - r["gainDb"]) <= 0.0025
                    and all(p.get(k) == r.get(k) for k in set(p) | set(r) if k not in ("lk", "hk", "gainDb"))):
                p["hk"] = r["hk"]
                merged += 1
                continue
        out.append(dict(r))
    m["regions"] = out
    return merged


def untrim(m: dict, total: dict) -> None:
    """Remove a previous run's cumulative trim (so a re-run starts from the calibrated, untrimmed library)."""
    for r in m["regions"]:
        t = total.get((r["a"], r["lk"]), 0.0)
        if abs(t) > 1e-6:
            r["gainDb"] = round(r["gainDb"] - t, 3)
    merge_split(m)


def trim_one(lib: str, iid: str, dry: bool) -> str:
    d = os.path.join(lib, iid)
    mp, rp = os.path.join(d, "map.json"), os.path.join(d, "build-report.json")
    m = json.load(open(mp))
    R = json.load(open(rp)) if os.path.exists(rp) else {}
    PT = R.get("peakTrim") or {}
    prev = {}
    for art, rec in (PT.get("artics") or {}).items():
        if art in m["artics"]:
            a = m["artics"].index(art)
            for k, v in rec.get("trimDb", {}).items():
                prev[(a, int(k))] = float(v)
    if prev and not dry:                                    # start again from the untrimmed, calibrated levels
        untrim(m, prev)
        with open(mp, "w") as f:
            json.dump(m, f, indent=1)
    PT = {}
    total = defaultdict(float)                              # (artic, key) → cumulative trim of THIS run
    before = measure(lib, iid)
    if not before:
        return f"{iid:40s} no measurement"
    worst0 = max(before.values())
    peaks = dict(before)
    splits, passes, warn = 0, 0, []
    ck = centre_key(m["regions"])
    for it in range(3):
        if max(peaks.values()) <= CEIL_DB - 0.05 and it > 0:
            break
        delta = {}
        for a in range(len(m["artics"])):
            keys = sorted(k for (aa, k) in peaks if aa == a)
            if not keys:
                continue
            at = [[k] for k in keys]                        # per KEY: a smooth curve, not zone-sized steps
            # an atom needs its current cumulative trim, lowered by what its loudest key is still over the target
            need = [min(total[(a, k)] + min(0.0, TARGET_DB - peaks[(a, k)]) for k in atom) for atom in at]
            if max(peaks[(a, k)] for k in keys) <= CEIL_DB:
                continue                                    # this articulation already meets the bar: untouched
            t = envelope(need, STEP_DB)
            for atom, tv in zip(at, t):
                if a == 0 and ck in atom and tv < -1e-6:
                    warn.append(f"calibration key {ck} would be trimmed {tv:.2f} dB")
                for k in atom:
                    dv = tv - total[(a, k)]
                    if abs(dv) > 1e-4:
                        delta[(a, k)] = dv
        if not delta:
            break
        passes += 1
        if dry:
            break
        splits += apply_trim(m, delta)
        for k, v in delta.items():
            total[k] += v
        with open(mp, "w") as f:
            json.dump(m, f, indent=1)
        peaks = measure(lib, iid)
    worst1 = max(peaks.values())
    if not dry:
        arts = {}
        for a, name in enumerate(m["artics"]):
            tk = {str(k): round(v, 2) for (aa, k), v in sorted(total.items()) if aa == a and abs(v) > 1e-4}
            keys = [k for (aa, k) in peaks if aa == a]
            rec = {"trimDb": tk, "maxTrimDb": round(min([0.0] + list(tk.values())), 2), "keysTrimmed": len(tk)}
            if keys:
                rec["peak127MaxDb"] = round(max(peaks[(a, k)] for k in keys), 2)
            arts[name] = rec
        R["peakTrim"] = {"pass": "tp108", "ceilDb": CEIL_DB, "targetDb": TARGET_DB, "stepDb": STEP_DB, "noise": float(NOISE),
                         "peak127MaxBeforeDb": round(max(PT.get("peak127MaxBeforeDb", worst0), worst0) if PT else worst0, 2),
                         "peak127MaxDb": round(worst1, 2), "regionsSplit": int(PT.get("regionsSplit", 0)) + splits,
                         "calibrationKey": ck, "artics": arts}
        # the calibration key's own trim is a PEAK LIMIT on the calibration (the tp106 rule engine_calibrate.py already
        # applies at one press: the key's loudest take at v127 may not pass −1 dBFS) — recorded like it, so the audit's
        # loudness bar reads −24 − peakLimitedDb
        ckt = round(-total.get((0, ck), 0.0), 2)
        L = R.setdefault("loudness", {})
        base = float(L.get("peakLimitedDb", 0.0)) - float(L.get("peakLimitedByTrimDb", 0.0))
        L["peakLimitedByTrimDb"] = ckt
        L["peakLimitedDb"] = round(base + ckt, 2)
        R["peakTrim"]["calibrationKeyTrimDb"] = -ckt
        if warn:
            R["peakTrim"]["warnings"] = sorted(set(w.replace("would be trimmed", "trimmed (a peak limit on the calibration)")
                                                   for w in warn))
        with open(rp, "w") as f:
            json.dump(R, f, indent=1)
    hot0 = sum(1 for v in before.values() if v > CEIL_DB)
    hot1 = sum(1 for v in peaks.values() if v > CEIL_DB)
    return (f"{iid:40s} worst {worst0:+6.2f} → {worst1:+6.2f} dBFS · keys over {CEIL_DB}: {hot0:3d} → {hot1:3d} · "
            f"passes {passes} · split {splits}" + (f" · WARN {sorted(set(warn))}" if warn else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ids", nargs="*")
    ap.add_argument("--lib", default=DEFAULT_LIB)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    why = ensure_audit()
    if why:
        print(f"cannot measure through the engine: {why}")
        return 1
    ids = a.ids or sorted(e["id"] for e in json.load(open(os.path.join(a.lib, "index.json"))))
    with ThreadPoolExecutor(a.jobs) as ex:
        for line in ex.map(lambda i: trim_one(a.lib, i, a.dry_run), ids):
            print(line)
            sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
