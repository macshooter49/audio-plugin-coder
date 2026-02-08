#!/usr/bin/env bash
# WebView Setup Validation Script (macOS/Linux)
# Port of validate-webview-setup.ps1 — adapted for WKWebView (macOS).

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

PLUGIN_PATH="$REPO_ROOT/plugins/$PLUGIN_NAME"
SOURCE_PATH="$PLUGIN_PATH/Source"
EDITOR_CPP="$SOURCE_PATH/PluginEditor.cpp"
EDITOR_H="$SOURCE_PATH/PluginEditor.h"
CMAKE_LISTS="$PLUGIN_PATH/CMakeLists.txt"
WEBUI_PATH="$SOURCE_PATH/ui/public"
INDEX_HTML="$WEBUI_PATH/index.html"
INDEX_JS="$WEBUI_PATH/js/index.js"
JUCE_INDEX_JS="$WEBUI_PATH/js/juce/index.js"

ISSUES=()
WARNINGS=()

PLATFORM="$(detect_platform)"

info "Validating WebView setup for plugin: $PLUGIN_NAME"
echo "============================================================"

# Check 1: Plugin directory exists
if [[ ! -d "$PLUGIN_PATH" ]]; then
    error "ERROR: Plugin directory not found: $PLUGIN_PATH"
    exit 1
fi

# Check 2: CMakeLists.txt exists
if [[ ! -f "$CMAKE_LISTS" ]]; then
    ISSUES+=("CMakeLists.txt not found")
else
    cmake_content="$(cat "$CMAKE_LISTS")"

    if ! echo "$cmake_content" | grep -q "juce_add_binary_data"; then
        ISSUES+=("CMakeLists.txt missing 'juce_add_binary_data' - web files won't be embedded")
    fi

    # Platform-specific WebView checks
    case "$PLATFORM" in
        macos)
            # macOS uses WKWebView (system framework) - no special flags needed
            # But JUCE_WEB_BROWSER=1 is still required
            if ! echo "$cmake_content" | grep -q "JUCE_WEB_BROWSER=1"; then
                ISSUES+=("CMakeLists.txt missing 'JUCE_WEB_BROWSER=1' compile definition")
            fi
            ;;
        linux)
            if ! echo "$cmake_content" | grep -qE "NEEDS_WEB_BROWSER\s+TRUE"; then
                ISSUES+=("CMakeLists.txt missing 'NEEDS_WEB_BROWSER TRUE' (required for Linux WebKitGTK)")
            fi
            if ! echo "$cmake_content" | grep -q "JUCE_WEB_BROWSER=1"; then
                ISSUES+=("CMakeLists.txt missing 'JUCE_WEB_BROWSER=1' compile definition")
            fi
            ;;
    esac

    if ! echo "$cmake_content" | grep -q "juce::juce_gui_extra"; then
        ISSUES+=("CMakeLists.txt missing 'juce::juce_gui_extra' module")
    fi
fi

# Check 3: PluginEditor.cpp exists
if [[ ! -f "$EDITOR_CPP" ]]; then
    ISSUES+=("PluginEditor.cpp not found")
