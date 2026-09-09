#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb611 — A WAVETABLE FILE MAY NOT BE ON THIS DISK, AND THE READ MUST NOT BLOCK THE UI.
#
#    python3 Tests/wt_locality_gate.py                    # from plugins/Terrain
#    WTLOC_MUT=inline|nopool|nodataless python3 …          # each must go RED
#
#  Max: "factory is solid now but the user folder like TERRA does the same thing."
#  The two folders hold the SAME 120 files, byte for byte (1,048,632 B each), and the pick calls
#  the identical native with an absolute path either way. MEASURED, which is the only reason this
#  was ever found:
#        ~/Library/WavesCrate/.../Factory   local            3.81 ms/file
#        ~/Desktop/TERRAIN-WAVETABLES       iCloud-evicted   922.92 ms/file   (105 of 120 dataless)
#  iCloud "Optimise Mac Storage" had evicted the bodies. `stat` works, the scan lists every name,
#  and the first read blocks for ~1 s while it downloads.
#
#  NO PLUGIN CAN MAKE AN ABSENT FILE ARRIVE FASTER. What IS ours: that read used to run on the
#  MESSAGE THREAD, so the wait was a frozen UI you could not tell from a hang. These bars pin the
#  fix, and the last one REPORTS locality so a future "it's slow again" starts from evidence.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys, json, time, pathlib, subprocess

MUT  = os.environ.get("WTLOC_MUT", "")
ROOT = pathlib.Path(__file__).resolve().parent.parent
ED   = (ROOT / "Source" / "PluginEditor.cpp").read_text()
PR   = (ROOT / "Source" / "PluginProcessor.cpp").read_text()

PASS = FAIL = 0
def gate(ok, name, detail=""):
    global PASS, FAIL
    if ok: PASS += 1; print("  ✓ %s   %s" % (name, detail))
    else:  FAIL += 1; print("  ✗ %s   %s" % (name, detail))

def native(src, name):
    i = src.index('.withNativeFunction("%s"' % name)
    j = src.index('.withNativeFunction(', i + 40)
    return src[i:j]

print("══ fb611 WAVETABLE FILE LOCALITY ══   mutation: %s" % (MUT or "(none)"))

# ── [1] neither native reads the file inline any more ─────────────────────────────────────────
bad = []
for n in ("loadWavetableByPath", "loadImportedWavetable"):
    body = native(ED, n)
    if MUT == "inline": body += "\n reader->read (&buf, 0, n, 0, true, true);\n"
    if "->read (" in body or "createReaderFor" in body: bad.append(n)
gate(not bad,
     "[1] NEITHER LOADER READS THE FILE ON THE MESSAGE THREAD",
     "loadWavetableByPath · loadImportedWavetable both hand off" if not bad
     else "STILL BLOCKING: " + ", ".join(bad) + "   ← a cloud-evicted file freezes the UI for ~1 s")

# ── [2] and both go through the async path ────────────────────────────────────────────────────
routed = all("loadWavetableFileAsync" in native(ED, n) for n in ("loadWavetableByPath", "loadImportedWavetable"))
impl   = PR[PR.index("void TerrainAudioProcessor::loadWavetableFileAsync"):] if "void TerrainAudioProcessor::loadWavetableFileAsync" in PR else ""
impl   = impl[:impl.index("\n}\n")] if "\n}\n" in impl else impl
if MUT == "nopool": impl = impl.replace("wtIoPool_", "XX")
pooled = ("wtIoPool_" in impl) and ("callAsync" in impl)
gate(routed and pooled,
     "[2] THE READ RUNS ON ITS OWN WORKER AND HANDS THE PCM BACK ON THE MESSAGE THREAD",
     "routed=%s  pool+callAsync=%s%s" % (routed, pooled,
        "" if pooled else "   ← importedPcm_ is message-thread state; a pool-thread write would race setImportFrames"))

# ── [3] the wait is named, not guessed ────────────────────────────────────────────────────────
dl = PR[PR.index("bool TerrainAudioProcessor::fileIsDataless"):] if "bool TerrainAudioProcessor::fileIsDataless" in PR else ""
dl = dl[:dl.index("\n}\n")] if "\n}\n" in dl else dl
if MUT == "nodataless": dl = ""
announced = "onWavetableFetching" in native(ED, "loadWavetableByPath")
gate(("SF_DATALESS" in dl) and announced,
     "[3] A FILE STILL IN THE CLOUD IS DETECTED FROM ITS OWN FLAG AND ANNOUNCED BEFORE THE READ",
     "SF_DATALESS read=%s  onWavetableFetching fired=%s%s" % ("SF_DATALESS" in dl, announced,
        "" if ("SF_DATALESS" in dl) else "   ← without the flag the only signal is 'it felt slow'"))

# ── [4] LOCALITY REPORT — the measurement that found this, kept runnable ──────────────────────
def dataless_count(root):
    try:
        out = subprocess.run(["find", root, "-name", "*.wav", "-flags", "+dataless"],
                             capture_output=True, text=True, timeout=30).stdout
        return len([l for l in out.splitlines() if l.strip()])
    except Exception: return -1
def wav_count(root):
    try:
        out = subprocess.run(["find", root, "-name", "*.wav"], capture_output=True, text=True, timeout=30).stdout
        return len([l for l in out.splitlines() if l.strip()])
    except Exception: return -1

HOME = os.path.expanduser("~")
roots = []
fact = os.path.join(HOME, "Library/WavesCrate/TerrainInstrument/Wavetables/Factory")
if not os.path.isdir(fact): fact = os.path.join(HOME, "Library/WavesCrate/Terrain/Wavetables/Factory")
if os.path.isdir(fact): roots.append(("FACTORY", fact))
for reg in ("Library/WavesCrate/TerrainInstrument/imports-wavetable.json",
            "Library/WavesCrate/Terrain/imports-wavetable.json"):
    p = os.path.join(HOME, reg)
    if os.path.isfile(p):
        try:
            for f in json.load(open(p)).get("folders", []):
                if os.path.isdir(f): roots.append(("user", f))
        except Exception: pass

print("\n  locality of every wavetable root this Mac has registered:")
factory_evicted = 0
for kind, r in roots:
    n, d = wav_count(r), dataless_count(r)
    if kind == "FACTORY" and d > 0: factory_evicted = d
    note = ("all local" if d == 0 else
            "⚠️  %d of %d EVICTED — every one of those costs ~1 s on its first read" % (d, n))
    print("    %-8s %-58s %4d wav  %s" % (kind, ("…" + r[-56:]) if len(r) > 57 else r, n, note))
if not roots: print("    (no wavetable roots found on this machine)")

gate(factory_evicted == 0,
     "[4] THE SHIPPED FACTORY BANK IS ON LOCAL DISK",
     "nothing evicted" if factory_evicted == 0
     else "*** %d factory tables are cloud-evicted — the bank we install must never be ***" % factory_evicted)

print("\n  %d pass, %d fail%s" % (PASS, FAIL, ("   (mutation: %s)" % MUT) if MUT else ""))
sys.exit(1 if FAIL else 0)
