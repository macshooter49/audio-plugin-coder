#!/usr/bin/env bash
# APC State Management Module (macOS/Linux)
# Provides standardized state management for APC plugin development workflow.
# Port of state-management.ps1 — uses python3 for all JSON manipulation.

# Guard against double-sourcing
[[ -n "${_APC_STATE_MANAGEMENT_LOADED:-}" ]] && return 0
_APC_STATE_MANAGEMENT_LOADED=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# --- STATE SCHEMA ---
STATE_REQUIRED_FIELDS=(
    plugin_name version current_phase ui_framework
    complexity_score created_at last_modified phase_history
    validation framework_selection error_recovery
)
STATE_PHASES=(ideation plan design code ship complete)
STATE_FRAMEWORKS=(visage webview pending)
STATE_VALIDATION_FIELDS=(
    creative_brief_exists parameter_spec_exists architecture_defined
    ui_framework_selected design_complete code_complete
    tests_passed ship_ready
)

# --- FUNCTIONS ---

# new_plugin_state <plugin_name> <plugin_path>
# Initialize a new plugin state from template.
new_plugin_state() {
    local plugin_name="$1"
    local plugin_path="$2"

    # Try multiple locations for the template
    local template_path=""
    for p in \
        "$REPO_ROOT/.kilocode/templates/status-template.json" \
        "$REPO_ROOT/.claude/templates/status-template.json" \
        "$REPO_ROOT/templates/status-template.json"; do
        if [[ -f "$p" ]]; then
            template_path="$p"
            break
        fi
    done

    if [[ -z "$template_path" ]]; then
        warn "Status template not found."
        return 1
    fi

    local status_path="$plugin_path/status.json"
    local now
    now="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

    python3 -c "
import json, sys

with open('$template_path') as f:
    tpl = json.load(f)

tpl['plugin_name'] = '$plugin_name'
tpl['created_at'] = '$now'
tpl['last_modified'] = '$now'
if not isinstance(tpl.get('phase_history'), list):
    tpl['phase_history'] = []

with open('$status_path', 'w') as f:
    json.dump(tpl, f, indent=2, ensure_ascii=False)
    f.write('\n')
"
    success "Initialized state for $plugin_name"
}

# update_plugin_state <plugin_path> [--phase <phase>] [--framework <framework>] [--set key=value ...]
# Update plugin state. Supports dot-notation keys (e.g. validation.build_completed=true).
update_plugin_state() {
    local plugin_path="$1"; shift
    local phase="" framework=""
    local -a sets=()

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --phase) phase="$2"; shift 2 ;;
            --framework) framework="$2"; shift 2 ;;
            --set) sets+=("$2"); shift 2 ;;
            *) shift ;;
        esac
    done

    local status_path="$plugin_path/status.json"
    if [[ ! -f "$status_path" ]]; then
        warn "Status file not found at $status_path"
        return 1
    fi

    # Build a JSON object of updates
    local updates_json="{}"
    for kv in "${sets[@]+"${sets[@]}"}"; do
        local key="${kv%%=*}"
        local val="${kv#*=}"
        updates_json=$(python3 -c "
import json, sys
d = json.loads('$updates_json')
d['$key'] = '$val'
print(json.dumps(d))
")
    done

    # Allowed phases and frameworks as comma-separated strings for python
    local phases_csv
    phases_csv=$(IFS=,; echo "${STATE_PHASES[*]}")
    local frameworks_csv
    frameworks_csv=$(IFS=,; echo "${STATE_FRAMEWORKS[*]}")

    local tmp_py
    tmp_py="$(mktemp)"
    cat > "$tmp_py" << PYEOF
import json, sys
from datetime import datetime, timezone

status_path = "$status_path"
phase = "$phase"
framework = "$framework"
updates = json.loads('$updates_json')
valid_phases = "$phases_csv".split(",")
valid_frameworks = "$frameworks_csv".split(",")
required_fields = "$(IFS=,; echo "${STATE_REQUIRED_FIELDS[*]}")".split(",")
validation_fields = "$(IFS=,; echo "${STATE_VALIDATION_FIELDS[*]}")".split(",")

with open(status_path) as f:
    state = json.load(f)

now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

# Apply dot-notation updates
for key, val in updates.items():
    parts = key.split(".")
    if len(parts) == 2:
        root, child = parts
        if root in state and isinstance(state[root], dict):
            if val in ("true", "True"): val = True
            elif val in ("false", "False"): val = False
            else:
                try: val = int(val)
                except ValueError:
                    try: val = float(val)
                    except ValueError: pass
            state[root][child] = val
        else:
            print(f"Warning: Root property '{root}' not found in state.", file=sys.stderr)
    else:
        if key in state:
            if val in ("true", "True"): val = True
            elif val in ("false", "False"): val = False
            else:
                try: val = int(val)
                except ValueError:
                    try: val = float(val)
                    except ValueError: pass
            state[key] = val
        else:
            print(f"Warning: Property '{key}' not found in state.", file=sys.stderr)

# Update phase
if phase and phase in valid_phases:
    state["current_phase"] = phase
    state["last_modified"] = now
    entry = {
        "phase": phase,
        "completed_at": now,
        "framework_selected": framework if framework else state.get("ui_framework", "pending")
    }
    if not isinstance(state.get("phase_history"), list):
        state["phase_history"] = []
    state["phase_history"].append(entry)

# Update framework
if framework and framework in valid_frameworks:
    state["ui_framework"] = framework
    if "framework_selection" in state and isinstance(state["framework_selection"], dict):
        state["framework_selection"]["decision"] = framework
    state["last_modified"] = now

# --- Validate schema before saving ---
props = set(state.keys())
for field in required_fields:
    if field and field not in props:
        print(f"Warning: Missing required field: {field}", file=sys.stderr)
        sys.exit(1)

if state.get("current_phase") not in valid_phases:
    print(f"Warning: Invalid phase: {state.get('current_phase')}", file=sys.stderr)
    sys.exit(1)

if state.get("ui_framework") not in valid_frameworks:
    print(f"Warning: Invalid framework: {state.get('ui_framework')}", file=sys.stderr)
    sys.exit(1)

if "validation" in state and isinstance(state["validation"], dict):
    val_props = set(state["validation"].keys())
    for field in validation_fields:
        if field and field not in val_props:
            print(f"Warning: Missing validation field: {field}", file=sys.stderr)
            sys.exit(1)

with open(status_path, "w") as f:
    json.dump(state, f, indent=2, ensure_ascii=False)
    f.write("\n")
PYEOF

    python3 "$tmp_py"
    local rc=$?
    rm -f "$tmp_py"

    if [[ $rc -eq 0 ]]; then
        success "Updated state: ${phase:-unchanged} (${framework:-unchanged})"
    else
        warn "State validation failed during update."
    fi
    return $rc
}

