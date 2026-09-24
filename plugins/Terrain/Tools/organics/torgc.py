#!/usr/bin/env python3
"""torgc.py — the Organics compiler: SFZ / SF2 → .torg v1 (design §4.1, contract §3).

    python3 torgc.py recipes/salamander.grand.v3.json [more.json …] [--out DIR] [--raw DIR] [--jobs N]
    python3 torgc.py --all                     # every recipe in recipes/
    python3 torgc.py --index-only              # rebuild index.json from the compiled folders

A recipe (recipes/*.json) names the source mapping(s), the articulations, the licence data and the
authoring budget. Everything else is measured from the audio.

Output per instrument (<out>/<id>/):
    map.json            the flattened region table, field-for-field the frozen fixture's shape
    samples/NNNN.flac   16-bit (24-bit only for a piano whose source is 24-bit), trimmed, peak-normalised
    preview.flac        middle C, velocity 90, ≤ 3 s
    source/             the original mapping file(s), LICENCE.txt, provenance.csv
    build-report.json   budget report, loop/onset stats and the listen-by-numbers QA
and at <out>/: index.json, ids.json (append-only).

Level model (what the runtime multiplies):  out = sample · 10^(gainDb/20) · gainNorm · velCurve(vel)
  gainDb    authored SFZ level (volume, group_volume, amplitude, evaluated CC defaults)
            + the per-file peak-normalisation compensation + one per-instrument calibration offset
            (vel 100 near middle C ≈ −18 dBFS RMS), so instruments sit at the same loudness.
  gainNorm  RMS normalisation across velocity layers / round robins of the same note, with 50 % of the
            natural loudness difference restored (design §2.3 Dynamics): gainNorm = (top/rms)^0.5.
  velCurve  the authored amp_velcurve/amp_veltrack curve × a smooth power-law fit of the removed 50 %,
            so the played loudness still matches the recording, but a Dynamics shift reads as timbre.

Region semantics the runtime can rely on (documented for agent B):
  kind      "attack" (note-on), "release" (note-off: trigger=release, rt_decay applies),
            "noise"  (mechanical noise; every noise region compiled here is NOTE-OFF triggered — key-off
                     clunks, damper noise, finger lifts — and follows the Noise knob instead of Release)
  lv..hv    velocity band; attack bands are contiguous and non-overlapping per key/RR slot (stacked
            regions — e.g. two mics — share the same band). xfLo/xfHi = lv/hv (no authored crossfade
            band; the runtime's Dynamics crossfade does the blending).
  rr        [position 0-based, length] (seq_position/seq_length); rand [lo, hi) (lorand/hirand)
  start/end sample frames (end exclusive); onset absolute frame; ls/le absolute, le exclusive
  loop      "no_loop" | "one_shot" | "continuous" | "sustain"; xf = crossfade frames before le, blended
            with the frames before ls (equal-power). For decaying regions (no_loop/one_shot) ls=le=0
            and xf belongs to the tail loop tailLs/tailLe. Looping regions: tailLs/tailLe = ls/le.
  pan       −100..100 (SFZ units). cents includes tune + transpose·100. env seconds, s 0..1.
  rtDecay   dB per second held (SFZ rt_decay).
"""
from __future__ import annotations

import argparse
import csv
import glob
import hashlib
import json
import math
import os
import re
import shutil
import sys
import time
from collections import defaultdict, OrderedDict
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, field, replace
from typing import Dict, List, Optional, Tuple

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sfz as sfzmod          # noqa: E402
import sf2 as sf2mod          # noqa: E402
import analyse as an          # noqa: E402

DEFAULT_RAW = os.path.expanduser("~/Developer/VST-Plugins/organics-library/raw")
DEFAULT_OUT = os.path.expanduser("~/Developer/VST-Plugins/organics-library/compiled")
REPO_ORGANICS = os.path.normpath(os.path.join(HERE, "..", "..", "Resources", "Organics"))

CATEGORIES = ["Keys", "Organs", "Strings", "Plucked", "Winds", "Brass", "Mallets & Bells", "Choir & Voice", "Percussion"]
FAMILIES = ["grand", "upright", "rhodes", "clav", "harpsi", "organ", "tonewheel", "violin", "cello", "bass", "guitar",
            "harp", "koto", "flute", "clarinet", "sax", "trumpet", "horn", "choir", "glock", "marimba", "kalimba", "musicbox"]
CAT_DEFAULT_FAM = {"Keys": "grand", "Organs": "organ", "Strings": "violin", "Plucked": "guitar", "Winds": "flute",
                   "Brass": "trumpet", "Mallets & Bells": "glock", "Choir & Voice": "choir", "Percussion": "marimba"}
BUDGET_TYPICAL_MB = 48.0
BUDGET_PIANO_MB = 160.0
CALIB_RMS_DB = -18.0
PEAK_TARGET = 10 ** (-0.3 / 20)
TODAY = "2026-09-24"

# budget tightening steps, applied in order until the instrument fits
BUDGET_STEPS = [("cap", 12.0), ("sus", 6.0), ("rr", 4), ("cap", 9.0), ("rr", 3), ("sus", 5.0), ("cap", 7.0),
                ("layers", 6), ("sus", 4.2), ("cap", 5.5), ("rr", 2), ("layers", 5), ("sus", 3.6), ("cap", 4.5),
                ("layers", 4), ("cap", 3.5), ("layers", 3), ("rr", 1), ("sus", 3.0), ("cap", 2.5), ("layers", 2),
                ("cap", 1.8)]


def log(*a):
    print(*a, flush=True)


# ============================================================================================ regions
@dataclass
class Reg:
    a: int
    kind: str
    src: str
    lk: int
    hk: int
    lv: int
    hv: int
    root: int
    cents: float
    gain_db: float
    pan: float
    offset: int
    end: Optional[int]
    loop_mode: str
    ls: Optional[int]
    le: Optional[int]
    xf_s: float
    rr: Tuple[int, int]
    rand: Tuple[float, float]
    grp: int
    off_by: int
    off_mode: str
    env: Dict[str, float]
    rt_decay: float
    curve: List[float]                   # 128 authored velocity→amplitude values
    fa: float = 0.0                      # full-on interval on the layer axis (vel or dynamics CC)
    fb: float = 127.0
    sustaining: bool = False
    tag: str = ""


def _f(r: dict, k: str, default: float) -> float:
    v = r.get(k)
    if v is None or v == "":
        return default
    try:
        return float(v)
    except ValueError:
        return default


