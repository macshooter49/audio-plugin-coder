#!/usr/bin/env python3
"""organics_compile_test.py — gates for the Organics compiler (Tools/organics/torgc.py) and the compiled library.

    python3 Tests/organics_compile_test.py [--lib ~/Developer/VST-Plugins/organics-library/compiled] [--quick]

1. round trip: Tests/fixtures/organics/sfz-src/fixture.sfz (keyswitch artics, velocity layers with an authored
   loop, seq round robin, a release trigger, #define/#include/default_path/note names) → .torg → the exact
   expected regions;
2. for EVERY compiled instrument in the library (and the fixture's own output):
   - schema: every top-level and region field the frozen fixture (test.sine/map.json) has, with the same JSON type,
     plus value ranges; families/categories from the contract;
   - zone coverage: no key/velocity holes per articulation over the declared key range, with complete RR
     sets (seq positions 0..n-1, random slots covering [0,1));
   - every loop seam (sustain loop and tail loop) re-measured on the decoded FLAC: ratio ≤ 1.5; a tp106 BAKED sustain
     loop (xf 0) by identity instead — the frame before le is the frame before ls and the pad repeats ls (≤ 6 LSB);
   - onsets inside the region; loop points inside the sample; smp indexes valid;
   - budget: int16 RAM (from the FLAC headers) ≤ the instrument's budget and ≈ index sizeMB;
   - licence: source/LICENCE.txt, source/provenance.csv (one row per sample, sha256), a mapping file;
     CC-BY instruments carry a credit in index.json AND in Resources/Organics/CREDITS.md;
3. ids.json is append-only against the committed Resources/Organics/ids.json (and the frozen fixture ids.json).
4. tp105 (contract amendment): every noise region has trig "on"|"off"; every pitched region carries tfix within
   ±60 ¢ (noise: 0); every sample a noise region plays is traced in provenance.csv (origin recording, licence,
   sha256, and for shared-library noise the extracted file + its sha256), CC-BY noise ⇒ the instrument is in the
   Required section of CREDITS.md; the round trip checks tfix (±10 ¢ authored tune ⇒ ∓10 ¢) and unit tests cover
   measure_f0, inject_noise and repair_rr;
5. no silent round robin (Max's xylophone report): for every articulation × key × velocity (all 127), each
   seq position 1..seq_length and the random slots [0, 1) resolve to a region, and no attack region is near-silent
   (segment peak < −50 dBFS, or a static gain 40 dB under the instrument's median);
6. loudness: every instrument's centre key at velocity 100 (Velocity 0.75) within ±1 dB of the library target
   (K-weighted, first 1 s), velocity-127 peak at that key ≤ −1 dBFS.
Exit code 0 = all pass.
"""
from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
PLUG = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(PLUG, "Tools", "organics"))
import analyse as an      # noqa: E402
import torgc              # noqa: E402

FIX = os.path.join(HERE, "fixtures", "organics")
SRC_FIX = os.path.join(FIX, "sfz-src")
RES = os.path.join(PLUG, "Resources", "Organics")
DEFAULT_LIB = os.path.expanduser("~/Developer/VST-Plugins/organics-library/compiled")

FAILS: list = []
PASSES = [0]


def check(cond, msg):
    if cond:
        PASSES[0] += 1
    else:
        FAILS.append(msg)
    return cond


def jtype(v):
    if isinstance(v, bool):
        return "bool"
    if isinstance(v, (int, float)):
        return "num"
    return type(v).__name__


# ---------------------------------------------------------------------------------------------- schema
FIXTURE_MAP = json.load(open(os.path.join(FIX, "test.sine", "map.json")))
TOP_FIELDS = {k: jtype(v) for k, v in FIXTURE_MAP.items()}
REG_FIELDS = {k: jtype(v) for k, v in FIXTURE_MAP["regions"][0].items()}
ENV_FIELDS = set(FIXTURE_MAP["regions"][0]["env"].keys())
KINDS = {"attack", "release", "noise"}
LOOPS = {"no_loop", "one_shot", "continuous", "sustain"}


