#!/usr/bin/env python3
"""wt_pages_gate.py — fb636 F1, the source half (no build).

wt_pages_cert.cpp proves the page door and the import lifetime by running them. What it cannot see is
WHO reads an import table in the plugin: a new message-thread reader that samples `importSlot_[o].live`
without a pin would read a table the bake worker can unmap under it (SIGSEGV since F1 — before F1 it
was a silent stale read, which is how the four existing ones went unnoticed until the F1 skeptic).
So this reads the source:

    python3 Tests/wt_pages_gate.py               # gate
    WTP_SRC_MUT=nostore python3 ...              # control: mipData_ back to std::vector<float>        -> red
    WTP_SRC_MUT=windows python3 ...              # control: windows.h in Wavetable.h (the fb515 law)   -> red
    WTP_SRC_MUT=unpin   python3 ...              # control: getOscLfoWaveJson samples a raw live load  -> red
    WTP_SRC_MUT=display python3 ...              # control: the waterfall bake resolves unpinned       -> red
    WTP_SRC_MUT=noclaim python3 ...              # control: a claim stops waiting out pins             -> red
    WTP_SRC_MUT=nofree  python3 ...              # control: the timer free stops skipping pins         -> red

Bars:
  [1] Wavetable's mipData_ and twinData_ are TableStore (the page-backed vector), and releaseStorage swaps
      both with a TableStore — the vector type is what routes a table through the page door.
  [2] PageBackedAllocator maps at >= kMapMinBytes (MAP_FAILED checked) and deallocate unmaps under the SAME
      test; Wavetable.h does not include windows.h (it reaches PluginEditor.cpp: the fb515 law).
  [3] The four off-audio samplers pin: getOscWavetableJson, getOscLfoWaveJson, setDistortionTableSrc and
      oscSourceSpec each construct an ImportRead and hold no raw live load; the waterfall bake resolves
      through the pinned wavetableForDisplay overload, and only the pointer-only callers (wtTableStamp,
      getOscNumFrames) use the unpinned one.
  [4] THE CENSUS. Every raw `importSlot_[..].live.load` sits in a function audited for it: the audio thread
      (processBlock's audition, wavetableForOsc — the grace fence) or pointer/epoch-only readers
      (getImportStateJson, rebuildHarmTableIfNeeded, rebuildMorphIfNeeded, the pointer-only
      wavetableForDisplay). A new one anywhere else is RED until someone audits it and adds it here.
  [5] claimImportBufLocked waits while a pin is out, freeRetiredImports skips a pinned buffer, and
      ImportRead re-loads `live` after it counts (the Dekker half that makes the pin a proof).
"""
import os, re, sys

HERE = os.path.dirname (os.path.abspath (__file__))
SRC  = os.path.join (HERE, "..", "Source")
MUT  = os.environ.get ("WTP_SRC_MUT", "")
passed = failed = 0

def chk (ok, label, detail):
    global passed, failed
    if ok: passed += 1
    else:  failed += 1
    print ("  %s  %s\n        %s" % ("PASS" if ok else "FAIL", label, detail))

def strip (s):
    """Comments, string and char literals out, so a brace or a name inside one counts for nothing."""
    s = re.sub (r'R"([^(\s]*)\(.*?\)\1"', '""', s, flags = re.S)
    s = re.sub (r"/\*.*?\*/", "", s, flags = re.S)
    s = re.sub (r"//[^\n]*", "", s)
    s = re.sub (r'"(?:\\.|[^"\\\n])*"', '""', s)
    return re.sub (r"'(?:\\.|[^'\\\n])'", "''", s)

def body_of (src, signature):
    """The brace-balanced body that follows `signature`."""
    at = src.find (signature)
    if at < 0: return ""
    b = src.find ("{", at)
    depth = 0
    for i in range (b, len (src)):
        if src[i] == "{": depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0: return src[b:i + 1]
    return ""

def mutate (text, old, new, what):
    if text.count (old) != 1:
        print ("  MUTATION %s: anchor found %d times — the control would test nothing" % (what, text.count (old)))
        sys.exit (2)
    return text.replace (old, new)

WT  = open (os.path.join (SRC, "Wavetable.h"),         encoding = "utf-8").read()
PH  = open (os.path.join (SRC, "PluginProcessor.h"),   encoding = "utf-8").read()
CPP = open (os.path.join (SRC, "PluginProcessor.cpp"), encoding = "utf-8").read()

if MUT == "nostore":
    WT  = mutate (WT, "TableStore           mipData_;", "std::vector<float>   mipData_;", MUT)
elif MUT == "windows":
    WT  = mutate (WT, "#include <juce_core/juce_core.h>", "#include <windows.h>\n#include <juce_core/juce_core.h>", MUT)
