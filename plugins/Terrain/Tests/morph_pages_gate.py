#!/usr/bin/env python3
"""morph_pages_gate.py — fb636 F4, the source half (no build).

morph_pages_cert.cpp proves the morph-buffer lifetime by running the sliced shipping code. What it cannot
see is the plugin AROUND those functions: who calls the free and when, who else reaches a MorphSlot, and
whether the audio half of the fence is still seq_cst (a release/acquire pair passes every run on Apple
silicon and is still not the Dekker pairing importGraceOver needs). So this reads the source:

    python3 Tests/morph_pages_gate.py                # gate
    MPG_SRC_MUT=stampeach python3 ...                # control: the None branch stamps both buffers every tick  -> [2] red
    MPG_SRC_MUT=direct    python3 ...                # control: the None branch stores nullptr itself          -> [2] red
    MPG_SRC_MUT=acquire   python3 ...                # control: wavetableForOsc's morph load back to acquire   -> [3] red
    MPG_SRC_MUT=nograce   python3 ...                # control: the free stops waiting out the audio block     -> [4] red
    MPG_SRC_MUT=cooldown  python3 ...                # control: the free writes retireCooldown                 -> [4] red
    MPG_SRC_MUT=noclaim   python3 ...                # control: a rebuild stops claiming its target            -> [5] red
    MPG_SRC_MUT=early     python3 ...                # control: the free runs BEFORE the four rebuilds         -> [1] red
    MPG_SRC_MUT=census    python3 ...                # control: a new function reads a MorphSlot               -> [6] red
    MPG_SRC_MUT=extrafree python3 ...                # control: a second door frees a morph buffer             -> [7] red

Bars:
  [1] MorphSlot carries pending[2] / retiredAt[2] / retiredMs[2]; timerCallback calls freeRetiredMorphs exactly
      once, AFTER the four rebuildMorphIfNeeded calls (a buffer a rebuild claims keeps its pages), and it is
      called nowhere else; processBlock never publishes, rebuilds or frees a morph.
  [2] Every morph publish goes through publishMorph: it stores `live` seq_cst, returns on `was == nullptr ||
      was == to` BEFORE any stamp (the None branch runs every tick — the F4 skeptic's first trap) and loads
      audioSeq_ seq_cst AFTER the store. rebuildMorphIfNeeded stores no `live` itself, makes exactly the two
      publishes (nullptr, &slot.buf[target]) and stamps nothing.
  [3] The fence's audio half: every `live` load in wavetableForOsc and resolveMorphTable is seq_cst.
  [4] freeRetiredMorphs frees only a pending, non-live buffer the audio thread is not reporting, past the hold
      AND importGraceOver — and writes nothing but `pending`: not retireCooldown, buildIdx, ready[], the built*
      keys, live, audioReadingIdx or the stamps (WHEN the next rebuild runs, and where, stays what it was).
  [5] The claim: rebuildMorphIfNeeded clears pending[target] before it marks the target not-ready and builds.
  [6] THE CENSUS. Every function that takes a MorphSlot& or names morphA_..morphD_ is audited: the audio read
      (processBlock -> wavetableForOsc; resolveMorphTable, uncalled), the message thread's display readers
      (wtTableStamp, getOscNumFrames, getOscWavetableJson -> wavetableForDisplay) and the timer's rebuild /
      publish / free (timerCallback, rebuildMorphIfNeeded, publishMorph, freeRetiredMorphs). The free is safe
      against the message-thread readers only because they share its thread; a new reader anywhere else is RED
      until someone audits it and adds it here. PluginEditor.cpp names none.
  [7] releaseStorage has exactly two callers in Source: freeRetiredImports and freeRetiredMorphs.
"""
import os, re, sys

HERE = os.path.dirname (os.path.abspath (__file__))
SRC  = os.path.join (HERE, "..", "Source")
MUT  = os.environ.get ("MPG_SRC_MUT", "")
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

def span_of (src, at):
    """(open, close) of the brace-balanced body whose '{' is the first at or after `at`."""
    b = src.find ("{", at)
    if b < 0: return None
    depth = 0
    for i in range (b, len (src)):
        if src[i] == "{": depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0: return (b, i)
    return None

def body_of (src, signature):
    at = src.find (signature)
    if at < 0: return ""
    sp = span_of (src, at)
    return src[sp[0]:sp[1] + 1] if sp else ""

def mutate (text, old, new, what):
    if text.count (old) != 1:
        print ("  MUTATION %s: anchor found %d times — the control would test nothing" % (what, text.count (old)))
        sys.exit (2)
    return text.replace (old, new)

