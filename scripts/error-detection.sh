#!/usr/bin/env bash
# Error Detection and Handling Module for APC Build Process (macOS/Linux)
# Port of error-detection.ps1 — Clang/Xcode/GCC error patterns.

[[ -n "${_APC_ERROR_DETECTION_LOADED:-}" ]] && return 0
_APC_ERROR_DETECTION_LOADED=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# --- ERROR PATTERNS ---

CMAKE_PATTERNS=(
    "CMake Error"
    "Could not find"
    "Target.*already exists"
    "duplicate target"
    "undefined reference"
    "linking failed"
    "compilation failed"
)

JUCE_PATTERNS=(
    "WebBrowserComponent.*error"
    "juce_gui_extra.*not found"
    "JUCE_WEB_BROWSER.*undefined"
    "Canvas.*not supported"
    "WKWebView.*error"
)

# Clang/Xcode/GCC patterns (replaces MSVC patterns)
COMPILER_PATTERNS=(
    "error:"
    "fatal error:"
    "ld: "
    "undefined symbol"
    "linker command failed"
    "BUILD FAILED"
    "clang: error"
    "Undefined symbols for architecture"
    "library not found"
)

# parse_build_errors <output_text>
# Parses build output for known error patterns. Prints JSON array of errors.
parse_build_errors() {
    local output="$1"

    python3 -c "
import json, re, sys

output = sys.stdin.read()

cmake_patterns = $(printf '%s\n' "${CMAKE_PATTERNS[@]}" | python3 -c "import json,sys; print(json.dumps(sys.stdin.read().strip().split('\n')))")
juce_patterns = $(printf '%s\n' "${JUCE_PATTERNS[@]}" | python3 -c "import json,sys; print(json.dumps(sys.stdin.read().strip().split('\n')))")
compiler_patterns = $(printf '%s\n' "${COMPILER_PATTERNS[@]}" | python3 -c "import json,sys; print(json.dumps(sys.stdin.read().strip().split('\n')))")

errors = []
for line in output.split('\n'):
    for pattern in cmake_patterns:
        if re.search(pattern, line):
            errors.append({'pattern': pattern, 'line': line.strip(), 'category': 'cmake'})
            break
    else:
        for pattern in juce_patterns:
            if re.search(pattern, line):
                errors.append({'pattern': pattern, 'line': line.strip(), 'category': 'webview'})
                break
        else:
            for pattern in compiler_patterns:
                if re.search(re.escape(pattern), line):
                    errors.append({'pattern': pattern, 'line': line.strip(), 'category': 'build'})
                    break

print(json.dumps(errors))
" <<< "$output"
}

# find_known_issue <errors_json>
# Searches known-issues.yaml for matching issue. Prints JSON result or empty string.
find_known_issue() {
    local errors_json="$1"

    local ts_dir
    ts_dir="$(find_troubleshooting_dir)"
    local known_issues_path="$ts_dir/known-issues.yaml"

    if [[ ! -f "$known_issues_path" ]]; then
        warn "Known issues database not found at $known_issues_path"
        return 1
    fi

    python3 -c "
import json, re, sys, os

errors = json.loads('''$errors_json''')
yaml_path = '$known_issues_path'
ts_dir = '$ts_dir'

with open(yaml_path) as f:
    yaml_content = f.read()

# Simple YAML parsing for error patterns
blocks = yaml_content.split('- id:')
blocks = [b for b in blocks if 'error_patterns' in b]

for block in blocks:
    lines = block.strip().split('\n')
    issue_id = lines[0].strip() if lines else ''

    # Extract title
    title = ''
    for line in lines:
        if 'title:' in line:
            title = line.split('title:')[1].strip().strip('\"')
            break

    # Extract error patterns
    patterns = []
    in_patterns = False
    for line in lines:
        if 'error_patterns:' in line:
            in_patterns = True
            continue
        if in_patterns:
            if line.strip().startswith('-'):
                p = line.strip().lstrip('-').strip().strip('\"')
                if p:
                    patterns.append(p)
            elif not line.strip().startswith('#') and ':' in line:
                in_patterns = False

    # Extract resolution file
    resolution_file = ''
    for line in lines:
        if 'resolution_file:' in line:
            resolution_file = line.split('resolution_file:')[1].strip()
            break

    # Match errors against patterns
    for err in errors:
        for pattern in patterns:
            try:
                if re.search(re.escape(pattern), err.get('line', '')) or re.search(re.escape(pattern), err.get('pattern', '')):
                    # Load solution if available
                    solution = ''
                    res_path = os.path.join(ts_dir, resolution_file) if resolution_file else ''
                    if res_path and os.path.isfile(res_path):
                        with open(res_path) as rf:
                            solution = rf.read()

                    result = {
                        'id': issue_id,
                        'title': title,
                        'solution': solution,
                        'resolution_file': resolution_file
                    }
                    print(json.dumps(result))
                    sys.exit(0)
            except re.error:
                pass

# No match
sys.exit(1)
"
}