# test_plugin_state <plugin_path> [--phase <required_phase>] [--files file1 file2 ...]
# Validate plugin state prerequisites.
test_plugin_state() {
    local plugin_path="$1"; shift
    local required_phase=""
    local -a required_files=()

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --phase) required_phase="$2"; shift 2 ;;
            --files) shift; while [[ $# -gt 0 && "$1" != --* ]]; do required_files+=("$1"); shift; done ;;
            *) shift ;;
        esac
    done

    local status_path="$plugin_path/status.json"
    if [[ ! -f "$status_path" ]]; then
        warn "Status file not found"
        return 1
    fi

    # Schema validation
    local phases_csv
    phases_csv=$(IFS=,; echo "${STATE_PHASES[*]}")
    local frameworks_csv
    frameworks_csv=$(IFS=,; echo "${STATE_FRAMEWORKS[*]}")

    if ! python3 -c "
import json, sys

with open('$status_path') as f:
    state = json.load(f)

required = '$(IFS=,; echo "${STATE_REQUIRED_FIELDS[*]}")'.split(',')
phases = '$phases_csv'.split(',')
frameworks = '$frameworks_csv'.split(',')
val_fields = '$(IFS=,; echo "${STATE_VALIDATION_FIELDS[*]}")'.split(',')

for field in required:
    if field and field not in state:
        print(f'Missing required field: {field}', file=sys.stderr)
        sys.exit(1)

if state.get('current_phase') not in phases:
    print(f'Invalid phase: {state.get(\"current_phase\")}', file=sys.stderr)
    sys.exit(1)

if state.get('ui_framework') not in frameworks:
    print(f'Invalid framework: {state.get(\"ui_framework\")}', file=sys.stderr)
    sys.exit(1)

if 'validation' in state and isinstance(state['validation'], dict):
    for field in val_fields:
        if field and field not in state['validation']:
            print(f'Missing validation field: {field}', file=sys.stderr)
            sys.exit(1)
"; then
        warn "State schema validation failed"
        return 1
    fi

    # Check required phase
    if [[ -n "$required_phase" ]]; then
        local current_phase
        current_phase=$(json_read "$status_path" "current_phase")

        # Get phase indices
        local req_idx=-1 cur_idx=-1 i=0
        for p in "${STATE_PHASES[@]}"; do
            [[ "$p" == "$required_phase" ]] && req_idx=$i
            [[ "$p" == "$current_phase" ]] && cur_idx=$i
            ((i++))
        done

        if [[ $cur_idx -lt $req_idx ]]; then
            warn "Cannot proceed: Current phase '$current_phase' must complete '$required_phase' first"
            return 1
        fi
    fi

    # Check required files
    for f in "${required_files[@]+"${required_files[@]}"}"; do
        if [[ ! -e "$plugin_path/$f" ]]; then
            warn "Required file missing: $f"
            return 1
        fi
    done

    return 0
}

