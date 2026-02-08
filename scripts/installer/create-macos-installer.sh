#!/usr/bin/env bash
# Creates macOS installer (.dmg and .pkg) for APC plugins
# Parallels create-windows-installer.ps1 — uses hdiutil + pkgbuild/productbuild.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../common.sh"

usage() {
    cat << EOF
Usage: $(basename "$0") -p <PluginName> -v <Version> [options]

  -p, --plugin       Plugin name (required)
  -v, --version      Version number, e.g. "1.0.0" (required)
  -c, --company      Company name (default: "APC")
  -u, --url          Plugin website URL
  --dmg-only         Only create DMG, skip PKG
  --pkg-only         Only create PKG, skip DMG
  -h, --help         Show this help
EOF
    exit 1
}

PLUGIN_NAME=""
VERSION=""
COMPANY_NAME="APC"
PLUGIN_URL="https://github.com/noizefield/audio-plugin-coder"
DMG_ONLY=false
PKG_ONLY=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--plugin) PLUGIN_NAME="$2"; shift 2 ;;
        -v|--version) VERSION="$2"; shift 2 ;;
        -c|--company) COMPANY_NAME="$2"; shift 2 ;;
        -u|--url) PLUGIN_URL="$2"; shift 2 ;;
        --dmg-only) DMG_ONLY=true; shift ;;
        --pkg-only) PKG_ONLY=true; shift ;;
        -h|--help) usage ;;
        *) error "Unknown option: $1"; usage ;;
    esac
done

if [[ -z "$PLUGIN_NAME" || -z "$VERSION" ]]; then
    error "Plugin name and version are required."
    usage
fi

info "========================================"
info "  Creating macOS Installer"
info "  Plugin: $PLUGIN_NAME"
info "  Version: $VERSION"
info "========================================"
echo ""

# ============================================
# CHECK PREREQUISITES
# ============================================

BUILD_DIR="$REPO_ROOT/build"

# Find build artifacts
VST3_PATH=$(find "$BUILD_DIR" -name "${PLUGIN_NAME}.vst3" -type d 2>/dev/null | head -1)
AU_PATH=$(find "$BUILD_DIR" -name "${PLUGIN_NAME}.component" -type d 2>/dev/null | head -1)
STANDALONE_PATH=$(find "$BUILD_DIR" -name "${PLUGIN_NAME}.app" -type d 2>/dev/null | head -1)

if [[ -z "$VST3_PATH" ]]; then
    error "VST3 build not found. Please build the plugin first."
    warn "Run: ./scripts/build-and-install.sh -p $PLUGIN_NAME"
    exit 1
fi

success "[OK] Build artifacts found"
gray "  VST3: $VST3_PATH"
[[ -n "$AU_PATH" ]] && gray "  AU: $AU_PATH"
[[ -n "$STANDALONE_PATH" ]] && gray "  Standalone: $STANDALONE_PATH"

# ============================================
# CREATE LICENSE FILE
# ============================================

DIST_DIR="$REPO_ROOT/dist"
mkdir -p "$DIST_DIR"
LICENSE_PATH="$DIST_DIR/LICENSE.txt"

if [[ ! -f "$LICENSE_PATH" ]]; then
    warn "Creating license file..."
    CURRENT_YEAR="$(date +"%Y")"
    cat > "$LICENSE_PATH" << LICEOF
================================================================================
                    $PLUGIN_NAME END USER LICENSE AGREEMENT
================================================================================

IMPORTANT: PLEASE READ THIS LICENSE CAREFULLY BEFORE USING THIS SOFTWARE.

1. GRANT OF LICENSE
   This software is licensed, not sold. By installing or using this software,
   you agree to be bound by the terms of this agreement.

2. PERMITTED USE
   - You may install and use this software on multiple computers
   - You may use this software for commercial and non-commercial purposes
   - You may create and distribute audio content using this software

3. RESTRICTIONS
   - You may not reverse engineer, decompile, or disassemble this software
   - You may not redistribute or resell this software
   - You may not remove or alter any copyright notices

4. DISCLAIMER OF WARRANTY
   THIS SOFTWARE IS PROVIDED AS IS WITHOUT WARRANTY OF ANY KIND.

5. LIMITATION OF LIABILITY
   IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DAMAGES ARISING FROM
   THE USE OF THIS SOFTWARE.

================================================================================
By installing this software, you acknowledge that you have read, understood,
and agree to be bound by these terms.

Copyright (c) $CURRENT_YEAR $COMPANY_NAME
================================================================================
LICEOF
    success "License file created: $LICENSE_PATH"