# apply_known_solution <issue_json>
# Applies automated fix from a known issue's resolution file.
apply_known_solution() {
    local issue_json="$1"

    local title
    title=$(echo "$issue_json" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('title',''))")
    local solution
    solution=$(echo "$issue_json" | python3 -c "import json,sys; print(json.loads(sys.stdin.read()).get('solution',''))")

    info "Applying solution for: $title"

    if [[ -n "$solution" ]]; then
        warn "Solution:"
        echo "$solution"
        echo ""

        # Extract bash commands from solution markdown
        local commands
        commands=$(echo "$solution" | python3 -c "
import sys
lines = sys.stdin.read().split('\n')
in_block = False
commands = []
for line in lines:
    if line.strip().startswith('\`\`\`bash') or line.strip().startswith('\`\`\`sh'):
        in_block = True
        continue
    elif line.strip().startswith('\`\`\`') and in_block:
        in_block = False
        continue
    elif in_block:
        commands.append(line)
print('\n'.join(commands))
")

        if [[ -n "$commands" ]]; then
            success "Executing automated fix..."
            if eval "$commands"; then
                success "Automated fix applied successfully"
            else
                error "Automated fix failed"
                warn "Manual intervention required"
            fi
        else
            warn "No automated solution available. Please check the resolution file."
        fi
    fi
}

# new_issue_from_error <errors_json> <build_output>
# Auto-captures unresolved errors to known-issues.yaml and creates resolution doc.
new_issue_from_error() {
    local errors_json="$1"
    local build_output="$2"

    warn "Auto-capturing new issue..."

    local ts_dir
    ts_dir="$(find_troubleshooting_dir)"

    python3 -c "
import json, os, sys
from datetime import date

errors = json.loads('''$errors_json''')
build_output = '''$build_output'''
ts_dir = '$ts_dir'

if not errors:
    sys.exit(1)

category = errors[0].get('category', 'build')

# Count existing resolution files
res_dir = os.path.join(ts_dir, 'resolutions')
os.makedirs(res_dir, exist_ok=True)
existing = len([f for f in os.listdir(res_dir) if f.endswith('.md')])
new_id = f'{category}-{existing + 1:03d}'

# Create issue summary (max 100 chars)
error_summary = errors[0].get('line', 'Unknown error')
if len(error_summary) > 100:
    error_summary = error_summary[:97] + '...'

# Build new issue YAML entry
symptoms = '\n'.join(f'    - \"{e[\"line\"]}\"' for e in errors[:5])
patterns = '\n'.join(f'    - \"{e[\"pattern\"]}\"' for e in errors[:3])
today = date.today().strftime('%Y-%m-%d')

new_issue = f'''
  - id: {new_id}
    title: \"[Auto] {error_summary}\"
    category: {category}
    severity: high
    symptoms:
{symptoms}
    error_patterns:
{patterns}
    resolution_status: investigating
    resolution_file: resolutions/{new_id}.md
    frequency: 1
    last_occurred: {today}
    attempts_before_resolution: 1'''

# Append to known-issues.yaml
yaml_path = os.path.join(ts_dir, 'known-issues.yaml')
with open(yaml_path, 'a') as f:
    f.write(new_issue + '\n')

# Create resolution document from template
template_path = os.path.join(ts_dir, '_template.md')
if os.path.isfile(template_path):
    with open(template_path) as f:
        content = f.read()
    content = content.replace('[auto-generated-id]', new_id)
    content = content.replace('[Issue Title]', f'[Auto] {error_summary}')
    from datetime import datetime
    content = content.replace('[date]', datetime.now().strftime('%Y-%m-%d %H:%M'))

    # Add error details
    error_details = '\n## Build Output\n\n    ' + build_output[:2000] + '\n\n## Detected Errors\n'
    for e in errors:
        error_details += f'- **{e[\"pattern\"]}**: {e[\"line\"]}\n'
    content = content.replace('## Root Cause', error_details + '\n## Root Cause')

    res_path = os.path.join(res_dir, f'{new_id}.md')
    with open(res_path, 'w') as f:
        f.write(content)

print(f'Issue logged as {new_id}')
print(f'See: {ts_dir}/resolutions/{new_id}.md')
" 2>&1

    local rc=$?
    if [[ $rc -eq 0 ]]; then
        success "Issue captured successfully"
    else
        warn "Failed to capture issue"
    fi
    return $rc
}
