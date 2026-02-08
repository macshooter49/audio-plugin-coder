#!/usr/bin/env bash
# PluginVal Integration Module for APC Build Process (macOS/Linux)
# Port of pluginval-integration.ps1 — downloads macOS pluginval, runs validation.

[[ -n "${_APC_PLUGINVAL_LOADED:-}" ]] && return 0
_APC_PLUGINVAL_LOADED=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"
source "$SCRIPT_DIR/state-management.sh"

# Default pluginval path
_PLUGINVAL_DEFAULT="_tools/pluginval/pluginval"

# test_with_pluginval <plugin_path> <plugin_name> [pluginval_path] [--strict] [--verbose]
# Runs pluginval on a built plugin, parses results, updates status.json.
test_with_pluginval() {
    local plugin_path="$1"
    local plugin_name="$2"
    local pluginval_path="${3:-$_PLUGINVAL_DEFAULT}"
    local strict=false
    local verbose=false

    shift 2
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --strict) strict=true; shift ;;
            --verbose) verbose=true; shift ;;
            *) shift ;;
        esac
    done

    info "Running PluginVal tests..."

    # Resolve pluginval path relative to repo root
    if [[ ! "$pluginval_path" = /* ]]; then
        pluginval_path="$REPO_ROOT/$pluginval_path"
    fi

    # Check if pluginval exists
    if [[ ! -x "$pluginval_path" ]]; then
        # Try macOS .app bundle
        local app_path="$REPO_ROOT/_tools/pluginval/pluginval.app/Contents/MacOS/pluginval"
        if [[ -x "$app_path" ]]; then
            pluginval_path="$app_path"
        else
            warn "PluginVal not found at $pluginval_path"
            warn "Skipping PluginVal tests"
            echo '{"passed":false,"skipped":true,"reason":"PluginVal not found"}'
            return 0
        fi
    fi

    # Validate plugin exists
    if [[ ! -e "$plugin_path" ]]; then
        error "Plugin not found at $plugin_path"
        echo '{"passed":false,"skipped":false,"reason":"Plugin file not found"}'
        return 1
    fi

    # Build arguments
    local -a args=("--validate" "$plugin_path")
    [[ "$verbose" == "true" ]] && args+=("--verbose")
    [[ "$strict" == "true" ]] && args+=("--strict")

    local start_time
    start_time=$(date +%s)
    local tmpout
    tmpout="$(mktemp)"
    local tmperr
    tmperr="$(mktemp)"

    # Run pluginval
    local exit_code=0
    "$pluginval_path" "${args[@]}" > "$tmpout" 2> "$tmperr" || exit_code=$?

    local end_time
    end_time=$(date +%s)
    local duration=$((end_time - start_time))

    local output
    output="$(cat "$tmpout" 2>/dev/null)"
    local error_output
    error_output="$(cat "$tmperr" 2>/dev/null)"
    rm -f "$tmpout" "$tmperr"

    # Analyze results
    local passed=false
    if echo "$output" | grep -q "SUCCESS"; then
        passed=true
    fi

    # Parse test categories
    local basic_result="" param_result="" processing_result=""
    if echo "$output" | grep -qE "Basic tests:\s*PASSED"; then basic_result="PASSED"; fi
    if echo "$output" | grep -qE "Basic tests:\s*FAILED"; then basic_result="FAILED"; fi
    if echo "$output" | grep -qE "Parameter tests:\s*PASSED"; then param_result="PASSED"; fi
    if echo "$output" | grep -qE "Parameter tests:\s*FAILED"; then param_result="FAILED"; fi
    if echo "$output" | grep -qE "Processing tests:\s*PASSED"; then processing_result="PASSED"; fi
    if echo "$output" | grep -qE "Processing tests:\s*FAILED"; then processing_result="FAILED"; fi

    # Build output summary
    local summary
    summary=$(echo "$output" | grep -E "(PASSED|FAILED|ERROR)" | head -10 | tr '\n' '; ')

    # Update status.json
    local now
    now="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    local status_path="$REPO_ROOT/plugins/$plugin_name/status.json"
    if [[ -f "$status_path" ]]; then
        json_write "$status_path" "validation.tests_passed" "$passed" bool
    fi

    # Report results
    if [[ "$passed" == "true" ]]; then
        success "PluginVal tests PASSED!"
        gray "Duration: ${duration}s"
    else
        error "PluginVal tests FAILED!"
        gray "Duration: ${duration}s"

        if [[ "$verbose" == "true" || -n "$basic_result" ]]; then
            warn "Test Results:"
            [[ -n "$basic_result" ]] && echo "  Basic Tests: $basic_result"
            [[ -n "$param_result" ]] && echo "  Parameter Tests: $param_result"
            [[ -n "$processing_result" ]] && echo "  Processing Tests: $processing_result"
        fi
    fi

    # Return JSON result
    echo "{\"passed\":$passed,\"skipped\":false,\"exit_code\":$exit_code,\"duration\":$duration}"
    return 0
}

# get_pluginval_report <plugin_name>
# Displays the last pluginval results from status.json.
get_pluginval_report() {
    local plugin_name="$1"
    local status_path="$REPO_ROOT/plugins/$plugin_name/status.json"

    if [[ ! -f "$status_path" ]]; then
        warn "No status.json found for $plugin_name"
        return 1
    fi

    local tests_passed
    tests_passed=$(json_read "$status_path" "validation.tests_passed" 2>/dev/null || echo "unknown")

    info "PluginVal Test Report for $plugin_name"
    echo "=================================================="
    if [[ "$tests_passed" == "true" ]]; then
        success "Status: PASSED"
    elif [[ "$tests_passed" == "false" ]]; then
        error "Status: FAILED"
    else
        warn "Status: No results found"
    fi
}

# install_pluginval [install_path]
# Downloads macOS pluginval from GitHub releases.
install_pluginval() {
    local install_path="${1:-$REPO_ROOT/_tools}"

    info "Installing PluginVal..."

    local platform
    platform="$(detect_platform)"
    local url=""

    case "$platform" in
        macos)
            url="https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip"
            ;;
        linux)
            url="https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Linux.zip"
            ;;
        *)
            error "Unsupported platform for pluginval download: $platform"
            return 1
            ;;
    esac

    local tmpzip
    tmpzip="$(mktemp -d)/pluginval.zip"

    if curl -fsSL "$url" -o "$tmpzip"; then
        mkdir -p "$install_path/pluginval"
        unzip -o "$tmpzip" -d "$install_path/pluginval"
        rm -f "$tmpzip"

        # Make executable
        if [[ -f "$install_path/pluginval/pluginval" ]]; then
            chmod +x "$install_path/pluginval/pluginval"
        fi

        success "PluginVal installed successfully to $install_path/pluginval"
        return 0
    else
        error "PluginVal download failed"
        return 1
    fi
}