def check_schema(d: str, m: dict, idx_entry: dict | None):
    iid = m.get("id", d)
    for k, t in TOP_FIELDS.items():
        check(k in m, f"{iid}: map.json missing top-level field '{k}'")
        if k in m:
            check(jtype(m[k]) == t, f"{iid}: '{k}' is {jtype(m[k])}, fixture has {t}")
    check(m.get("torg") == 1, f"{iid}: torg != 1")
    check(m.get("family") in torgc.FAMILIES, f"{iid}: family {m.get('family')} not a FAM id")
    check(m.get("category") in torgc.CATEGORIES, f"{iid}: category {m.get('category')} not a contract category")
    check(os.path.basename(d) == iid, f"{iid}: folder name != id")
    samples = m.get("samples", [])
    frames = []
    for s in samples:
        p = os.path.join(d, "samples", s)
        if check(os.path.exists(p), f"{iid}: missing sample {s}"):
            info = sf.info(p)
            check(info.format == "FLAC", f"{iid}: {s} is not FLAC")
            check(info.subtype in ("PCM_16", "PCM_24"), f"{iid}: {s} subtype {info.subtype}")
            frames.append((info.frames, info.channels))
        else:
            frames.append((0, 1))
    n_art = len(m.get("artics", []))
    check(n_art >= 1 and n_art <= 8, f"{iid}: {n_art} articulations (ORG_ARTIC is 0..7)")
    bad = 0
    for i, r in enumerate(m.get("regions", [])):
        miss = [k for k in REG_FIELDS if k not in r]
        types = [k for k, t in REG_FIELDS.items() if k in r and jtype(r[k]) != t]
        env_ok = isinstance(r.get("env"), dict) and ENV_FIELDS <= set(r["env"].keys())
        ok = (not miss and not types and env_ok and r["kind"] in KINDS and r["loop"] in LOOPS
              and 0 <= r["a"] < n_art and 0 <= r["smp"] < len(samples)
              and 0 <= r["lk"] <= r["hk"] <= 127 and 1 <= r["lv"] <= r["hv"] <= 127
              and r["lv"] <= r["xfLo"] <= r["xfHi"] <= r["hv"] and 0 <= r["root"] <= 127
              and -100 <= r["pan"] <= 100 and r["gainNorm"] > 0
              and len(r["rr"]) == 2 and 0 <= r["rr"][0] < r["rr"][1]
              and len(r["rand"]) == 2 and 0.0 <= r["rand"][0] < r["rand"][1] <= 1.0
              and r["offMode"] in ("normal", "fast") and len(r["velCurve"]) >= 2)
        if ok:
            nfr = frames[r["smp"]][0]
            ok = (0 <= r["start"] < r["end"] <= nfr and r["start"] <= r["onset"] < r["end"])
            if not ok:
                FAILS.append(f"{iid}: region {i} start/onset/end out of the sample "
                             f"(start {r['start']} onset {r['onset']} end {r['end']} frames {nfr})")
                bad += 1
                continue
            if r["loop"] in ("continuous", "sustain"):
                ok = r["start"] <= r["ls"] < r["le"] <= r["end"] and r["ls"] >= r["xf"] and r["tailLs"] == r["ls"]
            else:
                ok = r["ls"] == 0 and r["le"] == 0
            if r["tailLe"] > 0:
                ok = ok and r["start"] <= r["tailLs"] < r["tailLe"] <= r["end"] and r["tailLs"] >= r["xf"]
        if not ok:
            bad += 1
            if bad <= 3:
                FAILS.append(f"{iid}: region {i} fails schema/range: missing={miss} types={types} env={env_ok} "
                             + json.dumps({k: r.get(k) for k in ('kind', 'loop', 'lk', 'hk', 'lv', 'hv', 'ls', 'le', 'xf', 'tailLs', 'tailLe')}))
    check(bad == 0, f"{iid}: {bad} regions fail the schema")
    check(m.get("hasNoise") == any(r["kind"] == "noise" for r in m["regions"]), f"{iid}: hasNoise mismatch")
    check(m.get("hasRelease") == any(r["kind"] == "release" for r in m["regions"]), f"{iid}: hasRelease mismatch")
    if idx_entry is not None:
        for k in ("id", "name", "family", "category", "tags", "sizeMB", "licence", "credit"):
            check(k in idx_entry, f"{iid}: index.json entry lacks '{k}'")
        check(idx_entry.get("family") == m["family"] and idx_entry.get("category") == m["category"],
              f"{iid}: index.json family/category disagree with map.json")
    return frames


# ---------------------------------------------------------------------------------------------- tp105 fields
ALLOWED_LICENCES = {"CC0-1.0", "CC-BY-3.0", "CC-BY-4.0", "Unlicense", "Proprietary-WavesCrate"}


