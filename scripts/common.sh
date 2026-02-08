#!/usr/bin/env bash
# APC Common Utilities for macOS/Linux
# Sourced by all other scripts — provides helpers and constants.

set -euo pipefail

# --- PATH RESOLUTION ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# --- COLOR OUTPUT HELPERS ---
_color_reset="\033[0m"
_color_red="\033[0;31m"
_color_green="\033[0;32m"
_color_yellow="\033[0;33m"
_color_cyan="\033[0;36m"
_color_gray="\033[0;90m"

info()    { printf "${_color_cyan}%s${_color_reset}\n" "$*"; }
warn()    { printf "${_color_yellow}%s${_color_reset}\n" "$*" >&2; }
error()   { printf "${_color_red}%s${_color_reset}\n" "$*" >&2; }
success() { printf "${_color_green}%s${_color_reset}\n" "$*"; }
gray()    { printf "${_color_gray}%s${_color_reset}\n" "$*"; }

# --- DEPENDENCY CHECKS ---

# require_command <cmd> [friendly_name]
# Exits with error if command is not found on PATH.
require_command() {
    local cmd="$1"
    local name="${2:-$cmd}"
    if ! command -v "$cmd" &>/dev/null; then
        error "Required command not found: $name ($cmd)"
        return 1
    fi
}

# version_gte <version> <minimum>
# Returns 0 (true) if version >= minimum. Supports dotted versions (e.g. 3.22.1).
version_gte() {
    local ver="$1" min="$2"
    python3 -c "
import sys
from packaging.version import Version
try:
    result = Version('$ver') >= Version('$min')
except Exception:
    # Fallback: simple tuple comparison
    v = tuple(int(x) for x in '$ver'.split('.'))
    m = tuple(int(x) for x in '$min'.split('.'))
    result = v >= m
sys.exit(0 if result else 1)
" 2>/dev/null || {
        # Pure-bash fallback when python packaging module unavailable
        python3 -c "
import sys
v = tuple(int(x) for x in '$ver'.split('.'))
m = tuple(int(x) for x in '$min'.split('.'))
sys.exit(0 if v >= m else 1)
"
    }
}

# detect_platform
# Prints "macos", "linux", or "windows" (via WSL detection).
detect_platform() {
    case "$(uname -s)" in
        Darwin) echo "macos" ;;
        Linux)
            if grep -qiE 'microsoft|wsl' /proc/version 2>/dev/null; then
                echo "windows-wsl"
            else
                echo "linux"
            fi
            ;;
        MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
        *) echo "unknown" ;;
    esac
}

# --- JSON HELPERS (uses python3, no jq dependency) ---

# json_read <file> <key_path>
# Reads a value from a JSON file. Dot-notation supported (e.g. "validation.build_completed").
json_read() {
    local file="$1" key="$2"
    python3 -c "
import json, sys
with open('$file') as f:
    data = json.load(f)
keys = '$key'.split('.')
obj = data
for k in keys:
    if isinstance(obj, dict):
        obj = obj.get(k)
    else:
        obj = None
        break
if obj is None:
    sys.exit(1)
if isinstance(obj, (dict, list)):
    print(json.dumps(obj))
elif isinstance(obj, bool):
    print('true' if obj else 'false')
else:
    print(obj)
"
}

# json_write <file> <key_path> <value> [--type bool|int|str|json]
# Writes a value into a JSON file. Creates intermediate keys if needed.
json_write() {
    local file="$1" key="$2" value="$3" vtype="${4:-str}"
    python3 -c "
import json, sys, os

file_path = '$file'
key_path = '$key'
raw_value = '''$value'''
vtype = '$vtype'

# Parse value based on type
if vtype == 'bool':
    val = raw_value.lower() in ('true', '1', 'yes')
elif vtype == 'int':
    val = int(raw_value)
elif vtype == 'json':
    val = json.loads(raw_value)
else:
    val = raw_value

with open(file_path) as f:
    data = json.load(f)

keys = key_path.split('.')
obj = data
for k in keys[:-1]:
    if k not in obj or not isinstance(obj[k], dict):
        obj[k] = {}
    obj = obj[k]
obj[keys[-1]] = val

with open(file_path, 'w') as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
    f.write('\n')
"
}

# json_append_array <file> <key_path> <json_element>
# Appends a JSON element to an array at the given key path.
json_append_array() {
    local file="$1" key="$2" element="$3"
    python3 -c "
import json

file_path = '$file'
key_path = '$key'
element = json.loads('''$element''')

with open(file_path) as f:
    data = json.load(f)

keys = key_path.split('.')
obj = data
for k in keys[:-1]:
    obj = obj[k]

last = keys[-1]
if last not in obj or not isinstance(obj[last], list):
    obj[last] = []
obj[last].append(element)

with open(file_path, 'w') as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
    f.write('\n')
"
}

# --- TROUBLESHOOTING PATHS ---
# Try .claude first, fall back to .kilocode, then .agent
find_troubleshooting_dir() {
    for dir in "$REPO_ROOT/.claude/troubleshooting" "$REPO_ROOT/.kilocode/troubleshooting" "$REPO_ROOT/.agent/troubleshooting"; do
        if [[ -d "$dir" ]]; then
            echo "$dir"
            return 0
        fi
    done
    echo "$REPO_ROOT/.claude/troubleshooting"
}

# Find template directory
find_template_dir() {
    for dir in "$REPO_ROOT/.claude/templates" "$REPO_ROOT/.kilocode/templates" "$REPO_ROOT/templates"; do
        if [[ -d "$dir" ]]; then
            echo "$dir"
            return 0
        fi
    done
    echo "$REPO_ROOT/templates"
}
