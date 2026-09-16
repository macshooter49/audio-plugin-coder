#!/usr/bin/env python3
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  osc_pool_width_gate.py — tp20b · A PER-OSCILLATOR ID TABLE IS AS WIDE AS THE POOL, OR IT IS
#                                   A BUFFER OVERRUN.
#
#      python3 Tests/osc_pool_width_gate.py            # from plugins/Terrain
#      TI_POOLW_MUT=1 python3 Tests/osc_pool_width_gate.py   # mutation control (must go RED)
#
#  WHY THIS FILE EXISTS.  tp20 redefined ParameterIDs::kOscCount from 4 to 8. Twenty-two sites in
#  nine message-thread display functions had been written as
#
#      osc = juce::jlimit (0, ParameterIDs::kOscCount - 1, osc);          // now clamps to 7
#      static const char* const WF[4] = { ..A.., ..B.., ..C.., ..D.. };   // still FOUR wide
#      return { rawParam (WF[osc])->load(), ... };                        // osc 4..7 reads off the end
#
#  For osc >= 4 that reads PAST the array. The garbage const char* goes to rawParam(), whose slow
#  path is apvts.getRawParameterValue(id) — which returns NULLPTR for an id it does not know — and
#  ->load() on nullptr is a SIGSEGV at address 0x0. The UI refreshes all eight oscillator displays
#  when a preset loads, so this crashed the host on EVERY preset selection. (Ableton Live 12,
#  2026-09-16, EXC_BAD_ACCESS KERN_INVALID_ADDRESS at 0x0, faulting frame
#  TerrainAudioProcessor::wtDispEffective(int) const.)
#
#  The clamp and the table width were CORRECT together before tp20 and CORRECT apart never. That
#  is the whole bug shape: a constant moved, and twenty-two hand-written literals did not move
#  with it. Nothing compared them — the compiler cannot, because indexing a const char*[4] with a
#  runtime int is perfectly legal C++.
#
#  ⚠️ NOT EVERY 4-WIDE OSC TABLE IS WRONG. The audio-thread gather deliberately keeps A–D tables
#     and reaches bank 1 by POINTER REMAP (rawParamB / oscRemap_, OscBankIds.h): it loops o < 4
#     and hands an A–D id to a cache that resolves it to the E–H twin. Those are correct and this
#     gate must leave them alone. The law is about the INDEX, not the table:
#
#         a 4-wide per-oscillator ID table may only be indexed by something bounded to 0..3.
#
#  So the gate resolves, for every indexed use, the nearest bound governing that index — a for
#  loop's limit, a jlimit clamp, or one level of `const int o = <member>;` — and fails only when a
#  >4 bound meets a 4-wide table. The fix is always the same: use the kOsc_*[kOscCount] table from
#  the generated Source/OscBankIds.h, which is the pool's width by construction.
# ══════════════════════════════════════════════════════════════════════════════════════════════
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.path.join(ROOT, "Source", "PluginProcessor.cpp")

MUT = os.environ.get("TI_POOLW_MUT", "")

text = open(SRC, encoding="utf-8", errors="replace").read()

if MUT:
    # Mutation control: re-narrow ONE already-fixed site back to a 4-wide literal table and prove
    # the gate names it. Without this, a gate that is green because its regex stopped matching is
    # indistinguishable from a gate that is green because the tree is clean.
    text = text.replace(
        "const auto* WF = ParameterIDs::kOsc_WT_FRAME;",
        "static const char* const WF[4] = { ParameterIDs::SYN_OSC_A_WT_FRAME, ParameterIDs::SYN_OSC_B_WT_FRAME,\n"
        "                                       ParameterIDs::SYN_OSC_C_WT_FRAME, ParameterIDs::SYN_OSC_D_WT_FRAME };", 1)

lines = text.split("\n")

# ── function boundaries: a column-0 line that defines a member function ───────────────────────
fstarts = []
for i, l in enumerate(lines):
    if l and not l[0].isspace() and "::" in l and "(" in l and not l.lstrip().startswith(("//", "*", "/*")):
        m = re.search(r"(\w+)::(~?\w+)\s*\(", l)
        if m:
            fstarts.append((i, m.group(2)))

def func_of(idx):
    name, at = "<file scope>", -1
    for i, n in fstarts:
        if i <= idx and i > at:
            name, at = n, i
    return name, at

