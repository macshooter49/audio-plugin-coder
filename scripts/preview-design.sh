#!/usr/bin/env bash
# Instant GUI Preview (macOS/Linux)
# Port of preview-design.ps1 — builds Standalone, launches with open (macOS).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

usage() {
    echo "Usage: $(basename "$0") -p <PluginName>"
    exit 1
}

PLUGIN_NAME=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--plugin) PLUGIN_NAME="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) PLUGIN_NAME="$1"; shift ;;
    esac
done

if [[ -z "$PLUGIN_NAME" ]]; then
    error "Plugin name is required."
    usage
fi

BUILD_DIR="$REPO_ROOT/build"
PLATFORM="$(detect_platform)"

info "--- APC PREVIEW: $PLUGIN_NAME ---"

# 1. Configure (if needed)
if [[ ! -d "$BUILD_DIR" ]]; then
    warn "Configuring..."
    case "$PLATFORM" in
        macos)
            cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Xcode \
                -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
                -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
            ;;
        linux)
            cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G "Unix Makefiles" \
                -DCMAKE_BUILD_TYPE=Release
            ;;
    esac
fi

# 2. Build Standalone
gray "Compiling Standalone..."
cmake --build "$BUILD_DIR" --config Release --target "${PLUGIN_NAME}_Standalone"
if [[ $? -ne 0 ]]; then
    error "Build Failed"
    exit 1
fi

# 3. Launch & Monitor
case "$PLATFORM" in
    macos)
        APP=$(find "$BUILD_DIR" -name "${PLUGIN_NAME}.app" -type d 2>/dev/null | head -1)
        if [[ -n "$APP" ]]; then
            success "Launching..."
            open -W "$APP"
            exit_code=$?
            if [[ $exit_code -ne 0 ]]; then
                warn "CRASH DETECTED (exit code: $exit_code). Check Console.app for crash reports."
            fi
        else
            error "Standalone .app not found."
            exit 1
        fi
        ;;
    linux)
        EXE=$(find "$BUILD_DIR" -name "$PLUGIN_NAME" -type f -executable 2>/dev/null | grep -i standalone | head -1)
        if [[ -n "$EXE" ]]; then
            success "Launching..."
            "$EXE"
            exit_code=$?
            if [[ $exit_code -ne 0 ]]; then
                warn "CRASH DETECTED (exit code: $exit_code)."
            fi
        else
            error "Standalone executable not found."
            exit 1
        fi
        ;;
esac
