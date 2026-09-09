#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb612 — THE FACTORY WAVETABLE LIBRARY SHIPS WITH THE PLUGIN.
#
#    python3 Tests/wt_factory_gate.py                       # from plugins/Terrain
#    WTFAC_MUT=allcaps|nodash|dupe|short|hostapp python3 …   # each must go RED
#
#  Max: "we may have to make terra a factory lib. everything respective capitals, not ALL CAPS.
#  preset names 'Terra - (Name)' just so we have organization, the dash will give us that…
#  automatically installed on EVERYONE'S COMPUTER."
#
#  120 tables, ten categories, 24-bit FLAC (120.0 MB of float32 .wav -> 61.1 MB; worst round-trip
#  -138.5 dBr measured across all 120). They live in Resources/Wavetables and CMake copies them
#  into every bundle, so installing the plugin installs the library.
#
#  This reads the FLAC STREAMINFO header directly rather than importing soundfile — the gate must
#  run on a machine that has no audio libraries, and a gate that silently skips is not a gate.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys, glob, struct, pathlib, collections

MUT  = os.environ.get("WTFAC_MUT", "")
ROOT = pathlib.Path(__file__).resolve().parent.parent
BANK = ROOT / "Resources" / "Wavetables"
TEN  = {"Analog","Basic Shapes","Chaos","Cinematic","Digital","Harmonic","Metallic","Physical","Spectral","Vocal"}

PASS = FAIL = 0
def gate(ok, name, detail=""):
    global PASS, FAIL
    if ok: PASS += 1; print("  ✓ %s   %s" % (name, detail))
    else:  FAIL += 1; print("  ✗ %s   %s" % (name, detail))

def flac_total_samples(path):
    """STREAMINFO is always the first metadata block: 'fLaC', then a 4-byte block header, then 34
       bytes whose bits 108..143 are the total sample count."""
    with open(path, "rb") as f:
        if f.read(4) != b"fLaC": return -1
        hdr = f.read(4)
        if len(hdr) < 4 or (hdr[0] & 0x7F) != 0: return -1
        si = f.read(34)
        if len(si) < 18: return -1
        # bytes 10..17 hold: sample rate (20b) | channels (3b) | bits (5b) | total samples (36b)
        v = int.from_bytes(si[10:18], "big")
        return v & ((1 << 36) - 1)

files = sorted(glob.glob(str(BANK / "*" / "*.flac")))
names = [os.path.splitext(os.path.basename(f))[0] for f in files]
cats  = sorted({os.path.basename(os.path.dirname(f)) for f in files})
if MUT == "allcaps": names = names[:-1] + ["TERRA ZIPPER"]
if MUT == "nodash":  names = names[:-1] + ["Terra Zipper"]
if MUT == "dupe":    names = names[:-1] + [names[0]]

print("══ fb612 FACTORY WAVETABLE LIBRARY ══   mutation: %s" % (MUT or "(none)"))
print("  %d tables · %d categories · %.1f MB" %
      (len(files), len(cats), sum(os.path.getsize(f) for f in files) / 1048576))

gate(len(files) == 120 and set(cats) == TEN,
     "[1] 120 TABLES IN THE TEN CATEGORIES, SHIPPED IN THE REPO",
     " · ".join("%s %d" % (c, sum(1 for f in files if os.path.basename(os.path.dirname(f)) == c)) for c in cats))

bad_case = [n for n in names if not n.startswith("Terra - ")]
shouty   = [n for n in names if n == n.upper()]
gate(not bad_case and not shouty,
     "[2] EVERY NAME IS \"Terra - Something\", RESPECTIVE CAPITALS, NOT ALL CAPS",
     "e.g. %s" % ", ".join(sorted(names)[:3]) if not (bad_case or shouty)
     else ("no \"Terra - \" prefix: %s   " % bad_case[:3] if bad_case else "") + ("STILL ALL CAPS: %s" % shouty[:3] if shouty else ""))

dupes = [n for n, c in collections.Counter(names).items() if c > 1]
gate(not dupes,
     "[3] NO TWO TABLES SHARE A NAME",
     "all 120 distinct" if not dupes else
     "COLLIDING: %s   ← the browser lists names, so a duplicate is unpickable" % dupes[:3])

wrong = []
for f in files:
    n = flac_total_samples(f)
    if n != 128 * 2048: wrong.append((os.path.basename(f), n))
if MUT == "short" and files: wrong.append((os.path.basename(files[0]), 12345))
gate(not wrong,
     "[4] EVERY FILE IS EXACTLY 128 FRAMES x 2048 (what makes importIsFile_ take the frame-perfect path)",
     "128 x 2048 = 262144 samples, all 120" if not wrong
     else "WRONG LENGTH: %s   ← anything else falls back to the 40-frame resolution mode" % wrong[:2])

# ── [5] the resolver must ask where the PLUGIN is, not where the HOST is ──────────────────────
proc = (ROOT / "Source" / "PluginProcessor.cpp").read_text()
body = proc[proc.index("juce::File wtFactoryRoot()"):]
body = body[:body.index("\n    }\n") + 6]
if MUT == "hostapp": body = body.replace("currentExecutableFile", "currentApplicationFile")
gate("currentExecutableFile" in body and "currentApplicationFile" not in body,
     "[5] THE BANK IS FOUND FROM THE PLUGIN BINARY, NOT FROM THE HOST APP",
     "currentExecutableFile" if "currentExecutableFile" in body and "currentApplicationFile" not in body
     else "*** currentApplicationFile is the HOST — that would look for the bank inside Ableton ***")

# ── [6] and a built bundle really carries it ──────────────────────────────────────────────────
art = ROOT.parent.parent / "build" / "plugins" / "Terrain" / "Terrain_artefacts" / "Release"
built = []
for rel in ("VST3/Terrain.vst3/Contents/Resources/Wavetables", "AU/Terrain.component/Contents/Resources/Wavetables"):
    d = art / rel
    built.append((rel.split("/")[0], len(glob.glob(str(d / "*" / "*.flac"))) if d.is_dir() else -1))
if any(n >= 0 for _, n in built):
    gate(all(n == 120 for _, n in built if n >= 0),
         "[6] THE BUILT BUNDLES CARRY THE LIBRARY",
         " · ".join("%s %s" % (k, ("%d flac" % n) if n >= 0 else "not built") for k, n in built))
else:
    print("  – [6] no built artefacts to check (run a build first)")

print("\n  %d pass, %d fail%s" % (PASS, FAIL, ("   (mutation: %s)" % MUT) if MUT else ""))
sys.exit(1 if FAIL else 0)