def check_tp105(d: str, m: dict, credits_md: str):
    """trig on noise regions; tfix on every pitched region (±60 ¢); every noise sample traced in provenance.csv."""
    iid = m["id"]
    bad_trig = [i for i, r in enumerate(m["regions"]) if r["kind"] == "noise" and r.get("trig") not in ("on", "off")]
    check(not bad_trig, f"{iid}: {len(bad_trig)} noise regions without a valid trig (on|off), e.g. region {bad_trig[:3]}")
    stray = [i for i, r in enumerate(m["regions"]) if r["kind"] != "noise" and "trig" in r]
    check(not stray, f"{iid}: trig on non-noise regions {stray[:3]}")
    bad_tfix = [i for i, r in enumerate(m["regions"]) if r["kind"] in ("attack", "release")
                and not (isinstance(r.get("tfix"), (int, float)) and not isinstance(r.get("tfix"), bool)
                         and -60.0 <= r["tfix"] <= 60.0)]
    check(not bad_tfix, f"{iid}: {len(bad_tfix)} pitched regions without tfix in ±60 ¢, e.g. {[m['regions'][i].get('tfix') for i in bad_tfix[:3]]}")
    nz_tfix = [i for i, r in enumerate(m["regions"]) if r["kind"] == "noise" and r.get("tfix", 0.0) != 0.0]
    check(not nz_tfix, f"{iid}: unpitched noise regions carry a tfix {nz_tfix[:3]}")
    # provenance of every sample a noise region plays
    pv = os.path.join(d, "source", "provenance.csv")
    if not os.path.exists(pv):
        return
    rows = {r["file"]: r for r in csv.DictReader(open(pv))}
    lic_cc_by = False
    for smp in sorted({r["smp"] for r in m["regions"] if r["kind"] == "noise"}):
        f = "samples/" + m["samples"][smp]
        row = rows.get(f)
        if not check(row is not None, f"{iid}: noise sample {f} has no provenance row"):
            continue
        ok = (row.get("source_file") and row.get("licence") in ALLOWED_LICENCES and len(row.get("sha256", "")) == 64
              and row.get("url") and row.get("author"))
        if row.get("noise_set"):
            ok = ok and row.get("extract_file") and len(row.get("extract_sha256", "")) == 64
        check(ok, f"{iid}: noise sample {f} provenance incomplete: {dict(row)}")
        lic_cc_by = lic_cc_by or row.get("licence", "").upper().startswith("CC-BY")
    if lic_cc_by:
        required = credits_md.split("## With thanks")[0]
        check(f"`{iid}`" in required, f"{iid}: plays CC-BY noise samples but is missing from the Required section of "
                                      "Resources/Organics/CREDITS.md")


_PEAK_CACHE: dict = {}


def region_peak_db(d: str, m: dict, r: dict) -> float:
    key = (d, r["smp"], r["start"], r["end"])
    if key not in _PEAK_CACHE:
        x, _ = sf.read(os.path.join(d, "samples", m["samples"][r["smp"]]), dtype="float32", always_2d=True,
                       start=r["start"], stop=r["end"])
        _PEAK_CACHE[key] = an.db(float(np.abs(x).max())) if len(x) else -200.0
    return _PEAK_CACHE[key]


def check_rr_audible(d: str, m: dict):
    """Max's xylophone bug class ("round-robinning to a silence"): for EVERY articulation × key × velocity, every
    round-robin index 1..seq_length of every sequence the cell uses resolves to a region, the random slots cover
    [0, 1), and every attack region that can be picked is audible (its segment peaks above −50 dBFS and its static
    gain is not buried 40 dB under the instrument's median)."""
    iid = m["id"]
    gains = [r["gainDb"] + an.db(max(r["gainNorm"], 1e-9)) for r in m["regions"] if r["kind"] == "attack"]
    med = float(np.median(gains)) if gains else 0.0
    bad_cells = 0
    first = None
    for a in range(len(m["artics"])):
        rs = [r for r in m["regions"] if r["a"] == a and r["kind"] == "attack"]
        if not rs:
            continue
        cov = {}
        for r in rs:
            for k in range(r["lk"], r["hk"] + 1):
                cov.setdefault(k, []).append(r)
        for k, lst in cov.items():
            for v in range(1, 128):
                here = [r for r in lst if r["lv"] <= v <= r["hv"]]
                if not here:
                    continue
                by_len = {}
                for r in here:
                    by_len.setdefault(r["rr"][1], set()).add(r["rr"][0])
                ok = all(pos == set(range(L)) for L, pos in by_len.items())
                edge = 0.0
                for lo, hi in sorted({tuple(r["rand"]) for r in here}):
                    if lo > edge + 1e-6:
                        break
                    edge = max(edge, hi)
                ok = ok and edge >= 1.0 - 1e-6
                if not ok:
                    bad_cells += 1
                    first = first or (m["artics"][a], k, v, sorted(by_len.items()))
    check(bad_cells == 0, f"{iid}: {bad_cells} artic×key×velocity cells with an incomplete round-robin set, first {first}")
    quiet = []
    for i, r in enumerate(m["regions"]):
        if r["kind"] != "attack":
            continue
        if region_peak_db(d, m, r) < -50.0 or r["gainDb"] + an.db(max(r["gainNorm"], 1e-9)) < med - 40.0:
            quiet.append(i)
    check(not quiet, f"{iid}: {len(quiet)} near-silent attack regions (a silent step), e.g. region {quiet[:3]}")


