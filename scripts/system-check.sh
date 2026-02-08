#!/usr/bin/env bash
# Automated dependency validation for APC (macOS/Linux)
# Port of system-check.ps1 — checks cmake, Xcode/GCC, python3, JUCE, pluginval.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

COMMAND="${1:---check-all}"

check_platform() {
    local platform
    platform="$(detect_platform)"
    local ver
    ver="$(uname -r)"
    echo "{\"platform\":\"$platform\",\"version\":\"$ver\"}"
}

check_python() {
    local min="3.8"
    if command -v python3 &>/dev/null; then
        local ver
        ver="$(python3 --version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
        if [[ -z "$ver" ]]; then
            echo '{"found":false,"error":"Cannot parse version"}'
            return
        fi
        local ok="false"
        if version_gte "$ver" "$min"; then
            ok="true"
        fi
        echo "{\"found\":true,\"version\":\"$ver\",\"ok\":$ok}"
    else
        echo '{"found":false}'
    fi
}

check_xcode() {
    if [[ "$(detect_platform)" != "macos" ]]; then
        echo '{"found":false,"reason":"Not macOS"}'
        return
    fi
    if command -v xcodebuild &>/dev/null; then
        local xcode_out
        xcode_out="$(xcodebuild -version 2>&1 || true)"
        if echo "$xcode_out" | grep -q "error:"; then
            # Command line tools only, no full Xcode
            echo '{"found":false,"error":"Only Command Line Tools installed. Full Xcode recommended for AU builds."}'
        else
            local ver
            ver="$(echo "$xcode_out" | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?')"
            if [[ -n "$ver" ]]; then
                echo "{\"found\":true,\"version\":\"$ver\",\"ok\":true}"
            else
                echo '{"found":true,"version":"unknown","ok":true}'
            fi
        fi
    else
        echo '{"found":false,"error":"Xcode not installed. Run: xcode-select --install"}'
    fi
}

check_gcc() {
    if [[ "$(detect_platform)" != "linux" ]]; then
        echo '{"found":false,"reason":"Not Linux"}'
        return
    fi
    if command -v g++ &>/dev/null; then
        local ver
        ver="$(g++ --version 2>&1 | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
        echo "{\"found\":true,\"version\":\"$ver\",\"ok\":true}"
    else
        echo '{"found":false}'
    fi
}

check_cmake() {
    if command -v cmake &>/dev/null; then
        local ver
        ver="$(cmake --version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+'| head -1)"
        local ok="false"
        if version_gte "$ver" "3.22"; then
            ok="true"
        fi
        echo "{\"found\":true,\"version\":\"$ver\",\"ok\":$ok}"
    else
        echo '{"found":false}'
    fi
}

check_juce() {
    local juce_path="$REPO_ROOT/_tools/JUCE"
    if [[ -f "$juce_path/modules/juce_core/juce_core.h" ]]; then
        echo "{\"found\":true,\"path\":\"$juce_path\",\"ok\":true}"
    else
        echo "{\"found\":false,\"path\":\"$juce_path\"}"
    fi
}

check_pluginval() {
    local platform
    platform="$(detect_platform)"

    # Check multiple possible locations
    local paths=(
        "$REPO_ROOT/_tools/pluginval/pluginval"
        "$REPO_ROOT/_tools/pluginval/pluginval.app/Contents/MacOS/pluginval"
    )

    for p in "${paths[@]}"; do
        if [[ -x "$p" ]]; then
            echo "{\"found\":true,\"path\":\"$p\",\"ok\":true}"
            return
        fi
    done

    echo '{"found":false}'
}

check_all() {
    local platform
    platform="$(detect_platform)"

    echo "{"
    echo "  \"platform\": $(check_platform),"
    echo "  \"python\": $(check_python),"

    if [[ "$platform" == "macos" ]]; then
        echo "  \"xcode\": $(check_xcode),"
    else
        echo "  \"gcc\": $(check_gcc),"
    fi

    echo "  \"cmake\": $(check_cmake),"
    echo "  \"juce\": $(check_juce),"
    echo "  \"pluginval\": $(check_pluginval)"
    echo "}"
}

# --- User-friendly summary ---
check_all_pretty() {
    info "--- APC System Check ---"

    local platform
    platform="$(detect_platform)"
    success "Platform: $platform ($(uname -r))"

    # Python
    if command -v python3 &>/dev/null; then
        local pyver
        pyver="$(python3 --version 2>&1)"
        success "Python: $pyver"
    else
        error "Python: NOT FOUND"
    fi

    # Build tools
    if [[ "$platform" == "macos" ]]; then
        if command -v xcodebuild &>/dev/null; then
            local xcode_out
            xcode_out="$(xcodebuild -version 2>&1 || true)"
            if echo "$xcode_out" | grep -q "error:"; then
                warn "Xcode: Command Line Tools only (full Xcode recommended for AU builds)"
            else
                success "Xcode: $(echo "$xcode_out" | head -1)"
            fi
        else
            error "Xcode: NOT FOUND (run: xcode-select --install)"
        fi
    else
        if command -v g++ &>/dev/null; then
            success "GCC: $(g++ --version 2>&1 | head -1)"
        else
            error "GCC: NOT FOUND"
        fi
    fi

    # CMake
    if command -v cmake &>/dev/null; then
        local cmver
        cmver="$(cmake --version 2>&1 | head -1)"
        if version_gte "$(echo "$cmver" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')" "3.22"; then
            success "CMake: $cmver"
        else
            warn "CMake: $cmver (need >= 3.22)"
        fi
    else
        error "CMake: NOT FOUND"
    fi

    # JUCE
    if [[ -f "$REPO_ROOT/_tools/JUCE/modules/juce_core/juce_core.h" ]]; then
        success "JUCE: Found at _tools/JUCE"
    else
        error "JUCE: NOT FOUND at _tools/JUCE (run: ./scripts/setup.sh)"
    fi

    # Pluginval
    local pv_found=false
    for p in "$REPO_ROOT/_tools/pluginval/pluginval" "$REPO_ROOT/_tools/pluginval/pluginval.app/Contents/MacOS/pluginval"; do
        if [[ -x "$p" ]]; then
            success "PluginVal: Found"
            pv_found=true
            break
        fi
    done
    if [[ "$pv_found" == "false" ]]; then
        warn "PluginVal: NOT FOUND (optional, run: ./scripts/pluginval-integration.sh install)"
    fi
}

case "$COMMAND" in
    --check-all|--json) check_all ;;
    --pretty|-v) check_all_pretty ;;
    *) check_all ;;
esac
