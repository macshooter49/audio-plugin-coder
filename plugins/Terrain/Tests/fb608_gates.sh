#!/usr/bin/env bash
# ══════════════════════════════════════════════════════════════════════════════════════════════
#  fb608_gates.sh — the header gate, run once clean and then once per mutation control.
#
#    bash Tests/fb608_gates.sh          # from plugins/Terrain
#
#  A green bar that cannot go red is not a gate. `tpb_head.js` carries EIGHT mutations that must
#  each turn it red, and ONE that must LEAVE IT GREEN — that last one is not a mistake, it is the
#  disproof of a comment this change deleted (see `push` below).
#
#  WHAT IT GATES — Max: "the 'wavetables' folder directory needs to go next to the arrows … at the
#  very top of the menu header … idk about the 3 lines and separators, just looks weird … but no
#  cramming and make sure the '...' gets there if there are any LONG directorys", then "actually,
#  i want the SEARCH to be at the header, same rules apply."
#
#  THE MUTATIONS
#    band     search gets its own 26px band back (the pre-fb608 shape)  → panel/rail/path bars red
#    clip     the path text loses overflow:hidden → it is CUT, not ellipsed
#    noellip  text-overflow:clip → same cut, from the other direction
#    ltr      the "…" moves back to the END → the path shows where you STARTED, not where you are
#    nobidi   the inner span loses its LTR embedding → a Hebrew-named folder reverses the PATH
#    nocap    the path box loses its max-width → it grows into the search field
#    notitle  the ＋ emblem loses the words the label used to show
#    pad      the magnifier comes off the 14px rail the categories sit on
#    push     the path text loses min-width:0 → NOTHING HAPPENS, and that is the point: Flexbox
#             §4.5 already clamps the automatic minimum to zero on an overflow:hidden box, so the
#             folklore that credits min-width:0 for the ellipsis is wrong. Kept as a standing
#             disproof so nobody re-adds the claim to the comment.
# ══════════════════════════════════════════════════════════════════════════════════════════════
set -u
OUT="${TI_GATE_OUT:-/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/hdr/gates}"
mkdir -p "$OUT"; rc_all=0

run () {  # run <name> <expected-exit>
  local m="$1" want="$2" f="$OUT/${m:-clean}.txt"
  HDR_MUT="$m" node Tests/tpb_head.js > "$f" 2>&1; local rc=$?
  local verdict="OK"
  if [ "$rc" -ne "$want" ]; then
    if [ "$want" -eq 1 ]; then verdict="BROKEN CONTROL — the mutation did NOT go red"
    else verdict="RED — the gate itself is failing"; fi
    rc_all=1
  fi
  printf '  %-9s exit=%d want=%d  %-18s %s\n' "${m:-clean}" "$rc" "$want" \
         "$(grep -oE '[0-9]+ pass, [0-9]+ fail' "$f" | tail -1)" "$verdict"
  [ "$rc" -ne "$want" ] && grep '✗' "$f" | head -3 | sed 's/^/          /'
  return 0
}

echo "══ fb608 HEADER GATES ══"
run ""        0        # as it ships
echo "  ── controls that MUST go red ──"
for m in band clip noellip ltr nobidi nocap notitle pad; do run "$m" 1; done
echo "  ── the control that must STAY green (min-width:0 is not the mechanism) ──"
run push      0

echo
echo "  full output: $OUT"
exit $rc_all
