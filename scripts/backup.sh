#!/usr/bin/env bash
# Creates a clean ZIP backup of a plugin (macOS/Linux)
# Port of backup.ps1 — uses zip -r instead of Compress-Archive.

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
SOURCE_DIR="$REPO_ROOT/plugins/$PLUGIN_NAME"
BACKUP_ROOT="$REPO_ROOT/_backups"
TIMESTAMP="$(date +"%Y%m%d_%H%M")"

# Strip 'v' prefix if user typed "v1.0"
CLEAN_VER="${VERSION#v}"
ZIP_NAME="${PLUGIN_NAME}_v${CLEAN_VER}_${TIMESTAMP}.zip"
TARGET_ZIP="$BACKUP_ROOT/$PLUGIN_NAME/$ZIP_NAME"

# 2. Validation
if [[ ! -d "$SOURCE_DIR" ]]; then
    error "Plugin '$PLUGIN_NAME' not found at $SOURCE_DIR"
    exit 1
fi

# 3. Create Temp Staging Area
TEMP_DIR="$(mktemp -d)"
STAGE_DIR="$TEMP_DIR/$PLUGIN_NAME"
mkdir -p "$STAGE_DIR"

info "--- BACKUP: $PLUGIN_NAME ($VERSION) ---"
gray "Staging files..."

# 4. Copy files
cp -R "$SOURCE_DIR/." "$STAGE_DIR/"

# 5. Clean the Staging Area (Remove Artifacts)
EXCLUSIONS=(
    "build"
    "cmake-build-*"
    ".vs"
    "*.user"
    "*.suo"
    "*.ncb"
    "*.db"
    "*.ipch"
    ".DS_Store"
)

for exclude in "${EXCLUSIONS[@]}"; do
    find "$STAGE_DIR" -name "$exclude" -exec rm -rf {} + 2>/dev/null || true
done

# 6. Zip It
mkdir -p "$BACKUP_ROOT/$PLUGIN_NAME"

warn "Compressing to: $TARGET_ZIP"
(cd "$TEMP_DIR" && zip -r "$TARGET_ZIP" "$PLUGIN_NAME" -x "*/.DS_Store")

# 7. Cleanup
rm -rf "$TEMP_DIR"

success "SUCCESS! Backup saved to $TARGET_ZIP"