# ── the 4-wide PER-OSCILLATOR id tables (initialiser names A and D oscillator params) ─────────
decl_re = re.compile(r"\b(?:static\s+)?const\s+char\s*\*\s*(?:const\s+)?(\w+)\s*\[\s*4\s*\]")
tables = {}
for i, l in enumerate(lines):
    m = decl_re.search(l)
    if not m:
        continue
    init = "\n".join(lines[i:i + 8])
    if "SYN_OSC_A_" in init and "SYN_OSC_D_" in init:
        tables[m.group(1)] = i

POOL = "ParameterIDs::kOscCount"

def member_bound(mem):
    """A member used as a subscript is bounded by its own clamp, wherever it is assigned.
       jlimit(-1, 3, ..) bounds it to 0..3, which is LEGAL against a 4-wide table — the
       distortion Table-source pill is an A–D feature by design, not an oversight."""
    for k, ml in enumerate(lines):
        mm = re.search(re.escape(mem) + r"\s*=\s*juce::jlimit\s*\(\s*-?\d+\s*,\s*([^,]+),", ml)
        if mm:
            hi = mm.group(1).strip()
            if POOL in hi:
                return 8, "%s clamped hi=%s @%d" % (mem, hi, k + 1)
            if hi.isdigit():
                return int(hi) + 1, "%s clamped hi=%s @%d" % (mem, hi, k + 1)
    return 99, "%s unbounded" % mem


def bound_of(var, use_idx, fn_at):
    """Nearest bound governing `var` at `use_idx`. Returns (n, evidence)."""
    start = fn_at if fn_at >= 0 else 0
    for j in range(use_idx, start - 1, -1):
        l = lines[j]
        m = re.search(r"for\s*\(\s*(?:const\s+)?int\s+" + re.escape(var) + r"\s*=\s*0\s*;\s*"
                      + re.escape(var) + r"\s*<\s*([^;]+);", l)
        if m:
            lim = m.group(1).strip()
            return (8 if POOL in lim else (int(lim) if lim.isdigit() else 99)), "loop < %s @%d" % (lim, j + 1)
        m = re.search(re.escape(var) + r"\s*=\s*juce::jlimit\s*\(\s*-?\d+\s*,\s*([^,]+),", l)
        if m:
            hi = m.group(1).strip()
            if POOL in hi:
                return 8, "jlimit hi=%s @%d" % (hi, j + 1)
            if hi.isdigit():
                return int(hi) + 1, "jlimit hi=%s @%d" % (hi, j + 1)
            return 99, "jlimit hi=%s @%d" % (hi, j + 1)
        m = re.search(r"\b(?:const\s+)?int\s+" + re.escape(var) + r"\s*=\s*(\w+_)\s*;", l)
        if m:                      # one level of indirection: const int o = wtAudOsc_;
            return member_bound(m.group(1))
    if var.endswith("_"):          # a member used directly as the subscript: dstTableSrc_
        return member_bound(var)
    return None, "no bound found"


fails, checked = [], 0
for name, dcl in sorted(tables.items(), key=lambda kv: kv[1]):
    fn, fn_at = func_of(dcl)
    for j in range(dcl, min(dcl + 160, len(lines))):
        for m in re.finditer(re.escape(name) + r"\s*\[\s*([A-Za-z_]\w*)\s*\]", lines[j]):
            var = m.group(1)
            checked += 1
            n, why = bound_of(var, j, fn_at)
            if n is None or n > 4:
                fails.append((j + 1, fn, name, var, why if n else "UNBOUNDED"))

print("— tp20b OSC POOL WIDTH —")
print("  %d four-wide per-oscillator id tables, %d indexed uses examined" % (len(tables), checked))
for ln, fn, name, var, why in fails:
    print("  FAIL  %s:%d  %s()  %s[%s]  — index bounded to 0..%s but table is 4 wide"
          % ("Source/PluginProcessor.cpp", ln, fn, name, var, "7" if "kOscCount" in why else "?"))
    print("        %s   → use ParameterIDs::kOsc_*[kOscCount] (Source/OscBankIds.h)" % why)

if MUT:
    ok = len(fails) > 0 and any(f[2] == "WF" for f in fails)
    print("\nMUTATION CONTROL: gate went %s and %s the re-narrowed table"
          % ("RED" if fails else "GREEN", "NAMED" if any(f[2] == "WF" for f in fails) else "MISSED"))
    sys.exit(0 if ok else 1)

print("\n%d violation(s)" % len(fails))
print("PASS — every per-oscillator id table is as wide as the index that reaches it" if not fails
      else "FAIL — a 4-wide table is reachable with an index up to 7 (null deref, SIGSEGV at 0x0)")
sys.exit(1 if fails else 0)
