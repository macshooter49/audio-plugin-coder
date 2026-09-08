#!/usr/bin/env bash
# APC Setup — Initialize and update Git submodules
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

info "Initializing and updating Git submodules..."
cd "$REPO_ROOT"
git submodule update --init --recursive

# fb601 — the JS UI gates' ONE npm dependency (puppeteer-core), installed from the
# committed Tests/package-lock.json. Before this, `node Tests/harm_waterfall_gate.js`
# died on MODULE_NOT_FOUND and five gates were silently skipped. Prints whether it
# fired; never fails the whole setup when node/npm is absent.
info "Setting up the JS UI gates (puppeteer-core)..."
"$SCRIPT_DIR/setup-ui-gates.sh" || warn "UI gate setup did not complete — see above; the JS gates in plugins/Terrain/Tests will not run."

success "Setup Complete."
