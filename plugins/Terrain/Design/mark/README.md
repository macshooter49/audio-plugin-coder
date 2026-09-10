# The consciousness-through-sound mark — PARKED (fb630)

Max, after seeing it in the header for one round: *"I actually want to see what it looks like with no
logo."* So the header carries the wordmark alone, and the mark lives here instead of in index.html.

- `mark-dark.svg`  — the negative, for `[data-theme="dark"]` (contrast grows inward; see fb629)
- `mark-light.svg` — the purple original, for `:root` / a future white theme
- `gen.py`         — regenerates both: `python3 gen.py out.json` (geometry measured off Max's artwork)

**To put it back:** paste both SVGs inside `<div class="brand-logo">…</div>` as the first child of
`.header-left` in `Source/ui/public/index.html`. The `.brand-logo` CSS rules (34px, theme swap) were
left in place for exactly that, so nothing else needs to change. Bar [22] of
`Tests/preset_surfaces_gate.js` would then need its "no mark" clause flipped back.