fi

# ============================================
# STAGING AREA
# ============================================

STAGING="$(mktemp -d)"
STAGING_PLUGINS="$STAGING/plugins"
mkdir -p "$STAGING_PLUGINS"

# Copy VST3
if [[ -n "$VST3_PATH" ]]; then
    cp -R "$VST3_PATH" "$STAGING_PLUGINS/"
fi

# Copy AU
if [[ -n "$AU_PATH" ]]; then
    cp -R "$AU_PATH" "$STAGING_PLUGINS/"
fi

# Copy Standalone
if [[ -n "$STANDALONE_PATH" ]]; then
    cp -R "$STANDALONE_PATH" "$STAGING_PLUGINS/"
fi

# Copy LICENSE
cp "$LICENSE_PATH" "$STAGING/"

# Copy Documentation if present
DOC_DIR="$REPO_ROOT/plugins/$PLUGIN_NAME/Documentation"
if [[ -d "$DOC_DIR" ]]; then
    cp -R "$DOC_DIR" "$STAGING/Documentation"
fi

# ============================================
# CREATE PKG INSTALLER
# ============================================

if [[ "$DMG_ONLY" != "true" ]]; then
    warn "Creating PKG installer..."

    PKG_BUILD_DIR="$(mktemp -d)"
    PKG_IDENTIFIER="com.${COMPANY_NAME,,}.${PLUGIN_NAME,,}"

    # Create component packages
    COMPONENT_PKGS=()

    # VST3 component
    if [[ -n "$VST3_PATH" ]]; then
        VST3_PKG="$PKG_BUILD_DIR/${PLUGIN_NAME}-VST3.pkg"
        pkgbuild \
            --root "$VST3_PATH" \
            --identifier "${PKG_IDENTIFIER}.vst3" \
            --version "$VERSION" \
            --install-location "/Library/Audio/Plug-Ins/VST3/${PLUGIN_NAME}.vst3" \
            "$VST3_PKG"
        COMPONENT_PKGS+=("$VST3_PKG")
        success "  [OK] VST3 package created"
    fi

    # AU component
    if [[ -n "$AU_PATH" ]]; then
        AU_PKG="$PKG_BUILD_DIR/${PLUGIN_NAME}-AU.pkg"
        pkgbuild \
            --root "$AU_PATH" \
            --identifier "${PKG_IDENTIFIER}.component" \
            --version "$VERSION" \
            --install-location "/Library/Audio/Plug-Ins/Components/${PLUGIN_NAME}.component" \
            "$AU_PKG"
        COMPONENT_PKGS+=("$AU_PKG")
        success "  [OK] AU package created"
    fi

    # Standalone component
    if [[ -n "$STANDALONE_PATH" ]]; then
        STANDALONE_PKG="$PKG_BUILD_DIR/${PLUGIN_NAME}-Standalone.pkg"
        pkgbuild \
            --root "$STANDALONE_PATH" \
            --identifier "${PKG_IDENTIFIER}.app" \
            --version "$VERSION" \
            --install-location "/Applications/${PLUGIN_NAME}.app" \
            "$STANDALONE_PKG"
        COMPONENT_PKGS+=("$STANDALONE_PKG")
        success "  [OK] Standalone package created"
    fi

    # Build distribution XML for productbuild
    DIST_XML="$PKG_BUILD_DIR/distribution.xml"
    cat > "$DIST_XML" << DISTEOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$PLUGIN_NAME $VERSION</title>
    <organization>$COMPANY_NAME</organization>
    <license file="LICENSE.txt" mime-type="text/plain"/>
    <options customize="allow" require-scripts="false"/>
    <choices-outline>
DISTEOF

    # Add choices for each component
    if [[ -n "$VST3_PATH" ]]; then
        cat >> "$DIST_XML" << 'DISTEOF'
        <line choice="vst3"/>
DISTEOF
    fi
    if [[ -n "$AU_PATH" ]]; then
        cat >> "$DIST_XML" << 'DISTEOF'
        <line choice="au"/>
DISTEOF
    fi
    if [[ -n "$STANDALONE_PATH" ]]; then
        cat >> "$DIST_XML" << 'DISTEOF'
        <line choice="standalone"/>
DISTEOF
    fi

    cat >> "$DIST_XML" << 'DISTEOF'
    </choices-outline>