elif MUT == "unpin":
    CPP = mutate (CPP, "    const ImportRead pin (importSlot_[osc]);", "", MUT)
    CPP = mutate (CPP, "    const tw::Wavetable* wt = pin.wt;\n    if (wt == nullptr)\n    {\n        static const char* const WTP[4] = { ParameterIDs::SYN_OSC_A_WT_PRESET, ParameterIDs::SYN_OSC_B_WT_PRESET,\n                                            ParameterIDs::SYN_OSC_C_WT_PRESET, ParameterIDs::SYN_OSC_D_WT_PRESET };\n        const int bankPreset = (int) *apvts.getRawParameterValue (WTP[osc]);\n        wavetableBank.ensureBuilt (bankPreset);   // fb496 — message thread; this BAKES into an",
                        "    const tw::Wavetable* wt = importSlot_[osc].live.load (std::memory_order_acquire);\n    if (wt == nullptr)\n    {\n        static const char* const WTP[4] = { ParameterIDs::SYN_OSC_A_WT_PRESET, ParameterIDs::SYN_OSC_B_WT_PRESET,\n                                            ParameterIDs::SYN_OSC_C_WT_PRESET, ParameterIDs::SYN_OSC_D_WT_PRESET };\n        const int bankPreset = (int) *apvts.getRawParameterValue (WTP[osc]);\n        wavetableBank.ensureBuilt (bankPreset);   // fb496 — message thread; this BAKES into an", MUT)
elif MUT == "display":
    CPP = mutate (CPP, "wavetableForDisplay (mslot, wtPresetIdx, pin.wt)", "wavetableForDisplay (osc, mslot, wtPresetIdx)", MUT)
elif MUT == "noclaim":
    CPP = mutate (CPP, "\n           || slot.readers[idx].load (std::memory_order_seq_cst) != 0)", ")", MUT)
elif MUT == "nofree":
    CPP = mutate (CPP, "\n                && slot.readers[i].load (std::memory_order_seq_cst) == 0)", ")", MUT)
elif MUT:
    print ("  unknown WTP_SRC_MUT=%s" % MUT); sys.exit (2)

wt, ph, cpp = strip (WT), strip (PH), strip (CPP)
print ("══ wt_pages_gate — fb636 F1 (the source half)%s ══" % (" [MUT=%s]" % MUT if MUT else ""))

# [1] the vector type routes every table through the page door
store = re.search (r"using\s+TableStore\s*=\s*std::vector\s*<\s*float\s*,\s*PageBackedAllocator\s*<\s*float\s*>\s*>\s*;", wt)
mip   = re.search (r"\bTableStore\s+mipData_\s*;", wt)
twin  = re.search (r"\bTableStore\s+twinData_\s*;", wt)
rel   = body_of (wt, "void releaseStorage()")
relOk = re.search (r"TableStore\s*\(\s*\)\s*\.swap\s*\(\s*mipData_\s*\)", rel) and re.search (r"TableStore\s*\(\s*\)\s*\.swap\s*\(\s*twinData_\s*\)", rel)
chk (bool (store and mip and twin and relOk), "[1] mipData_ / twinData_ are TableStore and releaseStorage swaps both with one",
     "TableStore=%s mipData_=%s twinData_=%s releaseStorage=%s" % (bool (store), bool (mip), bool (twin), bool (relOk)))

# [2] the allocator's two doors agree, and windows.h stays out
alloc = body_of (wt, "T* allocate (std::size_t n)")
deal  = body_of (wt, "void deallocate (T* p, std::size_t n) noexcept")
mapOk = ("::mmap" in alloc and "MAP_FAILED" in alloc and re.search (r"bytes\s*>=\s*kMapMinBytes", alloc)
         and "::munmap" in deal and re.search (r"n\s*\*\s*sizeof\s*\(\s*T\s*\)\s*>=\s*kMapMinBytes", deal))
noWin = not re.search (r"#\s*include\s*<\s*windows\.h\s*>", wt, re.I)
chk (bool (mapOk) and noWin, "[2] mmap at >= kMapMinBytes (MAP_FAILED checked), munmap under the same test; no windows.h in Wavetable.h",
     "doors=%s windows.h-free=%s" % (bool (mapOk), noWin))

# [3] the four off-audio samplers pin
RAW = re.compile (r"importSlot_\s*\[[^\]]*\]\s*\.\s*live\s*\.\s*load")
sigs = { "getOscWavetableJson":   "juce::String TerrainAudioProcessor::getOscWavetableJson (int osc)",
         "getOscLfoWaveJson":     "juce::String TerrainAudioProcessor::getOscLfoWaveJson (int osc)",
         "setDistortionTableSrc": "void TerrainAudioProcessor::setDistortionTableSrc (int osc)",
         "oscSourceSpec":         "TerrainAudioProcessor::oscSourceSpec (int oscIdx, int preset," }
