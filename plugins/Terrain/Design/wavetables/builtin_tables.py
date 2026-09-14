#!/usr/bin/env python3
"""
builtin_tables.py — the 46 BUILT-IN tables, on the same footing as every generated table.

builtin_probe.cpp bakes each WavetableBank preset through the plugin's own code and dumps
<dir>/<idx>.wav + <dir>/index.json. This module loads that dump and presents each built-in as a
128-frame, wtlib.finalize()d table, so wtlib.fingerprint / distance / measure compare a new table
against the built-ins exactly the way gate.py compares it against the FLAC bank.

    python3 builtin_tables.py <dump-dir>        # verify the dump + print the board; exit 1 on failure
    import builtin_tables
    T = builtin_tables.load(<dump-dir>)         # [{idx, name, category, frames, raw, table}]

The dump dir may also come from $TERRAIN_WT_BUILTINS. (Not named builtins.py: that is a stdlib
module and would shadow this file on import.)

RESAMPLING 16 -> 128 FRAMES is the plugin's own frame read, not an invented smoothing law.
Wavetable::renderBlend at blur 0 (== lookup) plays WT POS p as a linear crossfade between frame
floor(p*(F-1)) and the next, in float32. resample() evaluates that same law at the 128 evenly
spaced positions k/127, so resampled frame k is what a voice plays with WT POS = k/127. The two end
frames are the dumped end frames exactly; interior raw frames are generally NOT sampled exactly, so
the board also prints the raw 16-frame harm60/span beside the resampled ones.
"""
import os, sys, json, glob
import numpy as np
import soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wtlib

N_BUILTIN = 46
KEYS = {"idx", "name", "category", "frames"}
# The categories a built-in can be filed under: PluginProcessor.cpp kWtCats[] — the merged ten plus
# the three fb638 appended (the probe parses the live list; this is only the sanity set).
TEN = {"Basic Shapes", "Analog", "Digital", "Vocal", "Metallic", "Spectral", "Chaos",
       "Cinematic", "Harmonic", "Physical", "Abstract", "Processed", "Textures"}


def dump_dir(d=None):
    d = d or os.environ.get("TERRAIN_WT_BUILTINS")
    if not d:
        raise SystemExit("builtin_tables: give the builtin_probe dump dir (argument or $TERRAIN_WT_BUILTINS)")
    return d


def resample(raw, n=wtlib.FRAMES):
    """(F, 2048) -> (n, 2048) by renderBlend's blur-0 law, in float32, at WT POS = k/(n-1)."""
    raw = np.asarray(raw, dtype=np.float32)
    F = raw.shape[0]
    out = np.empty((n, raw.shape[1]), dtype=np.float32)
    one = np.float32(1.0)
    for k in range(n):
        pos  = np.float32(k) / np.float32(n - 1)
        fidx = np.float32(pos * np.float32(F - 1))
        f0 = min(int(fidx), F - 1)
        f1 = f0 + 1 if f0 < F - 1 else f0
        fr = np.float32(fidx - np.float32(f0))
        out[k] = raw[f0] * (one - fr) + raw[f1] * fr
    return out


def read_raw(d, idx):
    x, sr = sf.read(os.path.join(d, "%d.wav" % idx), dtype="float32", always_2d=False)
    return x.reshape(-1, wtlib.SIZE), sr


def load(d=None):
    """Every built-in as {idx, name, category, frames, raw (F,2048), table (128,2048) finalized}."""
    d = dump_dir(d)
    with open(os.path.join(d, "index.json")) as f:
        index = json.load(f)
    out = []
    for e in index:
        raw, _ = read_raw(d, e["idx"])
        out.append(dict(e, raw=raw, table=wtlib.finalize(resample(raw))))
    return out


