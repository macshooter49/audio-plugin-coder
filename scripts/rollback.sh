#!/usr/bin/env bash
# Restores a plugin from a ZIP backup (macOS/Linux)
# Port of rollback.ps1 — uses unzip instead of Expand-Archive.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

usage() {
    echo "Usage: $(basename "$0") -p <PluginName> -v <Version>"
    exit 1
}

PLUGIN_NAME=""
VERSION=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--plugin) PLUGIN_NAME="$2"; shift 2 ;;
        -v|--version) VERSION="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) shift ;;
    esac
done

if [[ -z "$PLUGIN_NAME" || -z "$VERSION" ]]; then
    error "Plugin name and version are required."
    usage
fi

# 1. Setup Paths
PLUGIN_DIR="$REPO_ROOT/plugins/$PLUGIN_NAME"
BACKUP_ROOT="$REPO_ROOT/_backups/$PLUGIN_NAME"

# Strip 'v' for cleaner matching
CLEAN_VER="${VERSION#v}"

info "--- ROLLBACK: $PLUGIN_NAME to v$CLEAN_VER ---"

# 2. Find the Backup Zip
if [[ ! -d "$BACKUP_ROOT" ]]; then
    error "No backups found for $PLUGIN_NAME at $BACKUP_ROOT"
    exit 1
fi

# Find zips matching the version. Sort by modification time, newest first.
TARGET_ZIP=$(ls -t "$BACKUP_ROOT"/*v${CLEAN_VER}*.zip 2>/dev/null | head -1)

if [[ -z "$TARGET_ZIP" ]]; then
    error "Could not find a backup zip for version $CLEAN_VER in $BACKUP_ROOT"
    exit 1
fi

warn "Found Backup: $(basename "$TARGET_ZIP")"

# 3. Perform Rollback
{
    # Remove current folder if it exists
    if [[ -d "$PLUGIN_DIR" ]]; then
        gray "Clearing current source..."
        rm -rf "$PLUGIN_DIR"
    fi

    # Re-create empty folder
    mkdir -p "$PLUGIN_DIR"

    # Unzip
    gray "Restoring from Zip..."
    unzip -o "$TARGET_ZIP" -d "$PLUGIN_DIR"

    # Fix: Unzip may create the folder structure inside.
    # If the zip created a nested "PluginName" folder inside "PluginName", move it up.
    if [[ -d "$PLUGIN_DIR/$PLUGIN_NAME/Source" ]]; then
        gray "Adjusting folder structure..."
        mv "$PLUGIN_DIR/$PLUGIN_NAME"/* "$PLUGIN_DIR/" 2>/dev/null || true
        mv "$PLUGIN_DIR/$PLUGIN_NAME"/.* "$PLUGIN_DIR/" 2>/dev/null || true
        rmdir "$PLUGIN_DIR/$PLUGIN_NAME" 2>/dev/null || true
    fi

    success "SUCCESS! $PLUGIN_NAME rolled back to v$CLEAN_VER"
} || {
    error "Rollback Failed"
    exit 1
}
