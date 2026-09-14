#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb612 → fb638 — THE FACTORY WAVETABLE LIBRARY: 454 FLAC tables + 46 built-ins = 500. NO "TERRA".
#
#    python3 Tests/wt_factory_gate.py                                    # from anywhere
#    WTFAC_MUT=terra|allcaps|dupe|builtin|short|hostapp|noalias|md5 python3 …   # each must go RED
#
#  Max 2026-09-13: "I want 500 total factory wave tables … no more Terra when it comes to the name … just have
#  the name of the wave table and if it collides, then just rename it." And fb612's: "automatically installed on
#  EVERYONE'S COMPUTER" — Resources/Wavetables is copied into every bundle by CMake (wiped first since fb638).
#
#  THE RENAME MUST NOT COST A PRESET ITS TABLE. A preset saved before fb638 names
#  ref:1|<Category>/Terra - <Name>.flac|<hash of the file's bytes>. The 120 files were git-mv'd (bytes identical — the
#  md5s are recorded in Design/wavetables/renames_fb638.csv) and Source/WtFactoryAliases.h maps every legacy path;
#  the restore path consults it. Bars [7] and [8] prove both halves, the second against Max's REAL presets (read-only).
#
#  Reads the FLAC STREAMINFO header directly — no audio library needed, so the gate cannot silently skip.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys, glob, csv, hashlib, pathlib, collections

MUT  = os.environ.get("WTFAC_MUT", "")
ROOT = pathlib.Path(__file__).resolve().parent.parent
BANK = ROOT / "Resources" / "Wavetables"
CSVP = ROOT / "Design" / "wavetables" / "renames_fb638.csv"
HDR  = ROOT / "Source" / "WtFactoryAliases.h"
PROC = ROOT / "Source" / "PluginProcessor.cpp"
# the planned library, per category (existing + new — Design/wavetables/wtkit.py QUOTA_NEW + what shipped in fb612)
PLAN = {"Basic Shapes": 22, "Analog": 28, "Digital": 50, "Spectral": 46, "Vocal": 30, "Metallic": 30,
        "Physical": 24, "Harmonic": 24, "Cinematic": 28, "Chaos": 58, "Abstract": 48, "Processed": 42, "Textures": 24}
TARGET_FLAC, TARGET_ALL = sum(PLAN.values()), 500

PASS = FAIL = 0
def gate(ok, name, detail=""):
    global PASS, FAIL
    if ok: PASS += 1; print("  ✓ %s   %s" % (name, detail))
    else:  FAIL += 1; print("  ✗ %s   %s" % (name, detail))

def flac_total_samples(path):
    with open(path, "rb") as f:
        if f.read(4) != b"fLaC": return -1
        hdr = f.read(4)
        if len(hdr) < 4 or (hdr[0] & 0x7F) != 0: return -1
        si = f.read(34)
        if len(si) < 18: return -1
        return int.from_bytes(si[10:18], "big") & ((1 << 36) - 1)

