#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb610 — THE STALENESS SIGNATURE HAS TO DESCRIBE THE SAME THING ON BOTH SIDES.
#
#    python3 Tests/wt_stamp_gate.py            # from plugins/Terrain
#    WTSTAMP_MUT=drop|reorder|oneside python3 Tests/wt_stamp_gate.py     # each must go RED
#
#  Max: "the tables take a FEW SECONDS. Serum's take NONE."  It was never the bake (32.3 ms for a
#  128-frame table, Tests/wt_bake_bench.cpp). The waterfall decides it is stale by comparing
#  __wtDisp — pushed at 60 Hz from PluginEditor.cpp — against the SAME fields echoed back in the
#  payload from getOscWavetableJson. Two lists, written in two languages, four hundred thousand
#  lines apart, and correctness depends on their ORDER matching. index.html says so out loud:
#    "Order matters as much as presence: a mismatch makes the two signatures differ forever, which
#     is not a stale table but a table that re-bakes at the maximum rate for good."
#  And the OTHER direction — a field in the live push that the payload never echoes — is the bug
#  fb610 fixed: signatures that can never disagree, so the picture is declared fresh for ever.
#
#  THREE BARS, and none of them can pass on a missing measurement:
#    [1] the two lists are the same LENGTH
#    [2] every field cachedSig reads is emitted by getOscWavetableJson on BOTH of its payload
#        branches (the fb589 "rides on both paths or neither" law)
#    [4] the signature carries a field derived from the TABLE ITSELF — the one bar that reds on
#        the pre-fb610 code, because [1]-[3] were all GREEN while the bug was shipping
#    [3] each POSITION in the C++ push holds the value the JS reads at that position — a length
#        check alone would pass on any permutation of the middle, which is the exact failure
#        index.html warns about ("Order matters as much as presence")
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys, pathlib

MUT = os.environ.get("WTSTAMP_MUT", "")
ROOT = pathlib.Path(__file__).resolve().parent.parent
EDITOR = (ROOT / "Source" / "PluginEditor.cpp").read_text()
PROC   = (ROOT / "Source" / "PluginProcessor.cpp").read_text()
HTML   = (ROOT / "Source" / "ui" / "public" / "index.html").read_text()

PASS = FAIL = 0
def gate(ok, name, detail=""):
    global PASS, FAIL
    if ok: PASS += 1; print("  ✓ %s   %s" % (name, detail))
    else:  FAIL += 1; print("  ✗ %s   %s" % (name, detail))

# ── the JS side: the array cachedSig maps over ────────────────────────────────────────────────
m = re.search(r"return\s*\(t&&t\.wm!=null\)\?\[([^\]]*)\]\.map", HTML)
if not m:
    print("  ✗ could not find cachedSig's field list in index.html"); sys.exit(1)
js_fields = [f.strip()[2:] for f in m.group(1).split(",") if f.strip().startswith("t.")]
if MUT == "drop":     js_fields = js_fields[:-1]          # the pre-fb610 list — no table identity
# "reorder" swaps two MIDDLE C++ values below — a permutation a length check cannot see

# ── the C++ side: how many values __wtDisp pushes per oscillator ──────────────────────────────
blk = EDITOR[EDITOR.index('js << "window.__wtDisp=[";'):]
blk = blk[:blk.index('js << "];";')]
inner = blk[blk.index('js << "["'):]
# every element after the first is introduced by a << "," ; the leading "[" carries element 1
cpp_n = inner.count('<< ","') + 1
if MUT == "drop":     cpp_n -= 1

# ── the payload side: which keys getOscWavetableJson actually emits, per branch ────────────────
body = PROC[PROC.index("juce::String TerrainAudioProcessor::getOscWavetableJson"):]
body = body[:body.index("\n}\n", body.index('return out.toString')) if 'return out.toString' in body else len(body)]
cut  = body.index('const tw::Wavetable* wt = wavetableForDisplay')
harm_branch, wt_branch = body[:cut], body[cut:]
def keys(src): return set(re.findall(r'\\"([a-z0-9]{1,3})\\":', src)) | set(re.findall(r'"\\?"?([a-z0-9]{1,3})\\?"?":', src))
def emitted(src): return set(re.findall(r'\\"([A-Za-z0-9]{1,4})\\":', src)) | set(re.findall(r'"([A-Za-z0-9]{1,4})":', src))
harm_keys, wt_keys = emitted(harm_branch), emitted(wt_branch)
if MUT == "oneside": harm_keys.discard("tg")              # the fb589 trap: a field on one path only

print("══ fb610 WAVETABLE STALENESS SIGNATURE ══   mutation: %s" % (MUT or "(none)"))
print("  cachedSig reads : %s" % ", ".join(js_fields))
print("  __wtDisp pushes : %d values per osc" % cpp_n)