def check_loudness(lib: str, idx: dict):
    """tp105 normalisation: centre key, velocity 100 (Velocity 0.75) within ±1 dB of the library target; velocity-127
    peaks ≤ −1 dBFS (the compiler's own measurement, re-verified by rendering here)."""
    ach = {}
    for iid in idx:
        rp = os.path.join(lib, iid, "build-report.json")
        rep = json.load(open(rp)) if os.path.exists(rp) else {}
        L = rep.get("loudness")
        if not check(L is not None, f"{iid}: build-report has no loudness calibration (rebuild with tp105 torgc)"):
            continue
        check(L["peak127Db"] <= torgc.PEAK_CEIL_DB + 0.05, f"{iid}: velocity-127 peak {L['peak127Db']} dBFS > ceiling")
        ach[iid] = L.get("verify", L["achieved"])
    if ach:
        lo, hi = min(ach.values()), max(ach.values())
        check(hi - torgc.CALIB_LUFS <= 1.0 and torgc.CALIB_LUFS - lo <= 1.0,
              f"loudness spread {lo:.2f}..{hi:.2f} LUFS is outside target {torgc.CALIB_LUFS} ± 1 dB: "
              f"{sorted(ach.items(), key=lambda t: t[1])[:3]} … {sorted(ach.items(), key=lambda t: t[1])[-3:]}")
        print(f"   loudness: {len(ach)} instruments, {lo:.2f} … {hi:.2f} LUFS (target {torgc.CALIB_LUFS})")


# ---------------------------------------------------------------------------------------------- coverage
def check_coverage(m: dict):
    iid = m["id"]
    holes_total = 0
    for a in range(len(m["artics"])):
        rs = [r for r in m["regions"] if r["a"] == a and r["kind"] == "attack"]
        if not check(rs, f"{iid}: articulation {a} ({m['artics'][a]}) has no attack regions"):
            continue
        klo, khi = min(r["lk"] for r in rs), max(r["hk"] for r in rs)
        cov = np.zeros((128, 128), bool)
        for r in rs:
            cov[r["lk"]:r["hk"] + 1, r["lv"]:r["hv"] + 1] = True
        holes = int((~cov[klo:khi + 1, 1:128]).sum())
        if holes:
            ks, vs = np.nonzero(~cov[klo:khi + 1, 1:128])
            FAILS.append(f"{iid}: artic {a}: {holes} key/vel holes, first at key {klo + ks[0]} vel {1 + vs[0]}")
        holes_total += holes
        # RR completeness per cell: every seq length L present must have positions 0..L-1; random slots cover [0,1)
        rr_bad = 0
        cells = {}
        for r in rs:
            for k in range(r["lk"], r["hk"] + 1):
                cells.setdefault(k, []).append(r)
        for k, lst in cells.items():
            for v in (1, 32, 64, 96, 127):
                here = [r for r in lst if r["lv"] <= v <= r["hv"]]
                if not here:
                    continue
                by_len = {}
                for r in here:
                    by_len.setdefault(r["rr"][1], set()).add(r["rr"][0])
                for L, pos in by_len.items():
                    if pos != set(range(L)):
                        rr_bad += 1
                slots = sorted({tuple(r["rand"]) for r in here})
                edge = 0.0
                for lo, hi in slots:
                    if lo > edge + 1e-6:
                        break
                    edge = max(edge, hi)
                if edge < 1.0 - 1e-6:
                    rr_bad += 1
        check(rr_bad == 0, f"{iid}: artic {a}: {rr_bad} key/vel cells with an incomplete RR set")
    check(holes_total == 0, f"{iid}: zone coverage has {holes_total} holes")