def main(argv):
    d = dump_dir(argv[1] if len(argv) > 1 else None)
    problems = []
    def need(cond, msg):
        if not cond: problems.append(msg)

    with open(os.path.join(d, "index.json")) as f:
        index = json.load(f)
    need(len(index) == N_BUILTIN, "index.json has %d entries, want %d" % (len(index), N_BUILTIN))
    need([e.get("idx") for e in index] == list(range(N_BUILTIN)), "index.json idx is not 0..45 in order")
    need(all(set(e) == KEYS for e in index), "index.json entries must carry exactly %s" % sorted(KEYS))
    names = [e["name"] for e in index]
    need(len(set(names)) == len(names), "duplicate built-in names")
    need(all(e["category"] in TEN for e in index), "a built-in category is not one of the ten")
    wavs = sorted(glob.glob(os.path.join(d, "*.wav")))
    need(len(wavs) == N_BUILTIN, "%d .wav files in %s, want %d" % (len(wavs), d, N_BUILTIN))
    need({os.path.basename(p) for p in wavs} == {"%d.wav" % i for i in range(N_BUILTIN)},
         ".wav names are not exactly 0.wav..45.wav")

    print("\n══ BUILT-IN TABLES ══  %s\n" % d)
    hdr = "%3s  %-16s %-13s %6s %7s %8s %9s %8s %9s" % (
        "idx", "name", "category", "frames", "harm60", "span st", "minpk dB", "h60@raw", "span@raw")
    print(hdr); print("-" * len(hdr))
    rows = []
    for e in index:
        i = e["idx"]; path = os.path.join(d, "%d.wav" % i)
        if not os.path.isfile(path):
            need(False, "%d.wav is missing" % i); continue
        info = sf.info(path)
        need(info.samplerate == 44100 and info.channels == 1 and info.subtype == "FLOAT"
             and info.format == "WAV", "%d.wav is not float32 mono 44.1 kHz WAV (%s)" % (i, info))
        raw, _ = read_raw(d, i)
        ns = raw.size
        need(ns >= 4096 and ns % 2048 == 0 and ns // 2048 <= 256,
             "%d.wav fails the importer condition (n=%d)" % (i, ns))
        need(raw.shape[0] == e["frames"], "%d.wav holds %d frames, index says %d" % (i, raw.shape[0], e["frames"]))
        need(bool(np.isfinite(raw).all()), "%d.wav has non-finite samples" % i)
        fpk = np.abs(raw).max(axis=1)
        need(bool((fpk > 1e-6).all()), "%d.wav has %d silent frame(s)" % (i, int((fpk <= 1e-6).sum())))
        tab = wtlib.finalize(resample(raw))
        m, mr = wtlib.measure(tab), wtlib.measure(raw)
        need(m["dead"] == 0, "%d resampled has %d dead frames" % (i, m["dead"]))
        rows.append((e, m, mr, int((fpk <= 1e-6).sum())))
        print("%3d  %-16s %-13s %6d %7d %8.1f %9.1f %8d %9.1f" % (
            i, e["name"], e["category"], e["frames"], m["harm60"], m["span"],
            20 * np.log10(max(float(fpk.min()), 1e-30)), mr["harm60"], mr["span"]))

    h = np.array([m["harm60"] for _, m, _, _ in rows]); s = np.array([m["span"] for _, m, _, _ in rows])
    fr = sorted({e["frames"] for e, _, _, _ in rows})
    print("-" * len(hdr))
    print("%d tables · frames per table %s · harm60 min %d / mean %d / max %d · span median %.1f st (min %.1f, max %.1f)"
          % (len(rows), fr, h.min(), int(h.mean()), h.max(), float(np.median(s)), s.min(), s.max()))
    print("silent tables: %d · silent raw frames: %d · dead resampled frames: %d" % (
        sum(1 for r in rows if r[3] > 0), sum(r[3] for r in rows), sum(r[1]["dead"] for r in rows)))
    if problems:
        print("\nFAIL (%d):" % len(problems))
        for p in problems: print("   " + p)
        return 1
    print("\nPASS — %d files, %d index entries, every frame non-silent and finite, importer condition holds\n"
          % (len(wavs), len(index)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