def defs (cpp):
    """[(name, open, close)] for every out-of-line TerrainAudioProcessor:: definition (a call statement has a
    ';' before its next '{', so it never matches)."""
    out = []
    for m in re.finditer (r"^[^\n;{}]*\bTerrainAudioProcessor::(\w+)\s*\([^;{}]*\)[^;{}]*\{", cpp, re.M):
        sp = span_of (cpp, m.end() - 1)
        if sp: out.append ((m.group (1), sp[0], sp[1]))
    return out

def enclosing (D, pos):
    inside = [d for d in D if d[1] <= pos <= d[2]]
    return min (inside, key = lambda d: d[2] - d[1])[0] if inside else "?"

def read (name): return open (os.path.join (SRC, name), encoding = "utf-8").read()
PH, CPP, ED = read ("PluginProcessor.h"), read ("PluginProcessor.cpp"), read ("PluginEditor.cpp")
OTHER = {}
for fn in sorted (os.listdir (SRC)):
    if fn.endswith ((".h", ".hpp", ".cpp", ".mm")) and fn not in ("PluginProcessor.h", "PluginProcessor.cpp", "PluginEditor.cpp"):
        OTHER[fn] = read (fn)

NONE_PUB = "        publishMorph (slot, nullptr);"
if MUT == "stampeach":
    CPP = mutate (CPP, NONE_PUB, NONE_PUB + " for (int i = 0; i < 2; ++i) { slot.retiredAt[i] = audioSeq_.load (std::memory_order_seq_cst);"
                  " slot.retiredMs[i] = juce::Time::getMillisecondCounter(); slot.pending[i] = true; }", MUT)
elif MUT == "direct":
    CPP = mutate (CPP, NONE_PUB, "        slot.live.store (nullptr, std::memory_order_release);", MUT)
elif MUT == "acquire":
    PH  = mutate (PH, "if (auto* m = slot.live.load (std::memory_order_seq_cst))", "if (auto* m = slot.live.load (std::memory_order_acquire))", MUT)
elif MUT == "nograce":
    CPP = mutate (CPP, "\n                && importGraceOver (slot->retiredAt[i]))", ")", MUT)
elif MUT == "cooldown":
    CPP = mutate (CPP, "slot->pending[i] = false;", "slot->pending[i] = false; slot->retireCooldown = 0;", MUT)
elif MUT == "noclaim":
    CPP = mutate (CPP, "    slot.pending[target] = false;", "", MUT)
elif MUT == "early":
    CPP = mutate (CPP, "    freeRetiredMorphs();   // fb636 F4", "    // (moved)", MUT)
    CPP = mutate (CPP, "    rebuildMorphIfNeeded (morphA_, 0,", "    freeRetiredMorphs();\n    rebuildMorphIfNeeded (morphA_, 0,", MUT)
elif MUT == "census":
    CPP += "\nint TerrainAudioProcessor::peekMorphF4 (int o) noexcept\n{\n    return morphB_.live.load (std::memory_order_relaxed) != nullptr ? o : 0;\n}\n"
elif MUT == "extrafree":
    CPP = mutate (CPP, "    freeRetiredMorphs();   // fb636 F4", "    morphC_.buf[0].releaseStorage();\n    freeRetiredMorphs();   // fb636 F4", MUT)
elif MUT:
    print ("  unknown MPG_SRC_MUT=%s" % MUT); sys.exit (2)

ph, cpp, ed = strip (PH), strip (CPP), strip (ED)
other = { k: strip (v) for k, v in OTHER.items() }
D = defs (cpp)
print ("══ morph_pages_gate — fb636 F4 (the source half)%s ══" % (" [MUT=%s]" % MUT if MUT else ""))

pub  = body_of (cpp, "void TerrainAudioProcessor::publishMorph (MorphSlot& slot, const tw::Wavetable* to) noexcept")
free = body_of (cpp, "void TerrainAudioProcessor::freeRetiredMorphs()")
reb  = body_of (cpp, "void TerrainAudioProcessor::rebuildMorphIfNeeded (MorphSlot& slot, int oscIdx,")
tmr  = body_of (cpp, "void TerrainAudioProcessor::timerCallback()")
pbk  = body_of (cpp, "void TerrainAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)")
rmt  = body_of (cpp, "TerrainAudioProcessor::resolveMorphTable (MorphSlot& slot, int presetIdx) noexcept")
wfo  = body_of (ph,  "const tw::Wavetable* wavetableForOsc (int osc, MorphSlot& slot, int presetIdx) noexcept")
ms   = body_of (ph,  "struct MorphSlot")
missing = [n for n, b in (("publishMorph", pub), ("freeRetiredMorphs", free), ("rebuildMorphIfNeeded", reb), ("timerCallback", tmr),
                          ("processBlock", pbk), ("resolveMorphTable", rmt), ("wavetableForOsc", wfo), ("MorphSlot", ms)) if not b]