bad = []
for name, sig in sigs.items():
    b = body_of (cpp, sig)
    if not b: bad.append ("%s: body not found" % name); continue
    if not re.search (r"\bImportRead\s+\w+\s*\(\s*importSlot_\s*\[", b): bad.append ("%s: no ImportRead" % name)
    if RAW.search (b): bad.append ("%s: a raw live load" % name)
gb = body_of (cpp, sigs["getOscWavetableJson"])
if not re.search (r"wavetableForDisplay\s*\(\s*mslot\s*,\s*wtPresetIdx\s*,\s*pin\.wt\s*\)", gb): bad.append ("getOscWavetableJson: not the pinned overload")
unpinnedCallers = set()
for m in re.finditer (r"wavetableForDisplay\s*\(\s*osc\s*,", cpp):
    d = [x for x in re.finditer (r"^[^\n;{}]*\bTerrainAudioProcessor::(\w+)\s*\(", cpp[:m.start()], re.M)]
    unpinnedCallers.add (d[-1].group (1) if d else "?")
if unpinnedCallers != { "wtTableStamp", "getOscNumFrames" }: bad.append ("unpinned wavetableForDisplay callers %s" % sorted (unpinnedCallers))
chk (not bad, "[3] the four off-audio samplers take an ImportRead pin; the waterfall resolves through it",
     "; ".join (bad) if bad else "getOscWavetableJson · getOscLfoWaveJson · setDistortionTableSrc · oscSourceSpec pinned; unpinned overload only in wtTableStamp, getOscNumFrames")

# [4] the census of raw live loads
CPP_OK = { "processBlock", "getImportStateJson", "rebuildHarmTableIfNeeded", "rebuildMorphIfNeeded" }
H_OK   = { "wavetableForOsc", "wavetableForDisplay" }
where, stray = [], []
for m in RAW.finditer (cpp):
    d = [x for x in re.finditer (r"^[^\n;{}]*\bTerrainAudioProcessor::(\w+)\s*\(", cpp[:m.start()], re.M)]
    fn = d[-1].group (1) if d else "?"
    where.append (fn)
    if fn not in CPP_OK: stray.append ("PluginProcessor.cpp:%s" % fn)
for m in RAW.finditer (ph):
    d = [x for x in re.finditer (r"^\s*(?:const\s+)?[\w:]+\s*[*&]?\s*(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?noexcept\s*$", ph[:m.start()], re.M)]
    fn = d[-1].group (1) if d else "?"
    where.append (fn)
    if fn not in H_OK: stray.append ("PluginProcessor.h:%s" % fn)
chk (not stray, "[4] every raw importSlot_[..].live.load is in an audited function (audio, or pointer/epoch only)",
     ("UNAUDITED: " + ", ".join (stray)) if stray else "%d loads: %s" % (len (where), ", ".join (sorted (set (where)))))

# [5] the claim waits, the free skips, the pin re-checks
cl = body_of (cpp, "tw::Wavetable& TerrainAudioProcessor::claimImportBufLocked (ImportSlot& slot)")
fr = body_of (cpp, "void TerrainAudioProcessor::freeRetiredImports()")
rd = body_of (ph, "explicit ImportRead (ImportSlot& s) noexcept")
clOk = re.search (r"while\s*\(.*slot\.readers\s*\[\s*idx\s*\]\s*\.load\s*\(\s*std::memory_order_seq_cst\s*\)\s*!=\s*0", cl, re.S)
frOk = re.search (r"slot\.readers\s*\[\s*i\s*\]\s*\.load\s*\(\s*std::memory_order_seq_cst\s*\)\s*==\s*0\s*\)\s*\{[^}]*releaseStorage", fr, re.S)
rdOk = re.search (r"readers\s*\[\s*i\s*\]\s*\.fetch_add\s*\(\s*1\s*,\s*std::memory_order_seq_cst\s*\)\s*;\s*if\s*\(\s*s\.live\.load\s*\(\s*std::memory_order_seq_cst\s*\)\s*==\s*p\s*\)", rd)
chk (bool (clOk and frOk and rdOk), "[5] claimImportBufLocked waits out pins, freeRetiredImports skips them, ImportRead re-loads live after counting",
     "claim=%s free=%s pin=%s" % (bool (clOk), bool (frOk), bool (rdOk)))

print ("  %d pass / %d FAIL" % (passed, failed))
sys.exit (0 if failed == 0 else 1)