DISTEOF

    if [[ -n "$VST3_PATH" ]]; then
        local_vst3_pkg="$(basename "$VST3_PKG")"
        cat >> "$DIST_XML" << DISTEOF
    <choice id="vst3" title="VST3 Plugin" description="Install the VST3 plugin to /Library/Audio/Plug-Ins/VST3/">
        <pkg-ref id="${PKG_IDENTIFIER}.vst3"/>
    </choice>
    <pkg-ref id="${PKG_IDENTIFIER}.vst3">${local_vst3_pkg}</pkg-ref>
DISTEOF
    fi

    if [[ -n "$AU_PATH" ]]; then
        local_au_pkg="$(basename "$AU_PKG")"
        cat >> "$DIST_XML" << DISTEOF
    <choice id="au" title="Audio Unit Plugin" description="Install the AU plugin to /Library/Audio/Plug-Ins/Components/">
        <pkg-ref id="${PKG_IDENTIFIER}.component"/>
    </choice>
    <pkg-ref id="${PKG_IDENTIFIER}.component">${local_au_pkg}</pkg-ref>
DISTEOF
    fi

    if [[ -n "$STANDALONE_PATH" ]]; then
        local_standalone_pkg="$(basename "$STANDALONE_PKG")"
        cat >> "$DIST_XML" << DISTEOF
    <choice id="standalone" title="Standalone Application" description="Install the standalone app to /Applications/">
        <pkg-ref id="${PKG_IDENTIFIER}.app"/>
    </choice>
    <pkg-ref id="${PKG_IDENTIFIER}.app">${local_standalone_pkg}</pkg-ref>
DISTEOF
    fi

    echo "</installer-gui-script>" >> "$DIST_XML"

    # Build the combined installer package
    FINAL_PKG="$DIST_DIR/${PLUGIN_NAME}-${VERSION}-macOS.pkg"
    productbuild \
        --distribution "$DIST_XML" \
        --resources "$STAGING" \
        --package-path "$PKG_BUILD_DIR" \
        "$FINAL_PKG"

    rm -rf "$PKG_BUILD_DIR"
    success "[OK] PKG installer created: $FINAL_PKG"
fi

# ============================================
# CREATE DMG
# ============================================

if [[ "$PKG_ONLY" != "true" ]]; then
    warn "Creating DMG..."

    DMG_NAME="${PLUGIN_NAME}-${VERSION}-macOS"
    DMG_PATH="$DIST_DIR/${DMG_NAME}.dmg"
    DMG_STAGING="$(mktemp -d)"

    # Copy all artifacts to DMG staging
    cp -R "$STAGING_PLUGINS"/* "$DMG_STAGING/" 2>/dev/null || true
    cp "$LICENSE_PATH" "$DMG_STAGING/"

    # Copy Documentation if present
    if [[ -d "$STAGING/Documentation" ]]; then
        cp -R "$STAGING/Documentation" "$DMG_STAGING/"
    fi

    # Create a README for installation
    cat > "$DMG_STAGING/INSTALL.txt" << INSTALLEOF
$PLUGIN_NAME $VERSION - Installation Guide

MANUAL INSTALLATION:
  VST3:       Copy $PLUGIN_NAME.vst3 to ~/Library/Audio/Plug-Ins/VST3/
  AU:         Copy $PLUGIN_NAME.component to ~/Library/Audio/Plug-Ins/Components/
  Standalone: Copy $PLUGIN_NAME.app to /Applications/

Or use the .pkg installer for automatic installation.
INSTALLEOF

    # Remove any existing DMG
    rm -f "$DMG_PATH"

    # Create DMG
    hdiutil create \
        -volname "$PLUGIN_NAME $VERSION" \
        -srcfolder "$DMG_STAGING" \
        -ov \
        -format UDZO \
        "$DMG_PATH"

    rm -rf "$DMG_STAGING"
    success "[OK] DMG created: $DMG_PATH"
fi

# ============================================
# CLEANUP & SUMMARY
# ============================================

rm -rf "$STAGING"

echo ""
success "========================================"
success "  Installer Created Successfully!"
success "========================================"

if [[ "$DMG_ONLY" != "true" && -f "${FINAL_PKG:-}" ]]; then
    pkg_size=$(du -h "$FINAL_PKG" | cut -f1)
    warn "  PKG: $FINAL_PKG ($pkg_size)"
fi

if [[ "$PKG_ONLY" != "true" && -f "${DMG_PATH:-}" ]]; then
    dmg_size=$(du -h "$DMG_PATH" | cut -f1)
    warn "  DMG: $DMG_PATH ($dmg_size)"
fi

success "========================================"