if missing:
    print ("  FAIL  a body was not found: %s — the gate would test nothing" % ", ".join (missing)); sys.exit (1)

# [1] the stamps exist; the timer frees once, after the four rebuilds; nothing else frees; the audio thread never does
fields = all (re.search (p, ms) for p in (r"\bbool\s+pending\s*\[\s*2\s*\]", r"juce::uint64\s+retiredAt\s*\[\s*2\s*\]", r"juce::uint32\s+retiredMs\s*\[\s*2\s*\]"))
rebAt  = [m.start() for m in re.finditer (r"\brebuildMorphIfNeeded\s*\(\s*morph[A-D]_", tmr)]
freeAt = [m.start() for m in re.finditer (r"\bfreeRetiredMorphs\s*\(\s*\)\s*;", tmr)]
CALL = r"(?<![\w:])(?<!void )freeRetiredMorphs\s*\(\s*\)\s*;"   # a call, not the header's `void freeRetiredMorphs();`
allFree = len (re.findall (CALL, cpp)) + len (re.findall (CALL, ph))
order  = len (rebAt) == 4 and len (freeAt) == 1 and freeAt[0] > max (rebAt)
audioClean = not re.search (r"\b(publishMorph|freeRetiredMorphs|rebuildMorphIfNeeded|releaseStorage)\s*\(", pbk + wfo)
chk (fields and order and allFree == 1 and audioClean,
     "[1] MorphSlot carries the stamps; timerCallback frees once, AFTER the four rebuilds, and nothing else calls it; the audio thread never frees",
     "stamps=%s rebuilds=%d frees-in-timer=%d after-rebuilds=%s calls-in-source=%d audio-clean=%s"
     % (fields, len (rebAt), len (freeAt), order, allFree, audioClean))

# [2] one publish door, stamping only a transition, seq_cst on both halves
stSeq  = re.search (r"slot\.live\.store\s*\(\s*to\s*,\s*std::memory_order_seq_cst\s*\)\s*;", pub)
early  = re.search (r"if\s*\(\s*was\s*==\s*nullptr\s*\|\|\s*was\s*==\s*to\s*\)\s*return\s*;", pub)
stamp  = re.search (r"retiredAt\s*\[\s*r\s*\]\s*=\s*audioSeq_\.load\s*\(\s*std::memory_order_seq_cst\s*\)", pub)
shape  = bool (stSeq and early and stamp and stSeq.start() < early.start() < stamp.start())
pubs   = re.findall (r"\bpublishMorph\s*\(\s*slot\s*,\s*([^)]*?)\s*\)\s*;", reb)
direct = re.search (r"\bslot\.live\.store\s*\(", reb)
stamps = re.search (r"\b(retiredAt|retiredMs)\s*\[[^\]]*\]\s*=|\bpending\s*\[[^\]]*\]\s*=\s*true", reb)
chk (shape and sorted (pubs) == sorted (["nullptr", "&slot.buf[target]"]) and not direct and not stamps,
     "[2] every morph publish goes through publishMorph: seq_cst store, return on nothing-was-live BEFORE the stamp, seq_cst audioSeq_ after",
     "publishMorph shape=%s · rebuild publishes %s · direct live store in the rebuild=%s · rebuild stamps=%s"
     % (shape, pubs, bool (direct), stamps.group (0) if stamps else None))

# [3] the audio half of the fence
loads = re.findall (r"\.live\.load\s*\(\s*(std::memory_order_\w+)\s*\)", wfo) + re.findall (r"\.live\.load\s*\(\s*(std::memory_order_\w+)\s*\)", rmt)
chk (len (loads) >= 3 and all (o == "std::memory_order_seq_cst" for o in loads),
     "[3] every live load in wavetableForOsc and resolveMorphTable is seq_cst (the fence's audio half)",
     "orders: %s" % loads)

# [4] the free's five conditions, and it writes nothing a rebuild depends on
cond = re.search (r"if\s*\((.*?)\)\s*\{[^{}]*releaseStorage\s*\(\s*\)", free, re.S)
c = cond.group (1) if cond else ""
need = { "pending":  r"slot->pending\s*\[\s*i\s*\]",
         "not live": r"&\s*slot->buf\s*\[\s*i\s*\]\s*!=\s*live",
         "not read": r"slot->audioReadingIdx\.load\s*\([^)]*\)\s*!=\s*i",
         "hold":     r"now\s*-\s*slot->retiredMs\s*\[\s*i\s*\]\s*>=\s*kImportFreeHoldMs",
         "grace":    r"importGraceOver\s*\(\s*slot->retiredAt\s*\[\s*i\s*\]\s*\)" }