def md5(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()

proc = PROC.read_text(encoding="utf-8")
m = re.search(r'SYN_OSC_A_WT_PRESET, 1 \},\s*"[^"]*",\s*juce::StringArray \{(.*?)\}\s*,', proc, re.S)
builtins = re.findall(r'"([^"]*)"', re.sub(r'//[^\n]*', '', m.group(1))) if m else []

files = sorted(glob.glob(str(BANK / "*" / "*.flac")))
names = [os.path.splitext(os.path.basename(f))[0] for f in files]
cats  = collections.Counter(os.path.basename(os.path.dirname(f)) for f in files)
if MUT == "terra":   names = names[:-1] + ["Terra - Zipper"]
if MUT == "allcaps": names = names[:-1] + ["ZIPPER STORM"]
if MUT == "dupe":    names = names[:-1] + [names[0]]
if MUT == "builtin": names = names[:-1] + [builtins[3] if len(builtins) > 3 else "Pulse"]

print("══ fb638 FACTORY WAVETABLE LIBRARY ══   mutation: %s" % (MUT or "(none)"))
print("  %d FLAC tables · %d categories · %.1f MB · %d built-ins" %
      (len(files), len(cats), sum(os.path.getsize(f) for f in files) / 1048576, len(builtins)))

# [1] the planned library
off = {c: (cats.get(c, 0), n) for c, n in PLAN.items() if cats.get(c, 0) != n}
extra = sorted(set(cats) - set(PLAN))
gate(len(files) == TARGET_FLAC and not off and not extra,
     "[1] %d FLAC TABLES IN THE THIRTEEN CATEGORIES, AS PLANNED" % TARGET_FLAC,
     ("all thirteen at plan" if not off and not extra else
      "off plan (have/plan): %s%s" % (", ".join("%s %d/%d" % (c, h, n) for c, (h, n) in off.items()),
                                      ("   unknown folders: %s" % extra) if extra else "")))

# [2] names read like names — no brand prefix, not shouting
terra  = [n for n in names if re.match(r'(?i)terra\b', n)]
shouty = [n for n in names if n == n.upper() and any(ch.isalpha() for ch in n)]
lower  = [n for n in names if n[:1].islower()]
gate(not terra and not shouty and not lower,
     "[2] NO \"TERRA\" IN ANY NAME; RESPECTIVE CAPITALS, NOT ALL CAPS",
     "e.g. %s" % ", ".join(sorted(names)[:3]) if not (terra or shouty or lower) else
     ("TERRA: %s  " % terra[:3] if terra else "") + ("ALL CAPS: %s  " % shouty[:3] if shouty else "") +
     ("lower-case: %s" % lower[:3] if lower else ""))
bterra = [b for b in builtins if re.match(r'(?i)terra\b', b)]
gate(not bterra, "[2b] NO BUILT-IN IS CALLED \"TERRA …\"", "all %d plain" % len(builtins) if not bterra else "still: %s" % bterra[:4])

# [3] every name unique — among the FLACs and against the built-ins (the browser lists NAMES)
cf = collections.Counter(n.casefold() for n in names)
dupes = sorted(n for n, c in cf.items() if c > 1)
bset = {b.casefold() for b in builtins}
vsb = sorted({n for n in names if n.casefold() in bset})
gate(not dupes and not vsb, "[3] NO TWO TABLES SHARE A NAME — FLAC OR BUILT-IN",
     "all %d + %d distinct" % (len(names), len(builtins)) if not (dupes or vsb) else
     ("DUPLICATES: %s  " % dupes[:3] if dupes else "") + ("= A BUILT-IN: %s" % vsb[:3] if vsb else ""))

# [4] frame-perfect length
wrong = [(os.path.basename(f), n) for f in files for n in [flac_total_samples(f)] if n != 128 * 2048]
if MUT == "short" and files: wrong.append((os.path.basename(files[0]), 12345))
gate(not wrong, "[4] EVERY FILE IS EXACTLY 128 FRAMES x 2048 (the importer's frame-perfect path)",
     "all %d" % len(files) if not wrong else "WRONG LENGTH: %s" % wrong[:2])

# [5] found from the plugin binary, not the host
body = proc[proc.index("juce::File wtFactoryRoot()"):]
body = body[:body.index("\n    }\n") + 6]
if MUT == "hostapp": body = body.replace("currentExecutableFile", "currentApplicationFile")
gate("currentExecutableFile" in body and "currentApplicationFile" not in body,
     "[5] THE BANK IS FOUND FROM THE PLUGIN BINARY, NOT FROM THE HOST APP", "currentExecutableFile"
     if "currentApplicationFile" not in body else "*** currentApplicationFile is the HOST ***")

# [6] a built bundle carries exactly the library — and no stale pre-fb638 file
art = ROOT.parent.parent / "build" / "plugins" / "Terrain" / "Terrain_artefacts" / "Release"
built = []
for rel in ("VST3/Terrain.vst3/Contents/Resources/Wavetables", "AU/Terrain.component/Contents/Resources/Wavetables"):
    d = art / rel
    if d.is_dir():
        fl = glob.glob(str(d / "*" / "*.flac"))
        built.append((rel.split("/")[0], len(fl), sum(1 for x in fl if os.path.basename(x).startswith("Terra - "))))
if built:
    gate(all(n == len(files) and st == 0 for _, n, st in built),
         "[6] THE BUILT BUNDLES CARRY EXACTLY THE LIBRARY, NOTHING STALE",
         " · ".join("%s %d flac%s" % (k, n, (" (%d STALE 'Terra - ')" % st) if st else "") for k, n, st in built))
else:
    print("  – [6] no built artefacts to check (run a build first)")

# [7] the rename is preset-safe: bytes identical, and the C++ alias table is exactly the recorded map
rows = list(csv.DictReader(open(CSVP))) if CSVP.exists() else []
hdr = HDR.read_text(encoding="utf-8") if HDR.exists() else ""
pairs = re.findall(r'\{ "([^"]+)", "([^"]+)" \}', hdr)
if MUT == "noalias" and pairs: pairs = pairs[1:]
if MUT == "md5" and rows: rows[0] = dict(rows[0], md5="0" * 32)
missing = [r['current'] for r in rows if not (BANK / r['current']).exists()]
badmd5  = [r['current'] for r in rows if (BANK / r['current']).exists() and md5(BANK / r['current']) != r['md5']]
csvmap  = {(r['legacy'], r['current']) for r in rows}
hdrdiff = csvmap.symmetric_difference(set(pairs))
restore = proc[proc.find("tw::asset::parseRef (neu, rel, want)"):][:3000]
gate(len(rows) == 120 and not missing and not badmd5 and not hdrdiff and "tw::wtalias::currentFor (rel)" in restore,
     "[7] THE 120 RENAMES ARE PRESET-SAFE — BYTES IDENTICAL, EVERY LEGACY PATH MAPPED, THE RESTORE ASKS THE MAP",
     "120 mapped · md5 identical · header == csv · restore consults it" if not (missing or badmd5 or hdrdiff) and len(rows) == 120
     else "rows %d · missing %s · md5 changed %s · header≠csv %d%s" % (len(rows), missing[:2], badmd5[:2], len(hdrdiff),
          "" if "tw::wtalias::currentFor (rel)" in restore else " · RESTORE DOES NOT CONSULT THE MAP"))

# [8] Max's REAL presets still find their tables (read-only; skipped where there are none, e.g. CI)
amap = {a.casefold(): b for a, b in pairs}
home = pathlib.Path.home() / "Library" / "WavesCrate"
pres = sorted(set(glob.glob(str(home / "*" / "Banks" / "User" / "*.terrain"))))
refs = collections.Counter()
for p in pres:
    for r in re.findall(rb"ref:1\|([^|\x00]{1,200})\|", open(p, "rb").read()):
        refs[r.decode("utf-8", "replace")] += 1
lost = [r for r in refs if not (BANK / r).exists() and not (amap.get(r.casefold()) and (BANK / amap[r.casefold()]).exists())]
if pres:
    gate(not lost, "[8] EVERY FACTORY TABLE MAX'S PRESETS NAME STILL RESOLVES",
         "%d presets · %d distinct factory refs · all resolve (%d via the alias map)" %
         (len(pres), len(refs), sum(1 for r in refs if not (BANK / r).exists())) if not lost else "LOST: %s" % lost[:4])
else:
    print("  – [8] no user presets on this machine (CI) — the alias map is still proven by [7]")

# [9] the five hundred
gate(len(files) + len(builtins) == TARGET_ALL, "[9] %d FLAC + %d BUILT-INS = %d" % (len(files), len(builtins), TARGET_ALL),
     "exactly the five hundred" if len(files) + len(builtins) == TARGET_ALL else "have %d" % (len(files) + len(builtins)))

print("\n  %d pass, %d fail%s" % (PASS, FAIL, ("   (mutation: %s)" % MUT) if MUT else ""))
sys.exit(1 if FAIL else 0)
