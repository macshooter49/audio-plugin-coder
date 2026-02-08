#!/usr/bin/env bash
# APC Master Builder with Enhanced Error Detection and Testing (macOS/Linux)
# Port of build-and-install.ps1 — Xcode generator, AU support, macOS install paths.

set -euo pipefail

usage() {
    cat << EOF
Usage: $(basename "$0") -p <PluginName> [--no-install] [--skip-tests] [--strict]

  -p, --plugin    Plugin name (required)
  --no-install    Skip installation step
  --skip-tests    Skip PluginVal validation
  --strict        Enable strict PluginVal mode
  -h, --help      Show this help
EOF
    exit 1
}

# Parse arguments
PLUGIN_NAME=""
NO_INSTALL=false
SKIP_TESTS=false
STRICT=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--plugin) PLUGIN_NAME="$2"; shift 2 ;;
        --no-install) NO_INSTALL=true; shift ;;
        --skip-tests) SKIP_TESTS=true; shift ;;
        --strict) STRICT=true; shift ;;
        -h|--help) usage ;;
        *) error "Unknown option: $1"; usage ;;
    esac
done

if [[ -z "$PLUGIN_NAME" ]]; then
    error "Plugin name is required."
    usage
fi

# Import required modules
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"
source "$SCRIPT_DIR/state-management.sh"
source "$SCRIPT_DIR/error-detection.sh"
source "$SCRIPT_DIR/terminal-monitoring.sh"
source "$SCRIPT_DIR/pluginval-integration.sh"

BUILD_DIR="$REPO_ROOT/build"
PLATFORM="$(detect_platform)"

info "--- APC BUILDER: $PLUGIN_NAME ---"

# Validate prerequisites
if [[ -f "$REPO_ROOT/plugins/$PLUGIN_NAME/status.json" ]]; then
    current_phase=$(json_read "$REPO_ROOT/plugins/$PLUGIN_NAME/status.json" "current_phase" 2>/dev/null || echo "")
    if [[ "$current_phase" != "code_complete" && "$SKIP_TESTS" != "true" ]]; then
        warn "Plugin implementation not marked as complete. Use --skip-tests to override."
    fi
fi

# 1. Configure with error monitoring
warn "Configuring build..."

CONFIGURE_CMD="cmake -S \"$REPO_ROOT\" -B \"$BUILD_DIR\""

case "$PLATFORM" in
    macos)
        CONFIGURE_CMD+=" -G Xcode"
        CONFIGURE_CMD+=" -DCMAKE_OSX_ARCHITECTURES=\"x86_64;arm64\""
        CONFIGURE_CMD+=" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13"
        ;;
    linux)
        CONFIGURE_CMD+=" -G \"Unix Makefiles\""
        CONFIGURE_CMD+=" -DCMAKE_BUILD_TYPE=Release"
        ;;
esac

CONFIGURE_CMD+=" --fresh"

if run_monitored "$CONFIGURE_CMD" 120 true; then
    : # Success
else
    config_exit=$?
    if [[ -n "${_MONITORED_OUTPUT:-}" && $_MONITORED_ERROR_COUNT -gt 0 ]]; then
        warn "Configuration errors detected"
        errors_json=$(parse_build_errors "$_MONITORED_OUTPUT")
        if known_issue=$(find_known_issue "$errors_json" 2>/dev/null); then
            info "Known configuration issue detected"
            apply_known_solution "$known_issue"
            # Retry
            run_monitored "$CONFIGURE_CMD" 120 true || {
                error "Configuration failed after applying known fix"
                exit 1
            }
        else
            error "Configuration failed"
            exit 1
        fi
    fi
fi

# 2. Build VST3 with error monitoring
warn "Compiling VST3..."

BUILD_VST3_CMD="cmake --build \"$BUILD_DIR\" --config Release --target \"${PLUGIN_NAME}_VST3\""

if ! run_monitored "$BUILD_VST3_CMD" 300 true; then
    error "VST3 build errors detected"

    errors_json=$(parse_build_errors "${_MONITORED_OUTPUT:-}")
    if known_issue=$(find_known_issue "$errors_json" 2>/dev/null); then
        info "Known issue detected"
        apply_known_solution "$known_issue"
        # Retry
        if ! run_monitored "$BUILD_VST3_CMD" 300 true; then
            errors_json=$(parse_build_errors "${_MONITORED_OUTPUT:-}")
            new_issue_from_error "$errors_json" "${_MONITORED_OUTPUT:-}"
            error "VST3 build failed - issue logged for investigation"
            exit 1
        fi
    else
        new_issue_from_error "$errors_json" "${_MONITORED_OUTPUT:-}"
        error "VST3 build failed - issue logged for investigation"
        exit 1
    fi
fi

# 3. Build AU (macOS only)
if [[ "$PLATFORM" == "macos" ]]; then
    warn "Compiling AU..."
    BUILD_AU_CMD="cmake --build \"$BUILD_DIR\" --config Release --target \"${PLUGIN_NAME}_AU\""
    if ! run_monitored "$BUILD_AU_CMD" 300 true; then
        warn "AU build failed (non-fatal, continuing...)"
    fi
fi

# 4. Build Standalone with error monitoring
warn "Compiling Standalone..."