lack = [k for k, p in need.items() if not re.search (p, c)]
writes = re.findall (r"\b(retireCooldown|buildIdx|builtPreset|builtMode|builtAmount|builtLo|builtHi|builtImportPtr|builtImportEpoch|retiredAt|retiredMs)\b"
                     r"\s*(?:\[[^\]]*\]\s*)?(?:=(?!=)|\+\+|--|[+\-^|&]=)", free)
writes += [w + ".store" for w in re.findall (r"\b(live|ready|audioReadingIdx)\s*(?:\[[^\]]*\]\s*)?\.\s*(?:store|exchange|fetch_\w+)\s*\(", free)]
chk (not lack and not writes,
     "[4] freeRetiredMorphs: pending, not live, not the audio thread's buffer, past the hold AND the grace fence — and it writes only `pending`",
     ("missing: %s  " % lack if lack else "") + ("writes rebuild state: %s" % writes if writes else "no rebuild state written"))

# [5] the claim
cl = re.search (r"slot\.pending\s*\[\s*target\s*\]\s*=\s*false\s*;", reb)
nr = re.search (r"slot\.ready\s*\[\s*target\s*\]\s*\.store\s*\(\s*false", reb)
bf = re.search (r"slot\.buf\s*\[\s*target\s*\]\s*\.buildFromSpec\s*\(", reb)
chk (bool (cl and nr and bf and cl.start() < nr.start() < bf.start()),
     "[5] a rebuild claims its target (pending[target] = false) before it marks it not-ready and builds",
     "claim=%s not-ready=%s build=%s" % (bool (cl), bool (nr), bool (bf)))

# [6] THE CENSUS
SLOT_OK  = { "wavetableForDisplay", "wavetableForOsc", "resolveMorphTable", "rebuildMorphIfNeeded", "publishMorph" }
NAMED_OK = { "wtTableStamp", "getOscNumFrames", "getOscWavetableJson", "timerCallback", "processBlock", "freeRetiredMorphs" }
stray = []
takers = set()
for src, label in ((ph, "PluginProcessor.h"), (cpp, "PluginProcessor.cpp")):
    for m in re.finditer (r"\b(\w+)\s*\([^;{}()]*\bMorphSlot\s*&", src):
        takers.add (m.group (1))
        if m.group (1) not in SLOT_OK: stray.append ("%s: %s takes a MorphSlot&" % (label, m.group (1)))
named = set()
for m in re.finditer (r"\bmorph[A-D]_\b", cpp):
    fn = enclosing (D, m.start()); named.add (fn)
    if fn not in NAMED_OK: stray.append ("PluginProcessor.cpp:%s names %s" % (fn, m.group (0)))
hNames = re.findall (r"\bmorph[A-D]_\b", ph)
hDecl  = re.search (r"MorphSlot\s+morphA_\s*,\s*morphB_\s*,\s*morphC_\s*,\s*morphD_\s*;", ph)
if len (hNames) != 4 or not hDecl: stray.append ("PluginProcessor.h names morph slots outside their declaration (%d names)" % len (hNames))
if re.search (r"\bmorph[A-D]_\b|\bMorphSlot\b", ed): stray.append ("PluginEditor.cpp reaches a MorphSlot")
for fn, s in other.items():
    if re.search (r"\bmorph[A-D]_\b|\bMorphSlot\b", s): stray.append ("%s reaches a MorphSlot" % fn)
chk (not stray, "[6] every function that reaches a MorphSlot is audited (audio read · message-thread display · timer rebuild/publish/free)",
     ("UNAUDITED: " + "; ".join (stray)) if stray else "MorphSlot& in %s; morphA-D_ named in %s" % (sorted (takers), sorted (named)))

# [7] two doors free a table
callers = []
for m in re.finditer (r"(?:\.|->)\s*releaseStorage\s*\(\s*\)", cpp): callers.append (enclosing (D, m.start()))
for label, s in [("PluginProcessor.h", ph), ("PluginEditor.cpp", ed)] + sorted (other.items()):
    for m in re.finditer (r"(?:\.|->)\s*releaseStorage\s*\(\s*\)", s): callers.append ("%s (%s)" % (label, "?"))
chk (sorted (callers) == ["freeRetiredImports", "freeRetiredMorphs"],
     "[7] releaseStorage has exactly two callers in Source: freeRetiredImports and freeRetiredMorphs",
     "callers: %s" % sorted (callers))

print ("  %d pass / %d FAIL" % (passed, failed))
sys.exit (0 if failed == 0 else 1)
