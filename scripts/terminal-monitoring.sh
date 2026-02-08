#!/usr/bin/env bash
# Terminal Output Monitoring Module for APC Build Process (macOS/Linux)
# Port of terminal-monitoring.ps1 — real-time error detection and timeout monitoring.

[[ -n "${_APC_TERMINAL_MONITORING_LOADED:-}" ]] && return 0
_APC_TERMINAL_MONITORING_LOADED=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# run_monitored <command_string> [timeout_seconds] [show_output]
# Runs a command with timeout and real-time error pattern detection.
# Prints JSON result: { output, errors, exit_code, duration }
run_monitored() {
    local command="$1"
    local timeout_seconds="${2:-300}"
    local show_output="${3:-true}"

    info "Starting terminal monitoring..."
    info "Executing: $command"

    local tmpout
    tmpout="$(mktemp)"
    local tmperr
    tmperr="$(mktemp)"
    local start_time
    start_time=$(date +%s)

    # Run command in background, capture output
    eval "$command" > "$tmpout" 2> "$tmperr" &
    local cmd_pid=$!

    # Watchdog: kill if exceeds timeout
    (
        sleep "$timeout_seconds"
        if kill -0 "$cmd_pid" 2>/dev/null; then
            kill "$cmd_pid" 2>/dev/null
            echo "TIMEOUT" > "${tmpout}.timeout"
        fi
    ) &
    local watchdog_pid=$!

    # Wait for command to finish
    local exit_code=0
    wait "$cmd_pid" 2>/dev/null || exit_code=$?

    # Kill watchdog if still running
    kill "$watchdog_pid" 2>/dev/null
    wait "$watchdog_pid" 2>/dev/null || true

    local end_time
    end_time=$(date +%s)
    local duration=$((end_time - start_time))

    # Check for timeout
    if [[ -f "${tmpout}.timeout" ]]; then
        rm -f "${tmpout}.timeout"
        error "Command timed out after ${timeout_seconds} seconds"
        local output
        output="$(cat "$tmpout" 2>/dev/null; cat "$tmperr" 2>/dev/null)"
        rm -f "$tmpout" "$tmperr"
        echo "{\"output\":\"\",\"errors\":[],\"exit_code\":124,\"duration\":$duration}"
        return 124
    fi

    local output
    output="$(cat "$tmpout" 2>/dev/null)"
    local err_output
    err_output="$(cat "$tmperr" 2>/dev/null)"
    local combined="${output}${err_output:+
$err_output}"

    rm -f "$tmpout" "$tmperr"

    # Show output if requested
    if [[ "$show_output" == "true" && -n "$combined" ]]; then
        echo "$combined"
    fi

    # Detect error patterns in output
    local error_patterns=("error:" "Error:" "ERROR" "failed" "Failed" "FAILED" "fatal error" "BUILD FAILED" "undefined symbol")
    local -a detected_errors=()

    while IFS= read -r line; do
        for pattern in "${error_patterns[@]}"; do
            if [[ "$line" == *"$pattern"* ]]; then
                detected_errors+=("$line")
                if [[ "$show_output" != "true" ]]; then
                    warn "Error pattern detected: $pattern"
                    error "   $line"
                fi
                break
            fi
        done
    done <<< "$combined"

    local errors_count=${#detected_errors[@]}

    success "Terminal monitoring complete (Duration: ${duration}s)"

    if [[ $errors_count -gt 0 ]]; then
        warn "Detected $errors_count error patterns during execution"
    fi

    # Return JSON-like result via environment variables (for use in caller)
    _MONITORED_OUTPUT="$combined"
    _MONITORED_EXIT_CODE="$exit_code"
    _MONITORED_DURATION="$duration"
    _MONITORED_ERROR_COUNT="$errors_count"

    return "$exit_code"
}

# test_build_health <build_output>
# Scores build output 0-100 based on success/error/warning patterns.
# Prints JSON: { success, warnings, errors, score }
test_build_health() {
    local build_output="$1"

    python3 -c "
import re, json, sys

output = sys.stdin.read()

health = {
    'success': True,
    'warnings': [],
    'errors': [],
    'score': 100
}

# macOS/Xcode/Clang success patterns
success_patterns = [
    r'BUILD SUCCEEDED',
    r'Build succeeded',
    r'cmake.*completed',
    r'xcodebuild.*succeeded',
    r'plugin.*built successfully',
    r'\*\* BUILD SUCCEEDED \*\*'
]

has_success = any(re.search(p, output, re.IGNORECASE) for p in success_patterns)

# Error patterns (Clang/Xcode)
error_patterns = [
    r'error:',
    r'fatal error:',
    r'CMake Error',
    r'BUILD FAILED',
    r'compilation failed',
    r'ld:.*error',
    r'undefined symbol',
    r'Undefined symbols for architecture',
    r'linker command failed'
]

for pattern in error_patterns:
    if re.search(pattern, output):
        health['errors'].append(pattern)
        health['score'] -= 20

# Warning patterns
warning_patterns = [
    r'warning:',
    r'CMake Warning',
    r'note:.*warning'
]

for pattern in warning_patterns:
    if re.search(pattern, output):
        health['warnings'].append(pattern)
        health['score'] -= 5

if not has_success and health['errors']:
    health['success'] = False

health['score'] = max(0, health['score'])

print(json.dumps(health, indent=2))
" <<< "$build_output"
}
