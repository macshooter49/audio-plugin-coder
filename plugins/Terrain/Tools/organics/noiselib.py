#!/usr/bin/env python3
"""noiselib.py — the shared, licence-cleared mechanical-noise library for Organics (tp105 amendment: "Noise = real
mechanical noise").

    python3 noiselib.py [--raw DIR]           # (re)build raw/TerrainNoise/<set>/*.wav + manifest.json

Every noise one-shot is EXTRACTED from a recording we already ship under a cleared licence (no new downloads, no
logins, no permission): the key-release/damper noises of Salamander Grand v3 (CC BY 3.0), the per-note key
clicks and breath of Karoryfer Bear Sax (CC0), the breath of Karoryfer War Tuba (CC0), the fret/finger noises and
muted plucks of Karoryfer Emilyguitar (CC0), the bow-screech oscillator recordings of Karoryfer String Cyborgs
(CC0), the body knocks of Karoryfer x bigcat cello (CC0). Terrain-modelled EP key noises (Waves Crate, owned) are
added by `epmodel/` under raw/TerrainModels/*/noise and registered here as sets too.

Output per set: raw/TerrainNoise/<set>/<name>.wav (mono, source rate, 24-bit, peak −1 dBFS) and manifest.json:
    {"set", "description", "licence", "credit", "files": [{"file", "key"?, "origin": {source_file, url, author,
     licence, sha256, startS, endS, edits}}]}
torgc.py reads the manifests when a recipe asks for a set in "noiseMap" (see torgc.inject_noise); every injected
file carries its origin into the instrument's source/provenance.csv.
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import re
import shutil

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt

DEFAULT_RAW = os.path.expanduser("~/Developer/VST-Plugins/organics-library/raw")
TODAY = "2026-09-24"

SRC = {
    "salamander": dict(url="https://github.com/sfzinstruments/SalamanderGrandPiano (original: https://archive.org/details/SalamanderGrandPianoV3)",
                       author="Alexander Holm", licence="CC-BY-3.0",
                       credit="Salamander Grand Piano v3 by Alexander Holm (SFZ mapping by kinwie), CC BY 3.0, modified by Waves Crate"),
    "bearsax": dict(url="https://github.com/sfzinstruments/karoryfer.bear-sax", author="Karoryfer Samples",
                    licence="CC0-1.0", credit="Karoryfer Bear Sax, CC0"),
    "wartuba": dict(url="https://github.com/sfzinstruments/karoryfer.war-tuba", author="Karoryfer Samples",
                    licence="CC0-1.0", credit="Karoryfer War Tuba, CC0"),
    "emily": dict(url="https://github.com/sfzinstruments/karoryfer.emilyguitar", author="Karoryfer Samples",
                  licence="CC0-1.0", credit="Karoryfer Emilyguitar, CC0"),
    "cyborgs": dict(url="https://github.com/sfzinstruments/karoryfer.string-cyborgs", author="Karoryfer Samples",
                    licence="CC0-1.0", credit="Karoryfer String Cyborgs, CC0"),
    "cello": dict(url="https://github.com/sfzinstruments/karoryfer-bigcat.cello", author="Karoryfer Samples x bigcat",
                  licence="CC0-1.0", credit="Karoryfer x bigcat Cello, CC0"),
    "terrain": dict(url="Tools/organics/epmodel (Waves Crate physical-model renderer)", author="Waves Crate",
                    licence="Proprietary-WavesCrate", credit="Waves Crate physical model"),
}


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def load_mono(path):
    x, sr = sf.read(path, dtype="float64", always_2d=True)
    return x.mean(axis=1), sr


def onset(m, sr, thresh_db=-30.0):
    a = np.abs(m)
    pk = a.max()
    if pk <= 0:
        return 0
    idx = np.flatnonzero(a >= pk * 10 ** (thresh_db / 20))
    return int(idx[0]) if len(idx) else 0


def end_at(m, sr, start, rel_db=-54.0):
    hop = int(0.005 * sr)
    seg = m[start:]
    n = len(seg) // hop
    if n == 0:
        return len(m)
    env = 20 * np.log10(np.maximum(np.sqrt(np.mean(seg[:n * hop].reshape(n, hop) ** 2, axis=1)), 1e-12))
    above = np.flatnonzero(env >= env.max() + rel_db)
    return start + (int(above[-1]) + 1) * hop if len(above) else len(m)


def process(m, sr, a, b, hp=None, lp=None, shape=None, fade_in=0.002, fade_out=0.02):
    y = m[a:b].copy()
    edits = [f"cut {a / sr:.3f}–{b / sr:.3f} s", "mono sum"]
    if hp:
        y = sosfilt(butter(2, hp, "highpass", fs=sr, output="sos"), y)
        edits.append(f"high-pass {hp} Hz")
    if lp:
        y = sosfilt(butter(2, lp, "lowpass", fs=sr, output="sos"), y)
        edits.append(f"low-pass {lp} Hz")
    if shape:                                    # (attack s, hold s, decay tau s): a breath "puff" / bow onset envelope
        at, hold, tau = shape
        t = np.arange(len(y)) / sr
        env = np.where(t < at, np.sin(0.5 * np.pi * t / max(at, 1e-4)) ** 2, 1.0)
        env *= np.where(t > at + hold, np.exp(-(t - at - hold) / tau), 1.0)
        y *= env
        edits.append(f"envelope attack {at * 1000:.0f} ms, hold {hold * 1000:.0f} ms, decay τ {tau * 1000:.0f} ms")
    fi, fo = int(fade_in * sr), int(fade_out * sr)
    if fi > 0:
        y[:fi] *= np.sin(0.5 * np.pi * np.arange(fi) / fi) ** 2
    if fo > 0 and len(y) > fo:
        y[-fo:] *= np.cos(0.5 * np.pi * np.arange(fo) / fo) ** 2
    pk = np.abs(y).max()
    if pk > 0:
        y *= 10 ** (-1 / 20) / pk
    edits.append("fades, peak −1 dBFS, 24-bit WAV")
    return y, edits


class SetWriter:
    def __init__(self, raw, name, desc, src_key):
        self.raw = raw
        self.name = name
        self.dir = os.path.join(raw, "TerrainNoise", name)
        if os.path.exists(self.dir):
            shutil.rmtree(self.dir)
        os.makedirs(self.dir)
        s = SRC[src_key]
        self.man = {"set": name, "description": desc, "licence": s["licence"], "credit": s["credit"], "files": []}
        self.src = s

    def add(self, fname, y, sr, src_path, a, b, edits, key=None, src=None):
        out = os.path.join(self.dir, fname)
        sf.write(out, y, sr, subtype="PCM_24")
        s = src or self.src
        e = {"file": fname, "origin": {"source_file": os.path.relpath(src_path, self.raw), "url": s["url"],
                                        "author": s["author"], "licence": s["licence"], "sha256": sha(src_path),
                                        "startS": round(a / sr, 4), "endS": round(b / sr, 4), "edits": "; ".join(edits),
                                        "date": TODAY}}
        if key is not None:
            e["key"] = int(key)
        self.man["files"].append(e)

    def close(self):
        with open(os.path.join(self.dir, "manifest.json"), "w") as f:
            json.dump(self.man, f, indent=1)
        return len(self.man["files"])


# ---------------------------------------------------------------------------------------------------- sets
def piano_sets(raw):
    base = os.path.join(raw, "sfzinstruments", "SalamanderGrandPiano", "Samples")
    up = SetWriter(raw, "piano-keyup", "Grand-piano key release: damper felt landing + key/hammer return "
                   "(Salamander 'HammerNoise' rel samples), one per key 21–108. trig off.", "salamander")
    down = SetWriter(raw, "piano-keydown", "Grand-piano key/action thump: the loudest 120 ms (the thump) of "
                     "the Salamander key-action noises, high-passed, every 3rd key. trig on.", "salamander")
    for k in range(1, 89):
        p = os.path.join(base, f"rel{k}.flac")
        m, sr = load_mono(p)
        on = onset(m, sr)
        # key-up: keep the natural mechanical pre-delay (key travel → damper contact), trim the tail
        e = end_at(m, sr, on)
        y, ed = process(m, sr, 0, e, hp=30)
        up.add(f"keyup_{20 + k:03d}.wav", y, sr, p, 0, e, ed, key=20 + k)
        if k % 3 == 1:
            # the loudest 5 ms of the action noise (the thump itself), 20 ms before to 100 ms after
            h = int(0.005 * sr)
            n5 = len(m) // h
            e5 = np.sqrt(np.mean(m[:n5 * h].reshape(n5, h) ** 2, axis=1))
            pk = int(np.argmax(e5)) * h
            a = max(0, pk - int(0.020 * sr))
            b = min(len(m), pk + int(0.100 * sr))
            y, ed = process(m, sr, a, b, hp=60, fade_in=0.006, fade_out=0.04)
            down.add(f"keydown_{20 + k:03d}.wav", y, sr, p, a, b, ed, key=20 + k)
    return [up.close(), down.close()]


def sax_sets(raw):
    root = os.path.join(raw, "sfzinstruments", "karoryfer.bear-sax")
    mp = open(os.path.join(root, "Programs", "poly", "noises_map.sfz")).read()
    close = SetWriter(raw, "sax-keyclose", "Saxophone pad/key closing clicks, per fingering (Bear Sax, 1926 Conn "
                      "baritone), 4 round robins per note. trig on.", "bearsax")
    opn = SetWriter(raw, "sax-keyopen", "Saxophone pad/key opening clicks on release, per fingering (Bear Sax), "
                    "4 round robins. trig off.", "bearsax")
    # parse <group> lokey/pitch_keycenter/trigger + region samples
    for grp in re.split(r"<group>", mp)[1:]:
        km = re.search(r"pitch_keycenter=(\d+)", grp)
        if not km:
            continue
        key = int(km.group(1))
        rel = "trigger=release" in grp
        for smp in re.findall(r"sample=\.\./Samples/(\S+\.wav)", grp):
            p = os.path.join(root, "Samples", smp)
            if not os.path.exists(p):
                continue
            m, sr = load_mono(p)
            on = onset(m, sr)
            a = max(0, on - int(0.004 * sr))
            e = end_at(m, sr, on, -50.0)
            y, ed = process(m, sr, a, e, hp=80)
            (opn if rel else close).add(smp.replace(".wav", "") + ".wav", y, sr, p, a, e, ed, key=key)
    return [close.close(), opn.close()]


def breath_set(raw):
    w = SetWriter(raw, "breath-puff", "Breath onset puffs for winds and brass: 0.3–0.45 s shaped segments of "
                  "real breath recordings (Bear Sax breath noise, War Tuba breaths). trig on.", "bearsax")
    cuts = [("sfzinstruments/karoryfer.bear-sax/Samples/noise_breath.wav", "bearsax", [0.9, 2.1, 3.3]),
            ("sfzinstruments/karoryfer.war-tuba/Samples/breath_rr1_cnd.wav", "wartuba", [0.6, 2.0]),
            ("sfzinstruments/karoryfer.war-tuba/Samples/breath_rr3_cnd.wav", "wartuba", [0.8])]
    i = 0
    for rel, sk, starts in cuts:
        p = os.path.join(raw, rel)
        m, sr = load_mono(p)
        for s in starts:
            a = int(s * sr)
            b = a + int(0.42 * sr)
            y, ed = process(m, sr, a, b, hp=120, shape=(0.018, 0.04, 0.09), fade_in=0.0, fade_out=0.03)
            i += 1
            w.add(f"breath_{i:02d}.wav", y, sr, p, a, b, ed, src=SRC[sk])
    w.man["credit"] = SRC["bearsax"]["credit"] + "; " + SRC["wartuba"]["credit"]
    return [w.close()]


def guitar_sets(raw):
    root = os.path.join(raw, "sfzinstruments", "karoryfer.emilyguitar", "noises")
    fret = SetWriter(raw, "guitar-fret", "Guitar fretting-hand noise: finger squeak/lift on the strings "
                     "(Emilyguitar 'fingering'), 5 variants. trig off.", "emily")
    pick = SetWriter(raw, "guitar-pick", "Pick-on-string click: the first 70 ms of Emilyguitar muted plucks, "
                     "12 variants. trig on.", "emily")
    for p in sorted(glob.glob(os.path.join(root, "fingering*_rr*.wav"))):
        m, sr = load_mono(p)
        on = onset(m, sr, -36.0)
        a = max(0, on - int(0.01 * sr))
        e = end_at(m, sr, on, -40.0)
        y, ed = process(m, sr, a, e, hp=150, fade_out=0.04)
        fret.add(os.path.basename(p), y, sr, p, a, e, ed)
    for p in sorted(glob.glob(os.path.join(root, "muted[1-3]_rr*.wav")))[:12]:
        m, sr = load_mono(p)
        on = onset(m, sr)
        a = max(0, on - int(0.002 * sr))
        b = min(len(m), on + int(0.07 * sr))
        y, ed = process(m, sr, a, b, hp=200, fade_out=0.03)
        pick.add(os.path.basename(p), y, sr, p, a, b, ed)
    return [fret.close(), pick.close()]


def bow_set(raw):
    w = SetWriter(raw, "bow-start", "Bow contact scrape at the start of a stroke: shaped 0.25 s segments of the "
                  "String Cyborgs bow-screech recordings (cello and double bass). trig on.", "cyborgs")
    i = 0
    for inst in ("blackheart", "zinc"):
        for p in sorted(glob.glob(os.path.join(raw, "sfzinstruments", "karoryfer.string-cyborgs", "Samples", inst,
                                               "**", "noise*osc*.wav"), recursive=True))[:3]:
            m, sr = load_mono(p)
            for s in (0.7, 2.3):
                a = int(s * sr)
                b = a + int(0.25 * sr)
                if b > len(m):
                    continue
                y, ed = process(m, sr, a, b, hp=250, shape=(0.012, 0.02, 0.06), fade_in=0.0, fade_out=0.03)
                i += 1
                w.add(f"bow_{i:02d}.wav", y, sr, p, a, b, ed)
    return [w.close()]


def knock_set(raw):
    w = SetWriter(raw, "wood-knock", "Short wooden knock (the body knocks of the Karoryfer x bigcat cello), "
                  "trimmed to 140 ms, for wooden mallet instruments. trig on.", "cello")
    for p in sorted(glob.glob(os.path.join(raw, "sfzinstruments", "karoryfer-bigcat.cello", "Samples", "noises",
                                           "stuk*.wav")))[:8]:
        m, sr = load_mono(p)
        on = onset(m, sr)
        a = max(0, on - int(0.002 * sr))
        b = min(len(m), on + int(0.14 * sr))
        y, ed = process(m, sr, a, b, hp=180, fade_out=0.06)
        w.add(os.path.basename(p), y, sr, p, a, b, ed)
    return [w.close()]


def model_sets(raw):
    """Register the EP-model key noises (raw/TerrainModels/<Name>/noise/*.wav, owned by Waves Crate) as sets
    '<name>-on' / '<name>-off', using the README.txt the renderer writes (file  trig  key)."""
    n = []
    for d in sorted(glob.glob(os.path.join(raw, "TerrainModels", "*", "noise"))):
        inst = os.path.basename(os.path.dirname(d)).lower()
        files = sorted(glob.glob(os.path.join(d, "*.wav")))
        if not files:
            continue
        meta = {}
        rd = os.path.join(d, "README.txt")
        if os.path.exists(rd):
            for line in open(rd):
                # "file | trig | zone (lo-hi) | description" (epmodel's README table)
                t = [c.strip() for c in line.split("|")]
                if len(t) >= 3 and t[0].endswith(".wav"):
                    mz = re.search(r"(\d+)\s*-\s*(\d+)", t[2])
                    meta[t[0]] = [t[1]] + ([str((int(mz.group(1)) + int(mz.group(2))) // 2)] if mz else [])
        for trig in ("on", "off"):
            sel = [f for f in files if (meta.get(os.path.basename(f), [None])[0] == trig)
                   or (not meta and (("off" in os.path.basename(f) or "up" in os.path.basename(f)) == (trig == "off")))]
            if not sel:
                continue
            w = SetWriter(raw, f"model-{inst}-{trig}", f"{inst} key/action noise from the Waves Crate physical model "
                          f"(trig {trig}).", "terrain")
            for p in sel:
                m, sr = load_mono(p)
                key = None
                mt = meta.get(os.path.basename(p), [])
                for tok in mt[1:]:
                    if tok.isdigit():
                        key = int(tok)
                        break
                y, ed = process(m, sr, 0, len(m), fade_in=0.0, fade_out=0.01)
                w.add(os.path.basename(p), y, sr, p, 0, len(m), ed, key=key)
            n.append(w.close())
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default=DEFAULT_RAW)
    ap.add_argument("--only", nargs="*")
    a = ap.parse_args()
    os.makedirs(os.path.join(a.raw, "TerrainNoise"), exist_ok=True)
    jobs = {"piano": piano_sets, "sax": sax_sets, "breath": breath_set, "guitar": guitar_sets, "bow": bow_set,
            "knock": knock_set, "model": model_sets}
    for k, fn in jobs.items():
        if a.only and k not in a.only:
            continue
        print(k, fn(a.raw), flush=True)
    lic = os.path.join(a.raw, "TerrainNoise", "LICENSE.txt")
    with open(lic, "w") as f:
        f.write("TerrainNoise — mechanical noise one-shots extracted by Waves Crate (Tools/organics/noiselib.py, "
                f"{TODAY}) from cleared recordings. Each set's manifest.json names the licence and origin of every "
                "file.\n\n")
        for k, s in SRC.items():
            f.write(f"- {s['credit']} — {s['licence']} — {s['url']}\n")


if __name__ == "__main__":
    main()