class Interpreter:
    """Raw SFZ region dicts → Reg objects, evaluating CC defaults, curves and the opcode subset (§4.2)."""

    def __init__(self, sf_: sfzmod.SfzFile, recipe: dict, artic: dict):
        self.sf = sf_
        self.recipe = recipe
        self.artic = artic
        self.cc_over = {int(k): float(v) for k, v in {**recipe.get("cc", {}), **artic.get("cc", {})}.items()}
        self.dyn_cc = artic.get("dynCC", recipe.get("dynCC"))
        self.drops = defaultdict(int)

    def cc(self, n: int) -> float:
        if n == 133:                      # ARIA extended CC: note number (evaluated at the region root)
            return float(getattr(self, "_ctx_root", 60))
        if n == 131:                      # ARIA extended CC: note-on velocity (evaluated at 100)
            return 100.0
        if n in self.cc_over:
            return self.cc_over[n]
        return self.sf.cc_value(n)

    def _curve(self, r: dict, base: str, n: int) -> float:
        """Controller n at its default value through {base}_curveccN (SFZ predefined curves 0-6, or a <curve>)."""
        x = max(0.0, min(1.0, self.cc(n) / 127.0))
        c = r.get(f"{base}_curvecc{n}")
        if c is None:
            return x
        try:
            ci = int(float(c))
        except ValueError:
            return x
        if ci in self.sf.curves:
            return self.sf.curves[ci][int(round(x * 127))]
        return {0: x, 1: 2 * x - 1, 2: 1 - x, 3: 1 - 2 * x, 4: x * x, 5: math.sqrt(x),
                6: math.sqrt(1 - x)}.get(ci, x)

    def _oncc_sum(self, r: dict, base: str) -> float:
        """Σ value·curve(cc) over base_onccN / baseccN (ARIA and SFZ v1 spellings), at the default CC state."""
        tot = 0.0
        for k, v in r.items():
            m = re.match(r"^" + re.escape(base) + r"(?:_on)?cc(\d+)$", k)
            if m:
                n = int(m.group(1))
                try:
                    tot += float(v) * self._curve(r, base, n)
                except ValueError:
                    pass
        return tot

    def _cc_xfade_gain(self, r: dict) -> float:
        """Evaluate xfin/xfout_lo/hiccN at the default CCs (power curve), except the dynamics CC."""
        g = 1.0
        ccs = set()
        for k in r:
            m = re.match(r"^xf(?:in|out)_(?:lo|hi)cc(\d+)$", k)
            if m:
                ccs.add(int(m.group(1)))
        for n in ccs:
            if self.dyn_cc is not None and n == int(self.dyn_cc):
                continue
            v = self.cc(n)
            if f"xfin_locc{n}" in r or f"xfin_hicc{n}" in r:
                lo, hi = _f(r, f"xfin_locc{n}", 0), _f(r, f"xfin_hicc{n}", 0)
                if v <= lo:
                    g *= 0.0
                elif v < hi:
                    g *= math.sqrt((v - lo) / (hi - lo))
            if f"xfout_locc{n}" in r or f"xfout_hicc{n}" in r:
                lo, hi = _f(r, f"xfout_locc{n}", 127), _f(r, f"xfout_hicc{n}", 127)
                if v >= hi:
                    g *= 0.0
                elif v > lo:
                    g *= math.sqrt((hi - v) / (hi - lo))
        return g

    def interpret(self, r: dict, a_idx: int) -> List[Reg]:
        d = self.drops
        smp = r.get("_sample")
        if not smp or smp.startswith("*"):
            d["no_sample_or_generator"] += 1
            return []
        if not os.path.exists(smp):
            d["missing_file"] += 1
            return []
        for pat in self.recipe.get("dropMatch", []) + self.artic.get("dropMatch", []):
            if re.search(pat, smp) or re.search(pat, r.get("group_label", "") + "|" + r.get("master_label", "")):
                d["recipe_drop"] += 1
                return []
        # CC-triggered regions (pedal up/down noises …) cannot be expressed in .torg v1
        if any(k.startswith("on_locc") or k.startswith("on_hicc") for k in r):
            d["cc_triggered"] += 1
            return []
        # CC conditions at the default controller state
        for k, v in r.items():
            m = re.match(r"^(lo|hi)cc(\d+)$", k)
            if m:
                n = int(m.group(2))
                if self.dyn_cc is not None and n == int(self.dyn_cc):
                    continue
                val = self.cc(n)
                lim = _f(r, k, 0 if m.group(1) == "lo" else 127)
                if (m.group(1) == "lo" and val < lim) or (m.group(1) == "hi" and val > lim):
                    d["cc_condition"] += 1
                    return []
        for k in ("lochan", "hichan"):
            if k in r and k == "lochan" and _f(r, k, 1) > 1:
                d["channel"] += 1
                return []
        if "sw_previous" in r or "sw_down" in r or "sw_up" in r:
            d["sw_state"] += 1
            return []
        if _f(r, "lobend", -8192) > 0 or _f(r, "hibend", 8192) < 0:
            d["bend_condition"] += 1
            return []
        trig = r.get("trigger", "attack").strip().lower()
        if trig == "legato":
            d["legato"] += 1
            return []
        if trig == "first":
            trig = "attack"
        if trig not in ("attack", "release", "release_key"):
            d["trigger_" + trig] += 1
            return []
        kind = "attack" if trig == "attack" else "release"
        tag_texts = (smp, r.get("group_label", ""), r.get("master_label", ""), r.get("region_label", ""))
        for pat in self.recipe.get("noise", []) + self.artic.get("noise", []):
            if any(re.search(pat, t) for t in tag_texts):
                if kind == "attack" and not self.recipe.get("attackNoise", False):
                    d["attack_noise"] += 1
                    return []
                kind = "noise"
        if kind == "release" and not self.recipe.get("releases", True):
            d["release_disabled"] += 1
            return []
        if kind == "attack" and self.artic.get("releaseOnly"):
            return []

        no = int(r.get("_note_offset", "0"))
        oo = int(r.get("_octave_offset", "0"))
        N = lambda k: sfzmod.note_to_midi(r.get(k), no, oo) if r.get(k) not in (None, "") else None  # noqa
        key = N("key")
        lk = N("lokey")
        hk = N("hikey")
        if key is not None:
            lk = key if lk is None else lk
            hk = key if hk is None else hk
        lk = 0 if lk is None else lk
        hk = 127 if hk is None else hk
        if lk < 0 or hk < 0 or hk < lk:
            d["no_keys"] += 1
            return []
        lk, hk = max(0, lk), min(127, hk)
        pkc = r.get("pitch_keycenter")
        unity, smpl_loops = an.read_smpl_loops(smp)
        if pkc is not None and pkc.strip().lower() == "sample":
            root = unity if unity is not None else (key if key is not None else 60)
        elif pkc is not None:
            root = sfzmod.note_to_midi(pkc, no, oo)
            if root is None:
                root = 60
        else:
            root = key if key is not None else 60
        self._ctx_root = int(root)
        lv = int(_f(r, "lovel", 0))
        hv = int(_f(r, "hivel", 127))
        fa, fb = float(lv), float(hv)
        if "xfin_lovel" in r or "xfin_hivel" in r:
            fa = _f(r, "xfin_hivel", fa)
            lv = int(_f(r, "xfin_lovel", lv))
        if "xfout_lovel" in r or "xfout_hivel" in r:
            fb = _f(r, "xfout_lovel", fb)
            hv = int(_f(r, "xfout_hivel", hv))
        if self.dyn_cc is not None:
            n = int(self.dyn_cc)
            keys = [f"xfin_locc{n}", f"xfin_hicc{n}", f"xfout_locc{n}", f"xfout_hicc{n}", f"locc{n}", f"hicc{n}"]
            if any(k in r for k in keys):
                c_lo, c_hi = _f(r, f"locc{n}", 0), _f(r, f"hicc{n}", 127)
                fa = _f(r, f"xfin_hicc{n}", c_lo) if (f"xfin_hicc{n}" in r or f"xfin_locc{n}" in r) else c_lo
                fb = _f(r, f"xfout_locc{n}", c_hi) if (f"xfout_locc{n}" in r or f"xfout_hicc{n}" in r) else c_hi
                lv, hv = 0, 127
        tune = _f(r, "tune", 0) + _f(r, "pitch", 0) + self._oncc_sum(r, "tune") + self._oncc_sum(r, "pitch")
        cents = tune + 100.0 * _f(r, "transpose", 0)
        gain = (_f(r, "volume", 0) + _f(r, "group_volume", 0) + _f(r, "master_volume", 0) + _f(r, "global_volume", 0)
                + self._oncc_sum(r, "gain") + self._oncc_sum(r, "volume"))
        amp = _f(r, "amplitude", 100.0) / 100.0
        for k, v in r.items():
            m = re.match(r"^amplitude_(?:on)?cc(\d+)$", k)
            if m:
                amp *= _f(r, k, 100.0) / 100.0 * max(0.0, self._curve(r, "amplitude", int(m.group(1))))
        amp *= self._cc_xfade_gain(r)
        if amp <= 1e-6:
            d["silent_at_default_cc"] += 1
            return []
        gain += 20 * math.log10(amp)
        pan = _f(r, "pan", 0) + self._oncc_sum(r, "pan")
        pan = max(-100.0, min(100.0, pan))
        offset = int(_f(r, "offset", 0) + self._oncc_sum(r, "offset"))
        end = r.get("end")
        end_i = None
        if end not in (None, ""):
            e = int(_f(r, "end", 0))
            if e <= 0:
                d["end_zero"] += 1
                return []
            end_i = e + 1
        lm = (r.get("loop_mode") or r.get("loopmode") or "").strip().lower()
        ls = r.get("loop_start", r.get("loopstart"))
        le = r.get("loop_end", r.get("loopend"))
        ls_i = int(float(ls)) if ls not in (None, "") else None
        le_i = int(float(le)) + 1 if le not in (None, "") else None
        if (ls_i is None or le_i is None) and smpl_loops:
            ls_i, le_i = smpl_loops[0]
        if lm == "":
            lm = "loop_continuous" if (ls_i is not None and le_i is not None and le_i > ls_i) else "no_loop"
        lm = {"loop_continuous": "continuous", "loop_sustain": "sustain", "one_shot": "one_shot",
              "no_loop": "no_loop"}.get(lm, "no_loop")
        if lm in ("continuous", "sustain") and not (ls_i is not None and le_i is not None and le_i > ls_i):
            lm = "no_loop"
            ls_i = le_i = None
        if lm in ("no_loop", "one_shot"):
            ls_i = le_i = None
        xf_s = _f(r, "loop_crossfade", 0.0)
        seq_len = int(_f(r, "seq_length", 1))
        seq_pos = int(_f(r, "seq_position", 1))
        rr = (max(0, seq_pos - 1), max(1, seq_len)) if seq_len > 1 else (0, 1)
        rand = (max(0.0, _f(r, "lorand", 0.0)), min(1.0, _f(r, "hirand", 1.0)))
        if rand[1] <= rand[0]:
            d["empty_rand"] += 1
            return []
        env = {
            "a": round(max(0.0, _f(r, "ampeg_attack", 0.0) + self._oncc_sum(r, "ampeg_attack")), 4),
            "h": round(max(0.0, _f(r, "ampeg_hold", 0.0) + self._oncc_sum(r, "ampeg_hold")), 4),
            "d": round(max(0.0, _f(r, "ampeg_decay", 0.0) + self._oncc_sum(r, "ampeg_decay")), 4),
            "s": round(max(0.0, min(1.0, (_f(r, "ampeg_sustain", 100.0) + self._oncc_sum(r, "ampeg_sustain")) / 100.0)), 4),
            "r": round(max(0.001, _f(r, "ampeg_release", 0.001) + self._oncc_sum(r, "ampeg_release")), 4),
        }
        if self.recipe.get("envRelease") is not None and kind == "attack":
            env["r"] = float(self.recipe["envRelease"])
        curve = self._vel_curve(r)
        off_mode = (r.get("off_mode") or "normal").strip().lower()
        off_mode = "fast" if off_mode == "fast" else "normal"
        sustaining = bool(self.artic.get("sustaining", self.recipe.get("sustaining", False)))
        base = Reg(a=a_idx, kind=kind, src=smp, lk=lk, hk=hk, lv=max(1, lv), hv=min(127, hv), root=int(root),
                   cents=round(cents, 2), gain_db=gain, pan=round(pan, 1), offset=max(0, offset), end=end_i,
                   loop_mode=lm, ls=ls_i, le=le_i, xf_s=xf_s, rr=rr, rand=(round(rand[0], 4), round(rand[1], 4)),
                   grp=int(_f(r, "group", 0)), off_by=int(_f(r, "off_by", 0)), off_mode=off_mode, env=env,
                   rt_decay=_f(r, "rt_decay", 0.0), curve=curve, fa=fa, fb=fb, sustaining=sustaining,
                   tag=r.get("group_label", ""))
        # pitch_keytrack ≠ 100: expand per key (noise/percussive regions)
        kt = _f(r, "pitch_keytrack", 100.0)
        if abs(kt - 100.0) > 1e-6:
            if hk - lk > 48:
                d["keytrack_wide"] += 1
                return []
            out = []
            for k in range(lk, hk + 1):
                c = cents + (k - root) * (kt - 100.0)
                out.append(replace(base, lk=k, hk=k, root=k, cents=round(c, 2)))
            return out
        return [base]

    def _vel_curve(self, r: dict) -> List[float]:
        pts = {}
        for k, v in r.items():
            m = re.match(r"^amp_velcurve_(\d+)$", k)
            if m:
                try:
                    pts[int(m.group(1))] = float(v)
                except ValueError:
                    pass
        if pts:
            curve = sfzmod._interp_points(pts)
        else:
            curve = [(v / 127.0) ** 2 for v in range(128)]
        vt = (_f(r, "amp_veltrack", 100.0) + self._oncc_sum(r, "amp_veltrack")) / 100.0
        vt = max(-1.0, min(1.0, vt))
        if vt >= 0:
            return [1.0 - vt + vt * c for c in curve]
        return [1.0 + vt * c for c in curve]


