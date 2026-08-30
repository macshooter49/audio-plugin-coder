#!/usr/bin/env python3
"""relay_gate — every data-syn control must have a WebSliderRelay.

WHY THIS EXISTS (fb547). `window.Juce.getSliderState(id)` on a parameter with NO relay does not
fail: it returns a live-looking state object that reads 0 forever and swallows every write. The
control looks perfect and does nothing. This has now cost three sessions:
    fb544  the PHASE and RAND pills                 (found by a user report)
    fb546  noted again while wiring the warp card
    fb547  UNISON RANGE and STACK on all four oscs  (found by a user report)
Twice a human found it before we did. This gate finds it at build time instead.

THE TELL, if you ever debug one by hand: a control that displays 0 for a parameter whose default
is NOT 0. Range's real default is 150 cents and the knob read 0.000.
"""
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
html = (root / "Source/ui/public/index.html").read_text()
hdr  = (root / "Source/PluginEditor.h").read_text()

used   = set(re.findall(r'data-syn="([A-Z0-9_]+)"', html))
# require a CLOSING PAREN right after the literal, so `getSliderState('SYN_OSC_' + o + '_X')`
# contributes its fragments to nothing. Concatenated ids cannot be checked statically; they are
# covered because every id they can build also appears as a data-syn attribute somewhere.
used  |= set(re.findall(r"getSliderState\s*\(\s*'([A-Z0-9_]+)'\s*\)", html))
used.discard("PARAM_ID")                      # the documentation placeholder, not a parameter
relays = set(re.findall(r'WebSliderRelay\s+\w+\s*\{\s*ParameterIDs::([A-Z0-9_]+)\s*\}', hdr))

missing = sorted(used - relays)
print(f"  data-syn / getSliderState ids: {len(used)}    relays declared: {len(relays)}")
if missing:
    print(f"\n  ❌ {len(missing)} control(s) bound to a parameter with NO RELAY — they will read 0")
    print("     forever and silently swallow every write:\n")
    for m in missing: print(f"       {m}")
    print("\n  Fix: add the relay + attachment + .withOptionsFrom in PluginEditor.h/.cpp")
    print("  (mirror SYN_OSC_A_UDETUNE), or drive the control through __setSynParam instead.")
    sys.exit(1)
print("  ✅ every bound control has a relay")