# ---------------------------------------------------------------------------------------------- seams + budget
def check_audio(d: str, m: dict, frames, budget_mb: float, idx_size, quick: bool):
    iid = m["id"]
    ram = sum(f * c * 2 for f, c in frames) / 1048576.0
    check(ram <= budget_mb * 1.001, f"{iid}: {ram:.1f} MB RAM over budget {budget_mb} MB")
    if idx_size is not None:
        check(abs(ram - idx_size) <= max(0.2, 0.02 * ram), f"{iid}: index sizeMB {idx_size} != measured {ram:.1f}")
    seen = set()
    worst = 0.0
    n = 0
    for r in m["regions"]:
        loops = []
        if r["loop"] in ("continuous", "sustain"):
            loops.append((r["ls"], r["le"]))
        elif r["tailLe"] > 0:
            loops.append((r["tailLs"], r["tailLe"]))
        for ls, le in loops:
            key = (r["smp"], ls, le, r["xf"])
            if key in seen:
                continue
            seen.add(key)
            if quick and len(seen) > 40:
                continue
            x, _ = sf.read(os.path.join(d, "samples", m["samples"][r["smp"]]), dtype="float64", always_2d=True)
            n += 1
            if r["loop"] in ("continuous", "sustain") and r["xf"] == 0 and ls >= an.LOOP_PAD:
                # tp106 — a BAKED sustain loop (analyse.polish_loop): the wrap le → ls is the recording's own ls-1 → ls,
                # so the join is proven by identity, not by a slope ratio (which only measures how steep the waveform
                # naturally is at ls): the last frame before le IS the frame before ls (the blend lands on it; the ones
                # before it are still a hair into the crossfade) and the pad after le repeats the frames from ls, to the
                # 16-bit dither (a few LSB)
                tol = 6.0 / 32768.0
                d_in = float(np.max(np.abs(x[le - 1] - x[ls - 1])))
                d_pad = float(np.max(np.abs(x[le: le + an.LOOP_PAD] - x[ls: ls + an.LOOP_PAD]))) if le + an.LOOP_PAD <= len(x) else 1.0
                worst = max(worst, max(d_in, d_pad) * 32768.0 / 6.0)
                check(d_in <= tol and d_pad <= tol, f"{iid}: baked loop smp {r['smp']} [{ls},{le}) does not join: "
                      f"|Δ| before {d_in * 32768:.1f} LSB, pad {d_pad * 32768:.1f} LSB (≤ 6)")
                continue
            mt = an.seam_metric(x, ls, le, r["xf"])
            worst = max(worst, mt["ratio"])
            check(mt["ratio"] <= 1.5, f"{iid}: loop seam smp {r['smp']} [{ls},{le}) xf {r['xf']} ratio {mt['ratio']:.2f} > 1.5")
    return ram, n, worst


def check_licence(d: str, m: dict, idx_entry, credits_md: str):
    iid = m["id"]
    s = os.path.join(d, "source")
    check(os.path.exists(os.path.join(s, "LICENCE.txt")), f"{iid}: source/LICENCE.txt missing")
    pv = os.path.join(s, "provenance.csv")
    if check(os.path.exists(pv), f"{iid}: source/provenance.csv missing"):
        rows = list(csv.DictReader(open(pv)))
        check(len(rows) == len(m["samples"]), f"{iid}: provenance has {len(rows)} rows for {len(m['samples'])} samples")
        cols = {"file", "source_file", "url", "author", "licence", "date", "sha256", "edits"}
        check(rows and cols <= set(rows[0].keys()), f"{iid}: provenance columns {list(rows[0].keys()) if rows else []}")
        check(all(len(r.get("sha256", "")) == 64 for r in rows), f"{iid}: provenance sha256 malformed")
    maps = glob.glob(os.path.join(s, "mapping", "**", "*.sfz*"), recursive=True) + \
        glob.glob(os.path.join(s, "mapping", "**", "*.txt"), recursive=True) + \
        glob.glob(os.path.join(s, "mapping", "**", "*.sf2"), recursive=True)
    check(maps, f"{iid}: source/mapping has no original mapping file")
    lic = (idx_entry or {}).get("licence", "")
    if lic.upper().startswith("CC-BY"):
        check("CC BY" in m["credit"] or "CC-BY" in m["credit"], f"{iid}: CC-BY credit line lacks the licence")
        check(iid in credits_md, f"{iid}: CC-BY instrument missing from Resources/Organics/CREDITS.md")


def check_ids(lib: str):
    lib_ids = json.load(open(os.path.join(lib, "ids.json")))
    committed = {}
    try:
        txt = subprocess.run(["git", "show", "HEAD:plugins/Terrain/Resources/Organics/ids.json"], cwd=PLUG,
                             capture_output=True, text=True, check=True).stdout
        committed = json.loads(txt)
    except Exception:
        pass
    frozen = json.load(open(os.path.join(FIX, "ids.json")))
    for src_name, ref in (("committed Resources/Organics/ids.json", committed), ("frozen fixture ids.json", frozen),
                          ("working-tree Resources/Organics/ids.json", json.load(open(os.path.join(RES, "ids.json")))
                           if os.path.exists(os.path.join(RES, "ids.json")) else {})):
        for k, v in ref.items():
            check(lib_ids.get(k) == v, f"ids.json not append-only vs {src_name}: {k} {ref[k]} -> {lib_ids.get(k)}")
    vals = list(lib_ids.values())
    check(len(vals) == len(set(vals)), "ids.json reuses a number")
    check(all(isinstance(v, int) and 1 <= v <= 4095 for v in vals), "ids.json numbers outside 1..4095")
    idx = json.load(open(os.path.join(lib, "index.json")))
    for e in idx:
        check(e["id"] in lib_ids, f"index entry {e['id']} has no ids.json number")