# ============================================================================================ mapping ops
def layerize(regs: List[Reg]) -> List[Reg]:
    """Attack regions: contiguous, non-overlapping velocity bands per (artic, key zone, RR slot)."""
    groups = defaultdict(list)
    for r in regs:
        if r.kind != "attack":
            continue
        groups[(r.a, r.lk, r.hk, r.rr, r.rand)].append(r)
    out = [r for r in regs if r.kind != "attack"]
    for g in groups.values():
        layers = defaultdict(list)
        for r in g:
            layers[(r.fa, r.fb)].append(r)
        keys = sorted(layers, key=lambda t: (t[0] + t[1]) / 2)
        if len(keys) == 1:
            out.extend(layers[keys[0]])
            continue
        bounds = []
        for i in range(len(keys) - 1):
            b = int(math.floor((keys[i][1] + keys[i + 1][0]) / 2.0))
            b = max(b, (bounds[-1] + 1) if bounds else 1)
            bounds.append(min(b, 126))
        for i, k in enumerate(keys):
            lv = 1 if i == 0 else bounds[i - 1] + 1
            hv = 127 if i == len(keys) - 1 else bounds[i]
            if hv < lv:
                continue
            for r in layers[k]:
                out.append(replace(r, lv=lv, hv=hv))
    return out


def reduce_layers(regs: List[Reg], max_layers: int) -> List[Reg]:
    groups = defaultdict(list)
    for r in regs:
        groups[(r.a, r.kind, r.lk, r.hk, r.rr, r.rand)].append(r)
    out = []
    for key, g in groups.items():
        bands = sorted({(r.lv, r.hv) for r in g})
        if key[1] != "attack" or len(bands) <= max_layers:
            out.extend(g)
            continue
        keep_idx = sorted(set(np.round(np.linspace(0, len(bands) - 1, max_layers)).astype(int).tolist()))
        kept = [bands[i] for i in keep_idx]
        newband = {}
        for j, b in enumerate(kept):
            lv = 1 if j == 0 else b[0]
            hv = 127 if j == len(kept) - 1 else kept[j + 1][0] - 1
            newband[b] = (lv, hv)
        for r in g:
            if (r.lv, r.hv) in newband:
                lv, hv = newband[(r.lv, r.hv)]
                out.append(replace(r, lv=lv, hv=hv))
    return out


def reduce_rr(regs: List[Reg], max_rr: int) -> List[Reg]:
    out = []
    groups = defaultdict(list)
    for r in regs:
        groups[(r.a, r.kind, r.lk, r.hk, r.lv, r.hv)].append(r)
    for g in groups.values():
        # sequential
        seq = [r for r in g if r.rr[1] > max_rr]
        rest = [r for r in g if r.rr[1] <= max_rr]
        for r in seq:
            if r.rr[0] < max_rr:
                rest.append(replace(r, rr=(r.rr[0], max_rr)))
        # random
        slots = sorted({r.rand for r in rest})
        if len(slots) > max_rr and all(s != (0.0, 1.0) for s in slots):
            keep = slots[:max_rr]
            remap = {s: (round(i / len(keep), 4), round((i + 1) / len(keep), 4)) for i, s in enumerate(keep)}
            rest = [replace(r, rand=remap[r.rand]) for r in rest if r.rand in remap]
        out.extend(rest)
    return out


def fill_holes(regs: List[Reg], key_range: Optional[Tuple[int, int]] = None) -> Tuple[List[Reg], int]:
    """Close key holes per (artic, band, RR slot) by stretching the adjacent zone (upper preferred:
    a zone is pitched DOWN more than up, §5.3). Returns (regions, holes filled)."""
    filled = 0
    by_a = defaultdict(list)
    for r in regs:
        if r.kind == "attack":
            by_a[r.a].append(r)
    out = [r for r in regs if r.kind != "attack"]
    for a, rs in by_a.items():
        kmin = min(r.lk for r in rs) if key_range is None else key_range[0]
        kmax = max(r.hk for r in rs) if key_range is None else key_range[1]
        # velocity bands differ per zone; check each (band, rr, rand) against the key axis at each vel
        slots = defaultdict(list)
        for r in rs:
            slots[(r.rr, r.rand)].append(r)
        for slot, sr in slots.items():
            for v in range(1, 128):
                cov = [r for r in sr if r.lv <= v <= r.hv]
                if not cov:
                    continue
                covered = np.zeros(128, bool)
                for r in cov:
                    covered[r.lk:r.hk + 1] = True
                for k in range(kmin, kmax + 1):
                    if covered[k]:
                        continue
                    above = [r for r in cov if r.lk > k]
                    below = [r for r in cov if r.hk < k]
                    cand = None
                    if above:
                        ra = min(above, key=lambda r: r.lk)
                        cand = ra
                    if below:
                        rb = max(below, key=lambda r: r.hk)
                        if cand is None or (k - rb.hk) < (cand.lk - k):
                            cand = rb
                    if cand is None:
                        continue
                    olk, ohk = getattr(cand, "_olk", cand.lk), getattr(cand, "_ohk", cand.hk)
                    if (k < olk and olk - k > 7) or (k > ohk and k - ohk > 7):
                        continue          # never stretch a zone more than 7 semitones past its authored edge
                    for r in sr:
                        if r is cand or (r.lk == cand.lk and r.hk == cand.hk and r.lv == cand.lv and r.hv == cand.hv
                                         and r.src == cand.src):
                            if not hasattr(r, "_olk"):
                                r._olk, r._ohk = r.lk, r.hk
                            if k < r.lk:
                                r.lk = k
                            elif k > r.hk:
                                r.hk = k
                    for r in cov:
                        covered[r.lk:r.hk + 1] = True
                    filled += 1
        out.extend(rs)
    return out, filled