gate(cpp_n == len(js_fields),
     "[1] __wtDisp AND cachedSig ARE THE SAME LENGTH",
     "%d pushed vs %d read%s" % (cpp_n, len(js_fields),
        "" if cpp_n == len(js_fields) else "   ← the signatures can NEVER agree; the table re-bakes at max rate for ever"))

missing = [(f, "harm" if f not in harm_keys else "wavetable")
           for f in js_fields if f not in harm_keys or f not in wt_keys]
gate(not missing,
     "[2] EVERY FIELD RIDES ON BOTH PAYLOAD BRANCHES (fb589's law)",
     "harm %d keys · wavetable %d keys" % (len(harm_keys), len(wt_keys)) if not missing
     else "MISSING: " + ", ".join("%s (absent from the %s branch)" % (f, b) for f, b in missing))

# ── [3] POSITIONAL CORRESPONDENCE, not just a matching count ───────────────────────────────────
# The C++ side is a chain of unnamed VALUES and the JS side is a list of NAMES, so a length check
# alone would pass on any permutation of the middle — which is precisely the failure index.html
# warns about ("Order matters as much as presence"). This pins each position to the expression
# that is allowed to occupy it.
EXPECT = {
    "wm":  r"D\.warpMode",        "wa":  r"D\.warpAmt",     "w2m": r"D\.warp2Mode",
    "w2a": r"D\.warp2Amt",        "fs":  r"D\.foldShape",   "fa":  r"D\.foldAmt",
    "sa":  r"\bsa\b",              "st":  r"\bst\b",          "bl":  r"D\.feedback",
    "lo":  r"\bsl\b",              "hi":  r"\bsh\b",          "fm":  r"fmDisplaySignature",
    "hm":  r"harmDisplaySignature", "tg": r"wtTableStamp",
}
def cpp_exprs(src):
    out = []
    for chunk in src.split('<< ","'):
        c = re.sub(r"//[^\n]*", " ", chunk)                     # drop trailing comments
        c = c.replace('js <<', ' ').replace('"["', ' ').replace('"]"', ' ')
        c = c.replace('<<', ' ').replace('"', ' ')
        out.append(" ".join(c.split()))
    return out
exprs = cpp_exprs(inner)
if MUT == "reorder" and len(exprs) >= 3: exprs[3], exprs[5] = exprs[5], exprs[3]   # swap two MIDDLE values
bad = []
for i, f in enumerate(js_fields):
    if i >= len(exprs): bad.append("%s: nothing pushed at position %d" % (f, i)); continue
    pat = EXPECT.get(f)
    if pat is None: bad.append("%s: the gate has no expected expression for this field" % f); continue
    if not re.search(pat, exprs[i]): bad.append("position %d reads '%s' but pushes '%s'" % (i, f, exprs[i][:34]))
gate(not bad,
     "[3] EVERY POSITION PUSHES THE VALUE THAT POSITION IS READ AS",
     ("  ".join("%d:%s" % (i, f) for i, f in enumerate(js_fields))) if not bad else "; ".join(bad[:3]))

# ── [4] AND THE SIGNATURE MUST IDENTIFY THE TABLE AT ALL ──────────────────────────────────────
# ⚠️ BARS [1]-[3] CHECK CONSISTENCY, AND CONSISTENCY WAS NEVER THE PROBLEM. The pre-fb610 code was
# perfectly consistent: thirteen fields, same order, same length, echoed on both branches — and it
# still drew the wrong table for seconds, because every one of those fields describes what is DONE
# to a table and none of them says WHICH table. Drop `tg` from both sides and [1]-[3] all go green.
# So this bar names the thing: the signature carries a field derived from the TABLE ITSELF, and the
# C++ that produces it really reads the resolved table rather than returning a constant.
stamp_src = PROC[PROC.index("int TerrainAudioProcessor::wtTableStamp"):] if "int TerrainAudioProcessor::wtTableStamp" in PROC else ""
stamp_src = stamp_src[:stamp_src.index("\n}\n")] if "\n}\n" in stamp_src else stamp_src
derives = all(k in stamp_src for k in ("wavetableForDisplay", "buildEpoch", "getNumFrames"))
gate("tg" in js_fields and derives,
     "[4] THE SIGNATURE CARRIES THE TABLE'S OWN IDENTITY, DERIVED FROM THE LIVE TABLE",
     ("wtTableStamp reads wavetableForDisplay + buildEpoch + getNumFrames" if derives
      else "*** wtTableStamp does not read the resolved table — a constant cannot go stale ***")
     if "tg" in js_fields else
     "*** no table field in the signature — every picture is 'fresh' for ever (the fb610 bug) ***")

print("\n  %d pass, %d fail%s" % (PASS, FAIL, ("   (mutation: %s)" % MUT) if MUT else ""))
sys.exit(1 if FAIL else 0)