else
    cpp_content="$(cat "$EDITOR_CPP")"

    if ! echo "$cpp_content" | grep -q "WebBrowserComponent"; then
        ISSUES+=("PluginEditor.cpp doesn't use WebBrowserComponent")
    fi

    # Platform-specific backend checks
    case "$PLATFORM" in
        macos)
            # macOS doesn't need explicit backend — WKWebView is automatic
            ;;
        linux)
            # Linux uses WebKitGTK
            ;;
        *)
            if ! echo "$cpp_content" | grep -qE "withBackend.*webview2"; then
                ISSUES+=("PluginEditor.cpp missing '.withBackend(webview2)' - WebView2 backend not specified")
            fi
            ;;
    esac

    if ! echo "$cpp_content" | grep -q "withNativeIntegrationEnabled"; then
        ISSUES+=("PluginEditor.cpp missing '.withNativeIntegrationEnabled()' - JS to C++ communication disabled")
    fi

    if ! echo "$cpp_content" | grep -q "withResourceProvider"; then
        ISSUES+=("PluginEditor.cpp missing '.withResourceProvider()' - Web files won't load")
    fi

    if ! echo "$cpp_content" | grep -q "getResourceProviderRoot"; then
        if echo "$cpp_content" | grep -qE "data:text/html|loadHTML|goToURL.*data:"; then
            ISSUES+=("PluginEditor.cpp uses data URI instead of resource provider - Use getResourceProviderRoot()")
        else
            WARNINGS+=("PluginEditor.cpp doesn't use getResourceProviderRoot() - May not load embedded files")
        fi
    fi

    # Check for parameter relays (both .h and .cpp)
    combined_content="$cpp_content"
    if [[ -f "$EDITOR_H" ]]; then
        combined_content+=" $(cat "$EDITOR_H")"
    fi
    if ! echo "$combined_content" | grep -qE "WebSliderRelay|WebToggleButtonRelay|WebComboBoxRelay"; then
        WARNINGS+=("No parameter relays found - Parameters won't sync with JavaScript")
    fi

    if ! echo "$cpp_content" | grep -qE "WebSliderParameterAttachment|WebToggleButtonParameterAttachment|WebComboBoxParameterAttachment"; then
        WARNINGS+=("No parameter attachments found - Parameters won't be connected")
    fi
fi

# Check 4: Web UI files exist
if [[ ! -d "$WEBUI_PATH" ]]; then
    ISSUES+=("Web UI directory not found: $WEBUI_PATH")
else
    if [[ ! -f "$INDEX_HTML" ]]; then
        ISSUES+=("index.html not found: $INDEX_HTML")
    fi
    if [[ ! -f "$INDEX_JS" ]]; then
        ISSUES+=("js/index.js not found: $INDEX_JS")
    fi
    if [[ ! -f "$JUCE_INDEX_JS" ]]; then
        WARNINGS+=("js/juce/index.js not found - JUCE frontend library missing. Copy from JUCE modules/juce_gui_extra/native/javascript/index.js")
    fi
fi

# Check 5: Resource provider function exists
if [[ -f "$EDITOR_CPP" ]]; then
    cpp_content="$(cat "$EDITOR_CPP")"
    if ! echo "$cpp_content" | grep -qE "getResource.*String.*url"; then
        ISSUES+=("getResource() function not found - Resource provider won't work")
    fi
fi

# Check 6: Resource loading mechanism exists
if [[ -f "$EDITOR_CPP" ]]; then
    cpp_content="$(cat "$EDITOR_CPP")"
    if ! echo "$cpp_content" | grep -qE "getZipFile|createAssetInputStream|BinaryData::getNamedResource"; then
        ISSUES+=("No resource loading mechanism found - Need getZipFile(), createAssetInputStream(), or BinaryData::getNamedResource()")
    fi
fi

# Report results
echo ""
if [[ ${#ISSUES[@]} -eq 0 && ${#WARNINGS[@]} -eq 0 ]]; then
    success "All checks passed! WebView setup looks correct."
    exit 0
fi

if [[ ${#ISSUES[@]} -gt 0 ]]; then
    error "CRITICAL ISSUES FOUND:"
    for issue in "${ISSUES[@]}"; do
        error "  [X] $issue"
    done
fi

if [[ ${#WARNINGS[@]} -gt 0 ]]; then
    echo ""
    warn "WARNINGS:"
    for warning in "${WARNINGS[@]}"; do
        warn "  [!] $warning"
    done
fi

echo ""
info "Validation Summary:"
if [[ ${#ISSUES[@]} -gt 0 ]]; then
    error "  Issues: ${#ISSUES[@]}"
else
    success "  Issues: 0"
fi
if [[ ${#WARNINGS[@]} -gt 0 ]]; then
    warn "  Warnings: ${#WARNINGS[@]}"
else
    success "  Warnings: 0"
fi

if [[ ${#ISSUES[@]} -gt 0 ]]; then
    echo ""
    info "See templates in .claude/templates/webview/ for correct implementation"
    exit 1
fi

exit 0