# get_plugin_state <plugin_path>
# Prints the full JSON state to stdout (for piping to other tools).
get_plugin_state() {
    local plugin_path="$1"
    local status_path="$plugin_path/status.json"
    if [[ ! -f "$status_path" ]]; then
        return 1
    fi
    cat "$status_path"
}

# backup_plugin_state <plugin_path>
# Creates a timestamped backup of status.json. Prints backup path.
backup_plugin_state() {
    local plugin_path="$1"
    local status_path="$plugin_path/status.json"
    if [[ ! -f "$status_path" ]]; then
        return 1
    fi

    local backup_dir="$plugin_path/_state_backups"
    mkdir -p "$backup_dir"

    local timestamp
    timestamp="$(date +"%Y%m%d_%H%M%S")"
    local backup_file="$backup_dir/status_backup_${timestamp}.json"

    cp "$status_path" "$backup_file"
    warn "State backed up to $backup_file"

    # Update error recovery info
    json_write "$status_path" "error_recovery.last_backup" "$backup_file"
    json_write "$status_path" "error_recovery.rollback_available" "true" bool

    echo "$backup_file"
}

# restore_plugin_state <plugin_path> [backup_file]
# Restores from a backup. If no file specified, uses the latest.
restore_plugin_state() {
    local plugin_path="$1"
    local backup_file="${2:-}"
    local status_path="$plugin_path/status.json"
    local backup_dir="$plugin_path/_state_backups"

    if [[ ! -d "$backup_dir" ]]; then
        warn "No backup directory found"
        return 1
    fi

    if [[ -n "$backup_file" && -f "$backup_file" ]]; then
        local source="$backup_file"
    else
        # Get latest backup
        local source
        source="$(ls -t "$backup_dir"/status_backup_*.json 2>/dev/null | head -1)"
        if [[ -z "$source" ]]; then
            warn "No backup files found"
            return 1
        fi
    fi

    warn "Restoring state from $source"
    cp "$source" "$status_path"

    # Update error recovery info
    json_write "$status_path" "error_recovery.rollback_available" "false" bool

    local now
    now="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    json_append_array "$status_path" "error_recovery.error_log" "\"Rollback performed from $source at $now\""

    success "State restored"
}

# set_plugin_framework <plugin_path> <framework> <rationale>
# Sets the UI framework selection (visage or webview).
set_plugin_framework() {
    local plugin_path="$1"
    local framework="$2"
    local rationale="$3"

    if [[ "$framework" != "visage" && "$framework" != "webview" ]]; then
        error "Invalid framework: $framework (must be 'visage' or 'webview')"
        return 1
    fi

    # Validate planning phase is complete
    if ! test_plugin_state "$plugin_path" --phase "plan"; then
        error "Cannot set framework - planning phase not complete (Check status.json)"
        return 1
    fi

    update_plugin_state "$plugin_path" \
        --framework "$framework" \
        --set "ui_framework=$framework" \
        --set "framework_selection.decision=$framework" \
        --set "framework_selection.rationale=$rationale" \
        --set "framework_selection.implementation_strategy=single-pass"

    if [[ $? -eq 0 ]]; then
        success "Framework set to $framework"
    else
        warn "Failed to set framework"
        return 1
    fi
}

# test_canvas_implementation <plugin_path>
# Validate that WebView implementation uses canvas-based rendering.
test_canvas_implementation() {
    local plugin_path="$1"
    local index_path="$plugin_path/Design/index.html"

    if [[ ! -f "$index_path" ]]; then
        warn "WebView index.html not found"
        return 1
    fi

    local html
    html="$(cat "$index_path")"

    if ! echo "$html" | grep -q "canvas"; then
        warn "Missing canvas element in WebView implementation"
        return 1
    fi

    if ! echo "$html" | grep -qE "getContext\([\"']2d[\"']\)"; then
        warn "Missing canvas 2D context initialization"
        return 1
    fi

    if ! echo "$html" | grep -qE "fillRect|strokeRect|arc|beginPath|fill|stroke"; then
        warn "Missing canvas drawing operations (fillRect, arc, etc)"
        return 1
    fi

    if ! echo "$html" | grep -qiE "juce|frontend"; then
        warn "Missing JUCE frontend library integration"
        return 1
    fi

    success "Canvas implementation validated"
}

# add_state_error <plugin_path> <error_message>
# Appends an error message to the error recovery log.
add_state_error() {
    local plugin_path="$1"
    local error_message="$2"
    local status_path="$plugin_path/status.json"

    if [[ ! -f "$status_path" ]]; then
        return 1
    fi

    local now
    now="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
    json_append_array "$status_path" "error_recovery.error_log" "\"${now}: ${error_message}\""
}