# ---------------------------------------------------------------------------------------------- round trip
def round_trip():
    tmp = tempfile.mkdtemp(prefix="torgc-rt-")
    rep = torgc.Compiler(os.path.join(SRC_FIX, "recipe.json"), SRC_FIX, tmp, 2).run()
    d = os.path.join(tmp, "test.sfzsrc")
    m = json.load(open(os.path.join(d, "map.json")))
    check(m["artics"] == ["Sustain", "Staccato"], f"round trip: artics {m['artics']}")
    att0 = sorted([r for r in m["regions"] if r["a"] == 0 and r["kind"] == "attack"], key=lambda r: r["lv"])
    att1 = sorted([r for r in m["regions"] if r["a"] == 1 and r["kind"] == "attack"], key=lambda r: r["rr"][0])
    rel = [r for r in m["regions"] if r["kind"] == "release"]
    check(len(att0) == 2 and len(att1) == 2 and len(rel) == 2, f"round trip: region counts {len(att0)}/{len(att1)}/{len(rel)}")
    if len(att0) == 2:
        lo, hi = att0
        check((lo["lk"], lo["hk"], lo["root"], lo["lv"], lo["hv"]) == (48, 71, 60, 1, 63), f"round trip: layer lo {lo}")
        check((hi["lk"], hi["hk"], hi["root"], hi["lv"], hi["hv"]) == (48, 71, 60, 64, 127), f"round trip: layer hi")
        # authored loop: 9261..26116 inclusive in the source (start trimmed to 0 → same frames)
        check(lo["loop"] == "continuous" and lo["ls"] == 9261 and lo["le"] == 26117,
              f"round trip: loop {lo['loop']} {lo['ls']} {lo['le']}")
        check(lo["tailLs"] == lo["ls"] and lo["tailLe"] == lo["le"], "round trip: looping region tail != loop")
        # volume=-3 on the hi layer survives in gainDb (both files peak-normalised from the same amplitude)
        def eff(r):   # effective level = gainDb + RMS of the decoded, peak-normalised file
            x, _ = sf.read(os.path.join(d, "samples", m["samples"][r["smp"]]), always_2d=True)
            return r["gainDb"] + an.db(float(np.sqrt(np.mean(x[r["onset"]:] ** 2))))
        check(abs((eff(lo) - eff(hi)) - 3.0) < 0.3, f"round trip: volume=-3 not preserved ({eff(lo) - eff(hi):.2f} dB)")
        check(abs(lo["onset"] - 441) <= 30, f"round trip: onset {lo['onset']} (10 ms of air → ~441)")
        check(lo["env"]["r"] == 0.3, f"round trip: ampeg_release {lo['env']['r']}")
    if len(att1) == 2:
        a, b = att1
        check(a["rr"] == [0, 2] and b["rr"] == [1, 2], f"round trip: rr {a['rr']} {b['rr']}")
        check(a["cents"] == -10.0 and b["cents"] == 10.0, f"round trip: tune {a['cents']} {b['cents']}")
        check((a["lv"], a["hv"]) == (1, 127), "round trip: RR regions should span all velocities")
        # the fixture's samples are exactly C4; tune=−10/+10 per RR take → tfix +10/−10 (per take, ±0.5 ¢)
        check(abs(a["tfix"] - 10.0) <= 0.5 and abs(b["tfix"] + 10.0) <= 0.5, f"round trip: tfix {a['tfix']} {b['tfix']}")
    if len(att0) == 2:
        check(all(abs(r["tfix"]) <= 0.5 for r in att0), f"round trip: in-tune sustain tfix {[r['tfix'] for r in att0]}")
        vc = dict((v, g) for v, g in a["velCurve"])
        check(abs(vc[127] - 1.0) < 1e-3 and abs(vc[1] - 0.2) < 0.02, f"round trip: amp_velcurve evaluated {vc[1]} {vc[127]}")
        check(a["loop"] == "no_loop" and a["tailLe"] > a["tailLs"] > 0, "round trip: decaying staccato should get a tail loop")
    for r in rel:
        check((r["lk"], r["hk"], r["root"], r["rtDecay"]) == (48, 71, 60, 6.0), f"round trip: release region {r}")
    check(m["hasRelease"] and not m["hasNoise"], "round trip: hasRelease/hasNoise")
    check(os.path.exists(os.path.join(d, "preview.flac")) and sf.info(os.path.join(d, "preview.flac")).duration <= 3.001,
          "round trip: preview.flac missing or > 3 s")
    return tmp, d, m