def fill_vel_holes(regs: List[Reg]) -> List[Reg]:
    groups = defaultdict(list)
    for r in regs:
        if r.kind == "attack":
            groups[(r.a, r.lk, r.hk, r.rr, r.rand)].append(r)
    for g in groups.values():
        bands = sorted({(r.lv, r.hv) for r in g})
        fix = {}
        for i, (lv, hv) in enumerate(bands):
            nlv = 1 if i == 0 else lv
            nhv = 127 if i == len(bands) - 1 else max(hv, bands[i + 1][0] - 1)
            fix[(lv, hv)] = (nlv, nhv)
        for r in g:
            r.lv, r.hv = fix[(r.lv, r.hv)]
    return regs


def effective_cap(recipe: dict, rs: List["Reg"], cap: Optional[float]) -> Optional[float]:
    """Tail cap (s) for one source file: attack tails scale with register when capScale is on (bass notes ring
    longer), release/noise samples use releaseCap."""
    if cap is None:
        return None
    if all(r.kind != "attack" for r in rs):
        return float(recipe.get("releaseCap", max(cap, 3.0)))
    if recipe.get("capScale"):
        root = min(r.root for r in rs)
        return cap * max(0.5, min(2.0, 2.0 ** ((60 - root) / 30.0)))
    return cap


