#!/usr/bin/env bash
# APC Setup — Initialize and update Git submodules
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

info "Initializing and updating Git submodules..."
cd "$REPO_ROOT"
git submodule update --init --recursive

success "Setup Complete."