BUILD_STANDALONE_CMD="cmake --build \"$BUILD_DIR\" --config Release --target \"${PLUGIN_NAME}_Standalone\""

if ! run_monitored "$BUILD_STANDALONE_CMD" 300 true; then
    error "Standalone build errors detected"

    errors_json=$(parse_build_errors "${_MONITORED_OUTPUT:-}")
    if known_issue=$(find_known_issue "$errors_json" 2>/dev/null); then
        info "Known issue detected"
        apply_known_solution "$known_issue"
        if ! run_monitored "$BUILD_STANDALONE_CMD" 300 true; then
            errors_json=$(parse_build_errors "${_MONITORED_OUTPUT:-}")
            new_issue_from_error "$errors_json" "${_MONITORED_OUTPUT:-}"
            error "Standalone build failed - issue logged for investigation"
            exit 1
        fi
    else
        new_issue_from_error "$errors_json" "${_MONITORED_OUTPUT:-}"
        error "Standalone build failed - issue logged for investigation"
        exit 1
    fi
fi

# 5. Build LV2 (Linux only)
if [[ "$PLATFORM" == "linux" ]]; then
    warn "Compiling LV2..."
    BUILD_LV2_CMD="cmake --build \"$BUILD_DIR\" --config Release --target \"${PLUGIN_NAME}_LV2\""
    run_monitored "$BUILD_LV2_CMD" 300 true || warn "LV2 build failed (non-fatal)"
fi

# 6. Run PluginVal tests
if [[ "$SKIP_TESTS" != "true" ]]; then
    warn "Running PluginVal validation..."

    # Find the built VST3 plugin
    VST3_ARTIFACT=$(find "$BUILD_DIR" -name "${PLUGIN_NAME}.vst3" -type d 2>/dev/null | head -1)
    if [[ -n "$VST3_ARTIFACT" ]]; then
        local_strict=""
        [[ "$STRICT" == "true" ]] && local_strict="--strict"
        result=$(test_with_pluginval "$VST3_ARTIFACT" "$PLUGIN_NAME" "$_PLUGINVAL_DEFAULT" $local_strict)

        passed=$(echo "$result" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('passed',False))" 2>/dev/null || echo "false")
        if [[ "$passed" != "True" && "$passed" != "true" ]]; then
            error "PluginVal tests failed"
            if [[ "$STRICT" == "true" ]]; then
                error "PluginVal validation failed in strict mode"
                exit 1
            else
                warn "PluginVal tests failed - proceeding with installation anyway"
            fi
        fi
    else
        warn "VST3 plugin not found for PluginVal testing"
    fi
fi

# 7. Install plugins
if [[ "$NO_INSTALL" != "true" ]]; then
    case "$PLATFORM" in
        macos)
            # Install VST3
            VST3_ARTIFACT=$(find "$BUILD_DIR" -name "${PLUGIN_NAME}.vst3" -type d 2>/dev/null | head -1)
            if [[ -n "$VST3_ARTIFACT" ]]; then
                VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3/${PLUGIN_NAME}.vst3"
                rm -rf "$VST3_DEST"
                cp -R "$VST3_ARTIFACT" "$VST3_DEST"
                success "INSTALLED VST3 to: $VST3_DEST"
            fi

            # Install AU
            AU_ARTIFACT=$(find "$BUILD_DIR" -name "${PLUGIN_NAME}.component" -type d 2>/dev/null | head -1)
            if [[ -n "$AU_ARTIFACT" ]]; then
                AU_DEST="$HOME/Library/Audio/Plug-Ins/Components/${PLUGIN_NAME}.component"
                rm -rf "$AU_DEST"
                cp -R "$AU_ARTIFACT" "$AU_DEST"
                success "INSTALLED AU to: $AU_DEST"
            fi

            # Report Standalone location
            STANDALONE=$(find "$BUILD_DIR" -name "${PLUGIN_NAME}.app" -type d 2>/dev/null | head -1)
            if [[ -n "$STANDALONE" ]]; then
                success "STANDALONE built at: $STANDALONE"
            fi
            ;;
        linux)
            # Install VST3
            VST3_ARTIFACT=$(find "$BUILD_DIR" -name "${PLUGIN_NAME}.vst3" -type d 2>/dev/null | head -1)
            if [[ -n "$VST3_ARTIFACT" ]]; then
                VST3_DEST="$HOME/.vst3/${PLUGIN_NAME}.vst3"
                rm -rf "$VST3_DEST"
                mkdir -p "$HOME/.vst3"
                cp -R "$VST3_ARTIFACT" "$VST3_DEST"
                success "INSTALLED VST3 to: $VST3_DEST"
            fi

            # Report Standalone location
            STANDALONE=$(find "$BUILD_DIR" -name "$PLUGIN_NAME" -type f -executable 2>/dev/null | head -1)
            if [[ -n "$STANDALONE" ]]; then
                success "STANDALONE built at: $STANDALONE"
            fi
            ;;
    esac
fi

# 8. Update build status
STATUS_PATH="$REPO_ROOT/plugins/$PLUGIN_NAME/status.json"
if [[ -f "$STATUS_PATH" ]]; then
    NOW="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    json_write "$STATUS_PATH" "validation.build_completed" "true" bool
    json_write "$STATUS_PATH" "validation.build_timestamp" "$NOW"
fi

success "Build process complete!"