# ============================================================================================ audio jobs
def job_analyse(path: str) -> dict:
    info = sf.info(path)
    x, sr = sf.read(path, dtype="float32", always_2d=True)
    mono = an.to_mono(x)
    pk = float(np.abs(x).max()) if len(x) else 0.0
    end60 = an.trim_end(mono, sr, 0, -60.0)
    with open(path, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    return {"path": path, "sr": sr, "ch": x.shape[1], "frames": len(x), "peak": pk, "end60": end60,
            "subtype": info.subtype, "sha256": sha, "clip": int(np.sum(np.abs(x) >= 0.999))}


def _tpdf(n_shape, bits):
    lsb = 1.0 / (2 ** (bits - 1))
    return (np.random.default_rng(12345).random(n_shape) - np.random.default_rng(54321).random(n_shape)) * lsb


def job_render_sample(job: dict) -> dict:
    """Trim, loop-search, fade, normalise and write one output FLAC; measure onsets and RMS per region start."""
    x, sr = sf.read(job["src"], dtype="float64", always_2d=True)
    mono = an.to_mono(x)
    start0 = job["start0"]
    n = len(x)
    end = an.trim_end(mono, sr, start0, -60.0)
    natural_end = end
    if job.get("end_opcode"):
        end = min(end, job["end_opcode"])
    cap = job.get("cap_s")
    mode = job["mode"]
    if mode == "sustain_find":
        end = min(end, start0 + int(job.get("sus_len_s", 7.0) * sr))
    elif cap and mode in ("decay",):
        end = min(end, start0 + int(cap * sr))
    end = max(end, min(n, start0 + int(0.05 * sr)))
    res = {"src": job["src"], "out": job["out"], "sr": sr, "ch": x.shape[1]}
    loop = None
    tail = None
    first_start = min(job["starts"]) if job["starts"] else start0
    onset0 = an.find_onset(mono, first_start, end)
    if mode == "src_loop":
        ls, le = job["ls"], job["le"]
        le = min(le, n)
        xf = int(job.get("xf_s", 0.0) * sr)
        xf = min(xf, ls, le - ls)
        m = an.seam_metric(x, ls, le, xf)
        if m["ratio"] > 1.45:
            for xf_try in (int(0.005 * sr), int(0.015 * sr), int(0.04 * sr)):
                xf_try = min(xf_try, ls, (le - ls) // 3)
                m2 = an.seam_metric(x, ls, le, xf_try)
                if m2["ratio"] < m["ratio"]:
                    xf, m = xf_try, m2
                if m["ratio"] <= 1.45:
                    break
        loop = (ls, le, xf, m)
        if m["ratio"] > 1.45:
            found = an.find_sustain_loop(x, sr, first_start, min(n, max(le, end)), onset0)
            if found:
                loop = found
                res["loopReplaced"] = True
        end = max(loop[1], min(n, loop[1] + int(0.05 * sr)))
    elif mode == "sustain_find":
        found = an.find_sustain_loop(x, sr, first_start, end, onset0)
        if found:
            ls, le, xf, info = found
            loop = (ls, le, xf, info)
            end = min(n, le + int(0.05 * sr))
        else:
            tail = an.find_tail_loop(x, sr, first_start, end)
    elif job.get("want_tail", True):
        tail = an.find_tail_loop(x, sr, first_start, end)
    # short decaying recordings that stop while still loud: continue them with the tail loop (Wurli, EPs …)
    res["extendedS"] = 0.0
    if tail and job.get("extend_s") and not loop and end >= natural_end - int(0.02 * sr):
        pk_r = float(np.abs(mono[first_start:end]).max())
        lvl_end = an.db(an._rms(mono[max(first_start, end - int(0.1 * sr)):end])) - an.db(pk_r)
        if lvl_end > -50.0:
            tls, tle, txf, _ = tail
            rate = min(60.0, max(4.0, an.decay_rate_db_s(mono, sr, tls, end)))
            ext_s = min(float(job["extend_s"]), (60.0 + lvl_end) / rate)
            ext = int(ext_s * sr)
            if ext > int(0.3 * sr):
                x = an.extend_decay(x[:tle], sr, tls, tle, txf, rate, ext + (end - tle))
                mono = an.to_mono(x)
                end_cap = len(x)
                if cap:
                    end_cap = max(tle + int(0.25 * sr), min(len(x), start0 + int(cap * sr)))
                x = x[:end_cap]
                res["extendedS"] = round((len(x) - end) / sr, 2)
                end = len(x)
                natural_end = end
                n = len(x)
    # end fade (never inside a loop)
    protect = loop[1] if loop else (tail[1] if tail else first_start)
    fade = int(0.03 * sr) if loop else int(0.2 * sr)
    fade = min(fade, end - protect)            # never fade inside a loop / tail loop (0 when the loop ends the file)
    data = x[start0:end].copy()
    if fade > 0 and len(data) > fade:
        data[-fade:] *= np.cos(np.linspace(0, math.pi / 2, fade))[:, None] ** 1
    pk = float(np.abs(data).max()) if len(data) else 0.0
    comp_db = 20 * math.log10(pk / PEAK_TARGET) if pk > 1e-9 else 0.0
    scale = PEAK_TARGET / pk if pk > 1e-9 else 1.0
    y = data * scale
    bits = job["bits"]
    y = y + _tpdf(y.shape, bits)
    y = np.clip(y, -1.0, 1.0 - 1.0 / (2 ** (bits - 1)))
    y = np.round(y * (2 ** (bits - 1))) / (2 ** (bits - 1))     # exactly the values the FLAC will decode to
    # re-verify every seam on the QUANTISED data (dither can tip a marginal quiet tail over the gate)
    def _verify(lp):
        ls_, le_, xf_, info_ = lp
        a_, b_ = ls_ - start0, le_ - start0
        for xf_try in (xf_, int(xf_ * 1.6), max(1, xf_ // 2)):
            xf_try = int(max(1, min(xf_try, a_, 0.45 * (b_ - a_)))) if xf_ > 0 else 0
            mm = an.seam_metric(y, a_, b_, xf_try)
            if mm["ratio"] <= 1.45:
                return (ls_, le_, xf_try, dict(info_, **mm))
        return None
    if loop:
        loop = _verify(loop) or loop
    if tail:
        tail = _verify(tail)
    sf.write(job["out"], y.astype(np.float64), sr, subtype="PCM_24" if bits == 24 else "PCM_16", format="FLAC")
    res["frames"] = len(y)
    res["comp_db"] = comp_db
    res["cut"] = end < natural_end
    if loop:
        ls, le, xf, info = loop
        res["loop"] = [ls - start0, le - start0, int(xf), {k: float(v) for k, v in info.items()}]
    if tail:
        ls, le, xf, info = tail
        res["tail"] = [ls - start0, le - start0, int(xf), {k: float(v) for k, v in info.items()}]
    per = {}
    for s in job["starts"]:
        ons = an.find_onset(mono, s, end)
        seg = mono[ons: ons + int(0.5 * sr)]
        rms = float(np.sqrt(np.mean(seg ** 2))) if len(seg) else 0.0
        per[str(s)] = {"onset": ons - start0, "rms": rms}
    res["per_start"] = per
    # noise floor: quietest 50 ms window of the kept audio, relative to its peak
    env = an.envelope_db(mono, sr, 0.05)
    pk_src = float(np.abs(mono).max()) if len(mono) else 0.0
    res["floor_rel_db"] = float(env.min() - 20 * math.log10(max(pk_src, 1e-9))) if len(env) else 0.0
    res["clip"] = int(np.sum(np.abs(x[start0:end]) >= 0.999))
    return res


# ============================================================================================ compiler
class Compiler:
    def __init__(self, recipe_path: str, raw: str, out: str, jobs: int):
        with open(recipe_path) as f:
            self.recipe = json.load(f)
        self.recipe_path = recipe_path
        self.raw = raw
        self.out_root = out
        self.jobs = jobs
        R = self.recipe
        self.id = R["id"]
        assert R["category"] in CATEGORIES, R["category"]
        if R.get("family") not in FAMILIES:
            R["family"] = CAT_DEFAULT_FAM[R["category"]]
        self.dir = os.path.join(out, self.id)
        self.report = OrderedDict(id=self.id)
        self.tighten = 0

    def sus_len(self) -> float:
        base = min(float(self.recipe.get("susLen", 7.0)), getattr(self, "sus_cur", 99.0))
        return base * (0.85 ** self.tighten)

    def budget_mb(self) -> float:
        return float(self.recipe.get("budgetMB", BUDGET_PIANO_MB if self.recipe.get("piano") else BUDGET_TYPICAL_MB))

    # ------------------------------------------------------------------ 1. parse + interpret
    def collect(self) -> List[Reg]:
        R = self.recipe
        regs: List[Reg] = []
        self.sfz_files: Dict[str, sfzmod.SfzFile] = {}
        self._parsed = {}
        drops = defaultdict(int)
        self.artic_names = []
        for ai, art in enumerate(R["artics"]):
            self.artic_names.append(art["name"])
            for sfz_rel in (art["sfz"] if isinstance(art["sfz"], list) else [art["sfz"]]):
                self._collect_one(ai, art, sfz_rel, regs, drops)
        self.report["dropped"] = dict(drops)
        return regs

    def _collect_one(self, ai, art, sfz_rel, regs, drops):
        R = self.recipe
        if True:
            p = os.path.join(self.raw, sfz_rel)
            dp = art.get("defaultPath", R.get("defaultPath", ""))
            if (p, dp) not in self._parsed:
                if p.lower().endswith((".sf2", ".sf3")):
                    self._parsed[(p, dp)] = sf2mod.parse(p, art.get("preset", R.get("preset")),
                                                         cache_dir=os.path.join(self.out_root, ".sf2cache"))
                else:
                    self._parsed[(p, dp)] = sfzmod.parse(p, R.get("defines"), dp)
            f = self._parsed[(p, dp)]
            self.sfz_files[p] = f
            ip = Interpreter(f, R, art)
            sw = art.get("sw")
            sw_n = sfzmod.note_to_midi(str(sw)) if sw is not None else None
            label = art.get("swLabel")
            for r in f.regions:
                rsw = r.get("sw_last")
                if sw_n is not None or label:
                    if rsw is not None:
                        if sw_n is not None and sfzmod.note_to_midi(rsw) != sw_n:
                            continue
                        if label and not re.search(label, r.get("sw_label", "")):
                            continue
                    elif not art.get("includeUnswitched", True):
                        continue
                elif rsw is not None and art.get("swDefaultOnly", True):
                    dflt = r.get("sw_default")
                    if dflt is not None and sfzmod.note_to_midi(rsw) != sfzmod.note_to_midi(dflt):
                        continue
                for pat in art.get("onlyMatch", []):
                    if not re.search(pat, (r.get("_sample") or "") + "|" + r.get("group_label", "")):
                        break
                else:
                    regs.extend(ip.interpret(r, ai))
            for k, v in ip.drops.items():
                drops[k] += v

    # ------------------------------------------------------------------ 2. budget
    def estimate_mb(self, regs: List[Reg], meta: Dict[str, dict], cap: Optional[float]) -> float:
        per_src = defaultdict(list)
        for r in regs:
            per_src[r.src].append(r)
        tot = 0
        for src, rs in per_src.items():
            m = meta[src]
            s0 = min(r.offset for r in rs)
            r = rs[0]
            end = m["end60"]
            ends = [x.end for x in rs if x.end]
            if ends:
                end = min(end, max(ends))
            if any(x.loop_mode in ("continuous", "sustain") and x.le for x in rs):
                le = max(x.le for x in rs if x.le)
                end = min(m["frames"], le + int(0.05 * m["sr"]))
            elif any(x.sustaining and x.kind == "attack" for x in rs):
                end = min(end, s0 + int(self.sus_len() * 0.55 * m["sr"]))
            else:
                c = effective_cap(self.recipe, rs, cap)
                ext = self.recipe.get("extendTail")
                if ext and any(x.kind == "attack" for x in rs) and end >= m["frames"] - int(0.05 * m["sr"]):
                    end = m["frames"] + int(float(ext) * m["sr"])      # upper bound: the loop extension
                if c:
                    end = min(end, s0 + int(c * m["sr"]))
            tot += max(0, end - s0) * m["ch"] * 2
        return tot / 1048576.0

    def apply_budget(self, base: List[Reg], meta) -> Tuple[List[Reg], dict]:
        R = self.recipe
        max_layers = int(R.get("maxLayers", 99))
        max_rr = int(R.get("maxRR", 99))
        cap = R.get("maxLen")
        budget = self.budget_mb() * (0.92 ** self.tighten)      # a retry after a measured overshoot aims lower
        steps_taken = []

        def build():
            regs = reduce_layers(base, max_layers)
            regs = reduce_rr(regs, max_rr)
            return regs

        self.sus_cur = 99.0
        regs = build()
        mb = self.estimate_mb(regs, meta, cap)
        first_mb = mb
        steps = [tuple(x) for x in R["budgetSteps"]] if R.get("budgetSteps") else BUDGET_STEPS
        for kind, val in steps:
            if mb <= budget:
                break
            if kind == "cap" and (cap is None or val < cap):
                cap = val
            elif kind == "rr" and val < max_rr:
                if val < int(R.get("minRR", 1)):
                    continue
                max_rr = val
            elif kind == "sus" and val < self.sus_cur:
                self.sus_cur = val
            elif kind == "layers" and val < max_layers:
                if val < int(R.get("minLayers", 1)):
                    continue
                max_layers = val
            else:
                continue
            regs = build()
            mb = self.estimate_mb(regs, meta, cap)
            steps_taken.append(f"{kind}={val} → {mb:.1f} MB")
        return regs, {"budgetMB": budget, "estimateBeforeMB": round(first_mb, 1), "estimateAfterMB": round(mb, 1),
                      "maxLayers": max_layers, "maxRR": max_rr, "tailCapS": cap, "susLenS": round(self.sus_len(), 2),
                      "steps": steps_taken}

    # ------------------------------------------------------------------ main
    def run(self):
        t0 = time.time()
        R = self.recipe
        log(f"== {self.id}: parsing")
        regs = self.collect()
        if not regs:
            raise RuntimeError("no regions survived interpretation: " + json.dumps(self.report["dropped"]))
        regs = layerize(regs)
        srcs = sorted({r.src for r in regs})
        log(f"   {len(regs)} regions over {len(srcs)} source files; analysing")
        with ProcessPoolExecutor(self.jobs) as ex:
            meta = {m["path"]: m for m in ex.map(job_analyse, srcs, chunksize=4)}
        base_regs = regs
        while True:
            regs, bud = self.apply_budget([replace(r) for r in base_regs], meta)
            regs = fill_vel_holes(regs)
            regs, holes = fill_holes(regs, tuple(R["keyRange"]) if R.get("keyRange") else None)
            bud["keyHolesFilled"] = holes
            self.report["budget"] = bud
            log(f"   budget: {bud}")
            # ---- output samples (dedup per source file)
            if os.path.exists(self.dir):
                shutil.rmtree(self.dir)
            os.makedirs(os.path.join(self.dir, "samples"))
            os.makedirs(os.path.join(self.dir, "source"))
            order = sorted(regs, key=lambda r: (r.a, ["attack", "release", "noise"].index(r.kind), r.lk, r.lv, r.rr, r.rand))
            src_index: Dict[str, int] = OrderedDict()
            for r in order:
                if r.src not in src_index:
                    src_index[r.src] = len(src_index)
            bits_src = {s: (24 if meta[s]["subtype"] in ("PCM_24", "PCM_32", "FLOAT", "DOUBLE") else 16) for s in src_index}
            want24 = bool(R.get("piano")) and R.get("bits", "source") in ("source", 24)
            jobs = []
            for s, i in src_index.items():
                rs = [r for r in regs if r.src == s]
                start0 = min(r.offset for r in rs)
                loops = [r for r in rs if r.loop_mode in ("continuous", "sustain") and r.le]
                if loops:
                    mode = "src_loop"
                elif any(r.sustaining and r.kind == "attack" for r in rs):
                    mode = "sustain_find"
                else:
                    mode = "decay"
                job = {"src": s, "out": os.path.join(self.dir, "samples", f"{i + 1:04d}.flac"), "start0": start0,
                       "starts": sorted({r.offset for r in rs}), "mode": mode,
                       "bits": 24 if (want24 and bits_src[s] == 24) else 16,
                       "cap_s": effective_cap(R, rs, bud["tailCapS"]),
                       "sus_len_s": self.sus_len(),
                       "end_opcode": max((r.end for r in rs if r.end), default=None),
                       "want_tail": any(r.kind == "attack" for r in rs),
                       "extend_s": R.get("extendTail") if any(r.kind == "attack" for r in rs) else None}
                if loops:
                    job.update(ls=loops[0].ls, le=loops[0].le, xf_s=loops[0].xf_s, loop_mode=loops[0].loop_mode)
                jobs.append(job)
            log(f"   writing {len(jobs)} samples")
            with ProcessPoolExecutor(self.jobs) as ex:
                results = {res["src"]: res for res in ex.map(job_render_sample, jobs, chunksize=2)}
            self.assemble(regs, src_index, results, meta, bud)
            if self.report["sizeMB"] <= self.budget_mb() or self.tighten >= 5:
                break
            self.tighten += 1
            log(f"   measured {self.report['sizeMB']} MB > budget: retry {self.tighten} with a tighter plan")
        self.report.pop("overBudget", None) if self.report["sizeMB"] <= self.budget_mb() else None
        self.write_source(src_index, meta, results)
        self.calibrate_and_preview()
        self.report["seconds"] = round(time.time() - t0, 1)
        with open(os.path.join(self.dir, "build-report.json"), "w") as f:
            json.dump(self.report, f, indent=1)
        log(f"   done in {self.report['seconds']} s: {self.report['sizeMB']} MB RAM, {self.report['diskMB']} MB disk")
        return self.report

    # ------------------------------------------------------------------ 3. map.json
    def assemble(self, regs, src_index, results, meta, bud):
        R = self.recipe
        out_regions = []
        per_note_rms = defaultdict(list)
        region_rms = []
        loops_ok = tails = tails_missing = tails_missing_long = 0
        start0s = {}
        for r in regs:
            start0s[r.src] = min(start0s.get(r.src, r.offset), r.offset)
        for r in sorted(regs, key=lambda r: (r.a, ["attack", "release", "noise"].index(r.kind), r.lk, r.lv, r.rr, r.rand)):
            res = results[r.src]
            smp = src_index[r.src]
            start0 = start0s[r.src]
            start = r.offset - start0
            ps = res["per_start"][str(r.offset)]
            loop, ls, le, xf, tls, tle = r.loop_mode, 0, 0, 0, 0, 0
            if "loop" in res and r.kind == "attack":
                ls, le, xf = res["loop"][0], res["loop"][1], res["loop"][2]
                if ls < start + 1:
                    loop = "no_loop"
                    ls = le = xf = 0
                else:
                    loop = r.loop_mode if r.loop_mode in ("continuous", "sustain") else "sustain"
                    tls, tle = ls, le
                    loops_ok += 1
            else:
                loop = "one_shot" if r.loop_mode == "one_shot" else "no_loop"
                if "tail" in res and r.kind == "attack" and res["tail"][0] > start:
                    tls, tle, xf = res["tail"][0], res["tail"][1], res["tail"][2]
                    tails += 1
                elif r.kind == "attack":
                    tails_missing += 1
                    if (res["frames"] - start) / res["sr"] >= 1.0 and loop != "one_shot":
                        tails_missing_long += 1
            gain_db = r.gain_db + res["comp_db"]
            rms_src = ps["rms"] * (10 ** (r.gain_db / 20))
            rec = OrderedDict([
                ("a", r.a), ("kind", r.kind), ("smp", smp), ("lk", r.lk), ("hk", r.hk), ("lv", r.lv), ("hv", r.hv),
                ("xfLo", r.lv), ("xfHi", r.hv), ("root", r.root), ("cents", float(r.cents)), ("gainDb", 0.0),
                ("gainNorm", 1.0), ("pan", int(round(r.pan))), ("start", int(start)), ("end", int(res["frames"])),
                ("onset", int(max(start, min(res["frames"] - 1, ps["onset"])))), ("loop", loop), ("ls", int(ls)),
                ("le", int(le)), ("xf", int(xf)), ("tailLs", int(tls)), ("tailLe", int(tle)),
                ("rr", [int(r.rr[0]), int(r.rr[1])]), ("rand", [float(r.rand[0]), float(r.rand[1])]),
                ("grp", int(r.grp)), ("offBy", int(r.off_by)), ("offMode", r.off_mode), ("env", r.env),
                ("rtDecay", float(r.rt_decay)), ("velCurve", None)])
            rec["_gain_db"] = gain_db
            rec["_curve"] = r.curve
            rec["_rms"] = rms_src
            out_regions.append(rec)
            if r.kind == "attack":
                per_note_rms[(r.a, r.root, r.lk)].append(rec)
        # ---- gainNorm (50 % restore) + velocity-curve compensation fit
        pts = defaultdict(lambda: ([], []))            # per articulation: layer centres, sqrt(layer/top)
        for key, recs in per_note_rms.items():
            top = max(x["_rms"] for x in recs)
            if top <= 0:
                continue
            bands = defaultdict(list)
            for x in recs:
                bands[(x["lv"], x["hv"])].append(x["_rms"])
            for x in recs:
                g = math.sqrt(top / x["_rms"]) if x["_rms"] > 0 else 1.0
                x["gainNorm"] = round(max(0.25, min(4.0, g)), 4)
            if len(bands) > 1:
                for (lv, hv), rl in bands.items():
                    m = float(np.mean(rl))
                    if m > 0:
                        pts[key[0]][0].append((lv + hv) / 2.0)
                        pts[key[0]][1].append(math.sqrt(m / top))
        powers = {}
        for a_, (pv_, ps_) in pts.items():
            p = 0.0
            if len(pv_) >= 2:
                lv_ = np.log(np.array(pv_) / 127.0)
                ls_ = np.log(np.maximum(np.array(ps_), 1e-4))
                den = float(np.sum(lv_ ** 2))
                p = float(np.sum(lv_ * ls_) / den) if den > 0 else 0.0
                p = max(0.0, min(2.0, p))
            powers[a_] = max(p, float(self.recipe.get("velPowerMin", 0.0)))
        self.report["velFitPower"] = {self.artic_names[k]: round(v, 3) for k, v in sorted(powers.items())}
        vs = [0, 1, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127]
        for x in out_regions:
            c = x.pop("_curve")
            if x["kind"] == "attack":
                p = powers.get(x["a"], float(self.recipe.get("velPowerMin", 0.0)))
                cv = [[v, round(float(c[v]) * ((max(v, 1) / 127.0) ** p), 5)] for v in vs]
            else:
                cv = [[v, round(float(c[v]), 5)] for v in vs]
            cv[0][1] = 0.0 if cv[1][1] > 0 else cv[0][1]
            x["velCurve"] = cv
            x["gainDb"] = round(x.pop("_gain_db"), 3)
            x.pop("_rms")
        samples = [f"{i + 1:04d}.flac" for i in range(len(src_index))]
        self.floor_by_smp = {src_index[s_]: res["floor_rel_db"] for s_, res in results.items()}
        R_has_noise = any(x["kind"] == "noise" for x in out_regions)
        R_has_rel = any(x["kind"] == "release" for x in out_regions)
        m = OrderedDict([
            ("torg", 1), ("id", self.id), ("name", R["name"]), ("family", R["family"]), ("category", R["category"]),
            ("credit", R["credit"]), ("polyMax", int(R.get("polyMax", 32))), ("hasNoise", R_has_noise),
            ("hasRelease", R_has_rel), ("artics", self.artic_names), ("samples", samples), ("markers", {}),
            ("regions", out_regions)])
        self.map = m
        self._write_map()
        ram = sum(res["frames"] * res["ch"] * 2 for res in results.values()) / 1048576.0
        disk = sum(os.path.getsize(os.path.join(self.dir, "samples", s)) for s in samples) / 1048576.0
        n_att = [x for x in out_regions if x["kind"] == "attack"]
        layers = defaultdict(set)
        rrs = defaultdict(set)
        for x in n_att:
            layers[(x["a"], x["lk"])].add((x["lv"], x["hv"]))
            rrs[(x["a"], x["lk"], x["lv"])].add((tuple(x["rr"]), tuple(x["rand"])))
        self.report.update(OrderedDict(
            sizeMB=round(ram, 1), diskMB=round(disk, 1), samples=len(samples), regions=len(out_regions),
            attackRegions=len(n_att), releaseRegions=sum(x["kind"] == "release" for x in out_regions),
            noiseRegions=sum(x["kind"] == "noise" for x in out_regions), artics=self.artic_names,
            maxLayers=max((len(v) for v in layers.values()), default=0),
            maxRR=max((len(v) for v in rrs.values()), default=0),
            keyRange=[min(x["lk"] for x in n_att), max(x["hk"] for x in n_att)],
            sustainLoops=loops_ok, tailLoops=tails, tailLoopsMissing=tails_missing, tailLoopsMissingLong=tails_missing_long,
            extendedTails=int(sum(1 for res in results.values() if res.get("extendedS", 0) > 0)),
            bits=sorted({j for j in [24 if sf.info(os.path.join(self.dir, "samples", s)).subtype == "PCM_24" else 16 for s in samples]}),
            sampleRates=sorted({res["sr"] for res in results.values()}),
            channels=sorted({res["ch"] for res in results.values()}),
            sourceClippedSamples=int(sum(res["clip"] for res in results.values())),
            worstFloorRelDb=round(max((res["floor_rel_db"] for res in results.values()), default=-200), 1),
        ))
        seams = [res[k][3]["ratio"] for res in results.values() for k in ("loop", "tail") if k in res]
        xfdb = [res[k][3].get("xfDb", 0.0) for res in results.values() for k in ("loop", "tail") if k in res]
        if seams:
            self.report["seam"] = {"n": len(seams), "median": round(float(np.median(seams)), 3),
                                   "worst": round(float(max(seams)), 3), "over1_5": int(sum(x > 1.5 for x in seams)),
                                   "xfDbMin": round(float(min(xfdb)), 1), "xfDbMax": round(float(max(xfdb)), 1)}
        if ram > self.budget_mb() * 1.001:
            self.report["overBudget"] = True
            log(f"   !! over budget: {ram:.1f} MB > {self.budget_mb()} MB")

    def _write_map(self):
        with open(os.path.join(self.dir, "map.json"), "w") as f:
            json.dump(self.map, f, indent=1)

    # ------------------------------------------------------------------ 4. source/ + provenance
    def write_source(self, src_index, meta, results):
        R = self.recipe
        sdir = os.path.join(self.dir, "source")
        mdir = os.path.join(sdir, "mapping")
        os.makedirs(mdir, exist_ok=True)
        seen = set()
        for p, f in self.sfz_files.items():
            base = os.path.dirname(p)
            for inc in f.includes:
                if inc in seen:
                    continue
                seen.add(inc)
                rel = os.path.relpath(inc, base)
                if rel.startswith(".."):
                    rel = os.path.basename(inc)
                dst = os.path.join(mdir, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                if os.path.getsize(inc) > 20 * 1048576:      # a big .sf2 carries its samples: reference it instead
                    with open(dst + ".txt", "w") as f:
                        f.write(f"{os.path.relpath(inc, self.raw)}\nsha256 {hashlib.sha256(open(inc, 'rb').read()).hexdigest()}\n")
                else:
                    shutil.copy2(inc, dst)
        for extra in R.get("extraSourceFiles", []):
            p = os.path.join(self.raw, extra)
            if os.path.exists(p):
                shutil.copy2(p, os.path.join(mdir, os.path.basename(p)))
        # LICENCE: the dated LICENSE-SOURCE.txt (URL, quote, verbatim licence) + the set's own licence file(s)
        lic_dir = os.path.join(self.raw, R["licenceDir"])
        parts = []
        for name in ("LICENSE-SOURCE.txt", "LICENSE", "LICENSE.txt", "LICENSE.md", "cc0.txt", "license.txt", "readme.txt", "README.md"):
            p = os.path.join(lic_dir, name)
            if os.path.exists(p) and name.lower().startswith(("license", "cc0")):
                with open(p, "rb") as f:
                    parts.append(f"===== {name} =====\n" + f.read().decode("utf-8", "replace"))
        if not parts:
            raise RuntimeError("no licence file in " + lic_dir)
        head = (f"Instrument: {R['name']} ({self.id})\nLicence: {R['licence']}\nCredit: {R['credit']}\n"
                f"Source: {R['url']}\nAuthor: {R['author']}\n"
                + ("Modified by Waves Crate: trimmed, looped, level-normalised and re-encoded as FLAC.\n"
                   if R['licence'].upper().startswith("CC-BY") else "")
                + (f"Licence risk: {R['licenceRisk']}\n" if R.get("licenceRisk") else "") + "\n")
        with open(os.path.join(sdir, "LICENCE.txt"), "w") as f:
            f.write(head + "\n\n".join(parts))
        with open(os.path.join(sdir, "provenance.csv"), "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["file", "source_file", "url", "author", "licence", "date", "sha256", "edits"])
            for s, i in src_index.items():
                res = results[s]
                edits = ["trimmed", "end fade", f"peak-normalised ({-res['comp_db']:+.2f} dB)",
                         "TPDF-dithered to 16-bit" if sf.info(res["out"]).subtype == "PCM_16" else "24-bit",
                         "FLAC"]
                if "loop" in res:
                    edits.append("loop markers (metadata only)")
                if "tail" in res:
                    edits.append("tail-loop markers (metadata only)")
                w.writerow([f"samples/{i + 1:04d}.flac", os.path.relpath(s, self.raw), R["url"], R["author"],
                            R["licence"], TODAY, meta[s]["sha256"], "; ".join(edits)])

    # ------------------------------------------------------------------ 5. calibrate + preview + QA
    def calibrate_and_preview(self):
        rd = an.Renderer(self.dir)
        att = [r for r in self.map["regions"] if r["kind"] == "attack" and r["a"] == 0]
        klo, khi = min(r["lk"] for r in att), max(r["hk"] for r in att)
        note = 60 if klo <= 60 <= khi else int(round((klo + khi) / 2))
        self.report["qaNote"] = note

        def level(vel, dur=1.5, hold=1.2):
            y = rd.render(note, vel, dur, hold, with_release=False)
            mono = y.mean(axis=1)
            ons = an.find_onset(mono, 0, len(mono))
            seg = mono[ons: ons + int(0.5 * rd.sr)]
            return y, an.db(float(np.sqrt(np.mean(seg ** 2))) if len(seg) else 0.0)

        _, l100 = level(100)
        off = CALIB_RMS_DB - l100 if l100 > -150 else 0.0
        pk127 = max(an.db(float(np.abs(level(v, 2.0, 1.6)[0]).max())) for v in (96, 112, 120, 127)) + off
        if pk127 > -1.0:
            off -= (pk127 + 1.0)
        for r in self.map["regions"]:
            r["gainDb"] = round(r["gainDb"] + off, 3)
        self.report["calibrationDb"] = round(off, 2)
        self._write_map()
        rd = an.Renderer(self.dir)
        # preview: middle C (or the nearest playable note), vel 90, 3 s, note-off at 2.2 s
        pv = rd.render(note, 90, 3.0, 2.2)
        pk = float(np.abs(pv).max())
        if pk > 0.89:
            pv *= 0.89 / pk
        sf.write(os.path.join(self.dir, "preview.flac"), pv, rd.sr, subtype="PCM_16", format="FLAC")
        # ---- listen-by-numbers QA
        qa = OrderedDict(note=note)
        vals = {}
        for v in (40, 80, 120):
            y = rd.render(note, v, 2.0, 1.6, with_release=False)
            mono = y.mean(axis=1)
            ons = an.find_onset(mono, 0, len(mono))
            seg = mono[ons: ons + int(0.5 * rd.sr)]
            vals[v] = (an.db(float(np.abs(y).max())), an.db(float(np.sqrt(np.mean(seg ** 2)))) if len(seg) else -200)
            qa[f"v{v}"] = {"peakDb": round(vals[v][0], 1), "rmsDb": round(vals[v][1], 1)}
        qa["step40to80Db"] = round(vals[80][1] - vals[40][1], 1)
        qa["step80to120Db"] = round(vals[120][1] - vals[80][1], 1)
        sweep = []
        for v in range(8, 128, 8):
            y = rd.render(note, v, 0.8, 0.8, with_release=False)
            mono = y.mean(axis=1)
            ons = an.find_onset(mono, 0, len(mono))
            seg = mono[ons: ons + int(0.4 * rd.sr)]
            sweep.append(an.db(float(np.sqrt(np.mean(seg ** 2)))) if len(seg) else -200)
        jumps = [sweep[i + 1] - sweep[i] for i in range(len(sweep) - 1)]
        qa["maxVelJumpDb"] = round(max(abs(j) for j in jumps), 1)
        qa["nonMonotonicSteps"] = int(sum(1 for j in jumps if j < -1.5))
        qa["sweepRmsDb"] = [round(v, 1) for v in sweep]
        # hard switch at each band edge (what a crossfade-less player would do), minus the smooth curve's own step
        steps = []
        bands = sorted({(r["lv"], r["hv"]) for r in self.map["regions"] if r["kind"] == "attack" and r["a"] == 0
                        and r["lk"] <= note <= r["hk"]})

        def rms_at(v):
            y = np.zeros((int(0.8 * rd.sr), 2))
            for r, w in rd.pick(note, v, 0, "attack", xfade=False):
                y += rd.render_region(r, note, 0.8, v, 0.8)
            mono = y.mean(axis=1)
            ons = an.find_onset(mono, 0, len(mono))
            seg = mono[ons: ons + int(0.4 * rd.sr)]
            return an.db(float(np.sqrt(np.mean(seg ** 2)))) if len(seg) else -200

        for (lv, hv) in bands:
            if 1 < lv <= 127:
                r0 = [r for r, _ in rd.pick(note, lv, 0, "attack", xfade=False)]
                if not r0:
                    continue
                c = rd.vel_gain(r0[0]["velCurve"], lv) / max(1e-9, rd.vel_gain(r0[0]["velCurve"], lv - 1))
                steps.append(abs(rms_at(lv) - rms_at(lv - 1) - an.db(c)))
        qa["layerStepDb"] = round(max(steps), 1) if steps else 0.0
        qa["renderClips"] = bool(max(v[0] for v in vals.values()) > -0.1)
        qa["previewPeakDb"] = round(an.db(float(np.abs(pv).max())), 1)
        # source-level stats of the regions that play this note (all layers): quietest 50 ms of the recording
        used = [r for r in self.map["regions"] if r["kind"] == "attack" and r["a"] == 0 and r["lk"] <= note <= r["hk"]]
        floors = [self.floor_by_smp[r["smp"]] for r in used if r["smp"] in self.floor_by_smp]
        qa["noiseFloorRelDb"] = round(max(floors), 1) if floors else None
        self.report["qa"] = qa
        flags = []
        if qa["nonMonotonicSteps"]:
            flags.append("loudness not monotonic over velocity")
        sustaining_inst = self.recipe.get("sustaining") or all(a.get("sustaining") for a in self.recipe["artics"][:1])
        if qa["noiseFloorRelDb"] is not None and qa["noiseFloorRelDb"] > -50 and not sustaining_inst:
            flags.append(f"recording never quieter than {qa['noiseFloorRelDb']} dB rel. peak (hiss, or cut short)")
        if self.report.get("sourceClippedSamples", 0) > 0:
            flags.append(f"{self.report['sourceClippedSamples']} clipped source samples")
        if self.report.get("tailLoopsMissingLong", 0) > 0:
            flags.append(f"{self.report['tailLoopsMissingLong']} decaying regions ≥ 1 s without a tail loop")
        if qa.get("layerStepDb", 0) > 6.0:
            flags.append(f"hard layer step {qa['layerStepDb']} dB at a band edge (runtime crossfade hides it)")
        if self.report.get("seam", {}).get("over1_5", 0):
            flags.append(f"{self.report['seam']['over1_5']} loop seams over 1.5x local median")
        self.report["flags"] = flags


# ============================================================================================ library files
def load_json(p, default):
    if os.path.exists(p):
        with open(p) as f:
            return json.load(f)
    return default


def update_ids(out_root: str, ids_new: List[str]) -> Dict[str, int]:
    """Append-only: read the library's ids.json AND the repo snapshot, never renumber, append new ids."""
    ids = OrderedDict()
    for p in (os.path.join(REPO_ORGANICS, "ids.json"), os.path.join(out_root, "ids.json")):
        for k, v in load_json(p, {}).items():
            if k in ids and ids[k] != v:
                raise RuntimeError(f"ids.json conflict for {k}: {ids[k]} vs {v}")
            ids[k] = v
    if "test.sine" not in ids:
        ids["test.sine"] = 1           # the frozen fixture's number is reserved forever
    used = set(ids.values())
    nxt = max(used) + 1 if used else 1
    for i in ids_new:
        if i not in ids:
            while nxt in used:
                nxt += 1
            ids[i] = nxt
            used.add(nxt)
    with open(os.path.join(out_root, "ids.json"), "w") as f:
        json.dump(ids, f, indent=1)
    return ids


def rebuild_index(out_root: str, recipes_dir: str) -> list:
    recipes = {}
    for p in glob.glob(os.path.join(recipes_dir, "*.json")):
        r = load_json(p, None)
        if r:
            recipes[r["id"]] = r
    entries = []
    for d in sorted(os.listdir(out_root)):
        mp = os.path.join(out_root, d, "map.json")
        rp = os.path.join(out_root, d, "build-report.json")
        if not os.path.exists(mp) or not os.path.exists(rp):
            continue
        m = load_json(mp, {})
        rep = load_json(rp, {})
        rec = recipes.get(m["id"], {})
        e = OrderedDict([("id", m["id"]), ("name", m["name"]), ("family", m["family"]), ("category", m["category"]),
                         ("tags", rec.get("tags", [])), ("sizeMB", rep.get("sizeMB", 0.0)),
                         ("licence", rec.get("licence", "")), ("credit", m["credit"])])
        if rec.get("licenceRisk"):
            e["licenceRisk"] = rec["licenceRisk"]
        entries.append(e)
    entries.sort(key=lambda e: (CATEGORIES.index(e["category"]), e["name"]))
    with open(os.path.join(out_root, "index.json"), "w") as f:
        json.dump(entries, f, indent=1)
    update_ids(out_root, [e["id"] for e in entries])
    return entries


LICENCE_URLS = {"CC-BY-3.0": "https://creativecommons.org/licenses/by/3.0/",
                "CC-BY-4.0": "https://creativecommons.org/licenses/by/4.0/"}


def snapshot(out_root: str, recipes_dir: str):
    """Copy index.json / ids.json into Resources/Organics (stable ids for presets) and write CREDITS.md."""
    os.makedirs(REPO_ORGANICS, exist_ok=True)
    for n in ("index.json", "ids.json"):
        shutil.copy2(os.path.join(out_root, n), os.path.join(REPO_ORGANICS, n))
    idx = load_json(os.path.join(out_root, "index.json"), [])
    recipes = {}
    for p in glob.glob(os.path.join(recipes_dir, "*.json")):
        r = load_json(p, None)
        if r:
            recipes[r["id"]] = r
    lines = ["# Organics — sample credits (About screen)", "",
             "Generated by `Tools/organics/torgc.py --snapshot` from the compiled library's `index.json`. Every line under",
             "**Required** is a CC-BY licence condition: it must appear in the About → Credits page and the manual, and the",
             "instrument's samples must ship as plain, unencrypted FLAC (no DRM).", "",
             "Terrain includes sampled instruments from the following open libraries:", "", "## Required (CC BY)", ""]
    by_credit = OrderedDict()
    for e in idx:
        if e.get("licence", "").upper().startswith("CC-BY"):
            by_credit.setdefault(e["credit"], []).append(e)
    for credit, es in by_credit.items():
        lic = es[0]["licence"]
        ids = ", ".join(f"`{x['id']}`" for x in es)
        risk = "".join(f" **Licence risk: {x['licenceRisk']}.**" for x in es if x.get("licenceRisk"))
        lines.append(f"- {credit}. Licensed under {lic.replace('CC-BY-', 'CC BY ')} — {LICENCE_URLS.get(lic, '')} ({ids}){risk}")
    lines += ["", "## With thanks (CC0 / public domain / Unlicense — credit not required)", ""]
    thanks = OrderedDict()
    for e in idx:
        if not e.get("licence", "").upper().startswith("CC-BY"):
            thanks.setdefault(e["credit"], []).append(e["id"])
    for credit, ids in thanks.items():
        lines.append(f"- {credit} ({', '.join('`' + i + '`' for i in ids)})")
    lines.append("")
    with open(os.path.join(REPO_ORGANICS, "CREDITS.md"), "w") as f:
        f.write("\n".join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recipes", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--raw", default=DEFAULT_RAW)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--index-only", action="store_true")
    ap.add_argument("--min-free-gb", type=float, default=8.0)
    ap.add_argument("--snapshot", action="store_true", help="copy index/ids into Resources/Organics + write CREDITS.md")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rdir = os.path.join(HERE, "recipes")
    paths = sorted(glob.glob(os.path.join(rdir, "*.json"))) if a.all else a.recipes
    failed = []
    if not a.index_only:
        for p in paths:
            free = shutil.disk_usage(a.out).free / 1e9
            if free < a.min_free_gb:
                log(f"!! only {free:.1f} GB free (< {a.min_free_gb} GB): stopping before {p}")
                break
            try:
                Compiler(p, a.raw, a.out, a.jobs).run()
            except Exception as e:  # keep going: one bad source must not stop the library
                import traceback
                traceback.print_exc()
                failed.append((p, str(e)))
    entries = rebuild_index(a.out, rdir)
    log(f"index.json: {len(entries)} instruments")
    if a.snapshot:
        snapshot(a.out, rdir)
        log(f"snapshot written to {REPO_ORGANICS}")
    for p, e in failed:
        log(f"FAILED {p}: {e}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
