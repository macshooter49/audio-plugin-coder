# _deferred — parked features (NOT compiled)

Code here is **kept, not deleted** — pulled out of the active build/UI on purpose.
Nothing in this folder is `#include`d by the plugin or listed in CMake, so it does
not compile and does not ship. It stays in the tree (under `Source/`) so it travels
in the Opus zip and stays one `git mv` away from coming back.

## STELLATE — spectral shaper (Botanica-style full-replacement resynthesis)

**Deferred 2026-07-02, per Max** ("take away the Stellate for now… don't delete it,
save the code for later"). He wants to focus on the resonator + the essential
effects/nodes/matrix; spectral can return later.

- `StellateNode.h` — the engine. Full-replacement (no dry mix): audio in → analyzed to
  partials → re-synthesized through the selected shape → out. Contains **CC's tuning fix
  verbatim** (poly → equal temperament, mono-only settled FLL) and the V4 character shapes
  (Hyper/Pluck/Razor/Radio/Crown), spectral LP/HP, AIR residual, MOTION undulation.
- `StellateNode_test.cpp` — standalone g++ harness (was 63/63). Not a CMake test; Opus runs
  it ad-hoc. Its `#include "StellateNode.h"` resolves in-folder, so both files moved together.

**Last fully-wired state: git `4787911`** ("STELLATE V3+V4 — Botanica full-replacement…").
That commit has every integration point, each tagged `[STELLATE-CPP-Vx]` / `[STELLATE-UI-Vx]`.

### To reactivate
1. `git mv Source/_deferred/StellateNode*.{h,cpp} Source/` (back into `Source/`).
2. Re-add `#include "StellateNode.h"` to `PluginProcessor.h`.
3. Restore the wiring removed on the deferral commit — `git show 4787911 -- <file>` shows each
   block. Integration points (all tagged, greppable):
   - `ParameterIDs.hpp` — `SYN_STELL_*` param IDs
   - `PluginProcessor.h` — `wc::StellateNode stell;` member + `stellViz*_` atomics
   - `PluginProcessor.cpp` — param layout, `stell.prepare`, the `stell.process(...)` block
   - `PluginEditor.h` — `stell*Relay` relays + `stell*Attachment` declarations
   - `PluginEditor.cpp` — `.withOptionsFrom(stell*Relay)` chain, attachments, `__terrainStellate` viz feed
   - `ui/public/index.html` — emblems (`gm-embs`/`emb-stellate`), `stell-cv` canvas + `stell-menu`,
     the STELLATE IIFE (star visualizer + XY pad + `setMode` mode-switch)
4. The mode-switch emblems (Annulus ↔ Stellate) were also removed; restoring them re-adds the
   toggle. The Annulus resonator now owns the visualizer slot outright.
