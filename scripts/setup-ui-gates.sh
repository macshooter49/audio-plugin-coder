#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# fb601 — THE JS UI GATES CAN ACTUALLY BE RUN.
#
# Five committed gates (harm_header_gate · harm_waterfall_gate · harm_wtmenu_gate ·
# hue_track_gate · fx3_ui, plus ~30 more .js harnesses in Tests/) drive the shipped
# index.html through headless Chrome. They `require('puppeteer-core')`, and until
# fb601 that module lived ONLY in a per-session /private/tmp scratchpad — the exact
# thing Tests/README.md's opening line says this directory exists to stop:
#
#     "Every build bible cites harnesses by paths that do not exist: they were
#      written in per-session scratchpads under /private/tmp and evaporated."
#
# The consequence was measured in fb600: a bare `node Tests/harm_waterfall_gate.js`
# died on MODULE_NOT_FOUND, so five gates were SILENTLY SKIPPED for an unknown
# number of sessions. A gate nobody can run is a red bar everyone steps over.
#
# VENDOR OR INSTALL? — INSTALL, with the lockfile committed. Reasons, in order:
#   1. puppeteer-core 25.10.0 pulls 25 packages / ~5.4 MB of JS. Committing that
#      tree is exactly the class of accident .gitignore already records ("175 MB
#      slipped in once", the build-base note).
#   2. `-core` deliberately ships NO browser. The gates drive the Chrome that is
#      already on the machine (CHROME_PATH, default /Applications/Google Chrome.app).
#      So vendoring the JS would leave the REAL dependency un-vendored anyway — a
#      bigger repo and the identical setup step.
#   3. Tests/package-lock.json IS committed, so `npm ci` reinstalls byte-exactly.
#
# WHY Tests/node_modules AND NOT THE REPO ROOT — node resolves a bare require()
# from the requiring FILE's directory upward, not from the cwd. Installing beside
# the gates makes the documented command a plain `node Tests/<gate>.js` from the
# plugin root with NO NODE_PATH, which is what the README now says.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TESTS_DIR="$REPO_ROOT/plugins/Terrain/Tests"

# HARD LAW: a step that can silently no-op must PRINT whether it fired.
say () { printf '[ui-gates] %s\n' "$*"; }

if [ ! -f "$TESTS_DIR/package.json" ]; then
  say "SKIPPED — no $TESTS_DIR/package.json (nothing to install)"
  exit 0
fi

if ! command -v npm >/dev/null 2>&1; then
  say "SKIPPED — npm not on PATH. The JS UI gates will MODULE_NOT_FOUND until it is."
  say "          Install Node 22+ (brew install node), then re-run: scripts/setup-ui-gates.sh"
  exit 0
fi

say "npm $(npm -v) · node $(node -v 2>/dev/null || echo '?')"
if [ -f "$TESTS_DIR/package-lock.json" ]; then
  say "installing from the committed lockfile (npm ci) into $TESTS_DIR/node_modules"
  ( cd "$TESTS_DIR" && npm ci --no-fund --no-audit )
else
  say "no lockfile — npm install (this SHOULD NOT happen; package-lock.json is committed)"
  ( cd "$TESTS_DIR" && npm install --no-fund --no-audit )
fi

# Did it actually fire? Resolve the module the way a gate does, from a gate's directory.
if ( cd "$TESTS_DIR" && node -e "require.resolve('puppeteer-core')" >/dev/null 2>&1 ); then
  say "OK — puppeteer-core $(cd "$TESTS_DIR" && node -p "require('puppeteer-core/package.json').version") resolves from Tests/"
  say "     run a gate with:  cd plugins/Terrain && node Tests/harm_waterfall_gate.js"
else
  say "FAILED — puppeteer-core still does not resolve from $TESTS_DIR"
  exit 1
fi

# The second, un-vendorable dependency: a real Chrome binary.
CHROME="${CHROME_PATH:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
if [ -x "$CHROME" ]; then
  say "Chrome found: $CHROME"
else
  say "⚠️  Chrome NOT found at: $CHROME"
  say "    puppeteer-CORE ships no browser on purpose. Install Google Chrome, or export"
  say "    CHROME_PATH=/path/to/chrome. Without it every JS gate dies at puppeteer.launch()."
fi