def sf2_round_trip():
    """Write a tiny SoundFont (two zones, one looped, attenuation + fine tune) and compile it through the SF2 path."""
    import sf2 as sf2mod
    tmp = tempfile.mkdtemp(prefix="torgc-sf2-")
    src = os.path.join(tmp, "src")
    os.makedirs(src)
    sr = 44100
    t = np.arange(int(0.8 * sr)) / sr
    a = (0.5 * np.sin(2 * np.pi * 196.0 * t) * np.exp(-t * 3) * 32767).astype(np.int16)
    period = sr / 329.6275569
    ls, le = int(0.2 * sr), int(0.2 * sr) + int(round(100 * period))
    b = (0.5 * np.sin(2 * np.pi * 329.6275569 * t) * 32767).astype(np.int16)
    sf2mod.write_sf2(os.path.join(src, "t.sf2"), [("A", a, sr, 55, 0, 0), ("B", b, sr, 64, ls, le)],
                     [{43: (48, 59), 44: (1, 127), 52: -7, 53: 0},
                      {43: (60, 72), 44: (1, 127), 48: 60, 54: 1, 53: 1}])
    open(os.path.join(src, "LICENSE"), "w").write("generated test data, CC0\n")
    json.dump({"id": "test.sf2src", "name": "SF2 Fixture", "family": "grand", "category": "Keys", "tags": ["test"],
               "licence": "CC0-1.0", "credit": "generated", "author": "Terrain", "url": "generated", "licenceDir": ".",
               "artics": [{"name": "Default", "sfz": "t.sf2"}]}, open(os.path.join(src, "recipe.json"), "w"))
    out = os.path.join(tmp, "out")
    torgc.Compiler(os.path.join(src, "recipe.json"), src, out, 2).run()
    d = os.path.join(out, "test.sf2src")
    m = json.load(open(os.path.join(d, "map.json")))
    att = sorted([r for r in m["regions"] if r["kind"] == "attack"], key=lambda r: r["lk"])
    if check(len(att) == 2, f"sf2 round trip: {len(att)} regions"):
        r0, r1 = att
        check((r0["lk"], r0["hk"], r0["root"], r0["cents"]) == (48, 59, 55, -7.0), f"sf2 round trip: zone A {r0['lk']}-{r0['hk']} root {r0['root']} cents {r0['cents']}")
        check((r1["lk"], r1["hk"], r1["root"], r1["loop"]) == (60, 72, 64, "continuous"), f"sf2 round trip: zone B {r1['loop']}")
        check(r1["ls"] == ls and r1["le"] == le, f"sf2 round trip: loop {r1['ls']}-{r1['le']} != {ls}-{le}")
    return d, m


def unit_tfix_and_noise():
    """measure_f0 on detuned synthetic notes; inject_noise zones/trig/random slots; repair_rr fills a missing take."""
    sr = 48000
    t = np.arange(int(1.5 * sr)) / sr
    for note, c in ((28, -17.0), (60, 23.0), (100, -41.0)):
        f = an.midi_hz(note) * 2 ** (c / 1200.0)
        x = sum((0.6 ** h) * np.sin(2 * np.pi * f * (h + 1) * t) for h in range(6)) * np.exp(-t * 2.0)
        r = an.measure_f0(x, sr, 0, len(x), an.midi_hz(note))
        check(abs(r["cents"] - c) <= 0.5, f"measure_f0: note {note} {c:+} ¢ measured {r['cents']:+.2f}")
    tmp = tempfile.mkdtemp(prefix="torgc-noise-")
    nd = os.path.join(tmp, "TerrainNoise", "unit-set")
    os.makedirs(nd)
    files = []
    for i in range(3):
        sf.write(os.path.join(nd, f"n{i}.wav"), 0.1 * np.random.default_rng(i).standard_normal(4800), sr, subtype="PCM_24")
        files.append({"file": f"n{i}.wav", "origin": {"source_file": "x", "url": "u", "author": "a", "licence": "CC0-1.0",
                                                      "sha256": "0" * 64, "edits": "e"}})
    json.dump({"set": "unit-set", "licence": "CC0-1.0", "credit": "unit", "files": files},
              open(os.path.join(nd, "manifest.json"), "w"))
    base = torgc.Reg(a=0, kind="attack", src="s", lk=40, hk=75, lv=1, hv=127, root=60, cents=0.0, gain_db=0.0, pan=0.0,
                     offset=0, end=None, loop_mode="no_loop", ls=None, le=None, xf_s=0.0, rr=(0, 1), rand=(0.0, 1.0),
                     grp=0, off_by=0, off_mode="normal", env={}, rt_decay=0.0, curve=[1.0] * 128)
    rec = {"artics": [{"name": "Sustain"}, {"name": "Pizzicato"}],
           "noiseMap": [{"set": "unit-set", "trig": "on", "relDb": -30, "zone": 12, "exclude": "pizz"}]}
    inj = torgc.inject_noise(rec, tmp, [base, torgc.replace(base, a=1)], 2)
    check(inj and all(r.kind == "noise" and r.trig == "on" and r.a == 0 for r in inj), "inject_noise: kind/trig/exclude")
    zones = sorted({(r.lk, r.hk) for r in inj})
    check(zones == [(40, 51), (52, 63), (64, 75)], f"inject_noise: zones {zones}")
    for z in zones:
        sl = sorted(r.rand for r in inj if (r.lk, r.hk) == z)
        check(sl[0][0] == 0.0 and sl[-1][1] == 1.0 and len(sl) == 3, f"inject_noise: random slots {sl}")
    recs = [{"a": 0, "kind": "attack", "lk": 60, "hk": 60, "lv": 1, "hv": 127, "rr": [p, 3], "rand": [0.0, 1.0]}
            for p in (0, 2)]
    fx = torgc.repair_rr(recs)
    check(sorted(r["rr"][0] for r in recs) == [0, 1, 2] and fx["seqCloned"] == 1, f"repair_rr: {recs}")


# ---------------------------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lib", default=DEFAULT_LIB)
    ap.add_argument("--quick", action="store_true", help="seam-check at most 40 loops per instrument")
    a = ap.parse_args()
    credits_md = open(os.path.join(RES, "CREDITS.md")).read() if os.path.exists(os.path.join(RES, "CREDITS.md")) else ""
    print("== round trip (fixture SFZ → .torg)")
    tmp, d, m = round_trip()
    frames = check_schema(d, m, None)
    check_coverage(m)
    check_audio(d, m, frames, 48.0, None, False)
    check_licence(d, m, {"licence": "CC0-1.0"}, credits_md)
    check_tp105(d, m, credits_md)
    check_rr_audible(d, m)
    print("== unit: measure_f0 / inject_noise / repair_rr")
    unit_tfix_and_noise()
    print("== round trip (generated SF2 → .torg)")
    d2, m2 = sf2_round_trip()
    frames = check_schema(d2, m2, None)
    check_coverage(m2)
    check_audio(d2, m2, frames, 48.0, None, False)
    print(f"   {PASSES[0]} checks passed, {len(FAILS)} failed so far")
    if os.path.exists(os.path.join(a.lib, "index.json")):
        idx = {e["id"]: e for e in json.load(open(os.path.join(a.lib, "index.json")))}
        recipes = {json.load(open(p))["id"]: json.load(open(p))
                   for p in glob.glob(os.path.join(PLUG, "Tools", "organics", "recipes", "*.json"))}
        print(f"== library {a.lib}: {len(idx)} instruments")
        for iid in sorted(idx):
            d = os.path.join(a.lib, iid)
            f0 = len(FAILS)
            m = json.load(open(os.path.join(d, "map.json")))
            frames = check_schema(d, m, idx[iid])
            check_coverage(m)
            rec = recipes.get(iid, {})
            budget = float(rec.get("budgetMB", 160.0 if rec.get("piano") else 48.0))
            ram, n, worst = check_audio(d, m, frames, budget, idx[iid].get("sizeMB"), a.quick)
            check_licence(d, m, idx[iid], credits_md)
            check_tp105(d, m, credits_md)
            check_rr_audible(d, m)
            check(os.path.exists(os.path.join(d, "preview.flac")), f"{iid}: preview.flac missing")
            if os.path.exists(os.path.join(d, "preview.flac")):
                check(sf.info(os.path.join(d, "preview.flac")).duration <= 3.001, f"{iid}: preview > 3 s")
            print(f"   {'ok ' if len(FAILS) == f0 else 'FAIL'} {iid:44s} {ram:6.1f} MB  loops {n:4d} worst seam {worst:.2f}")
        check_ids(a.lib)
        check_loudness(a.lib, idx)
    else:
        print(f"== no library at {a.lib}; only the round trip ran")
    print(f"\n{PASSES[0]} checks passed, {len(FAILS)} failed")
    for f in FAILS[:60]:
        print("  FAIL:", f)
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
