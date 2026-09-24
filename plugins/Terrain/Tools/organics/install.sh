#!/usr/bin/env bash
# install.sh — put the compiled Organics library where Terrain looks for it (contract §3).
#
#   Tools/organics/install.sh [--copy] [--src DIR] [--dst DIR] [--dry-run]
#
# Default: SYMLINK every compiled instrument folder (and index.json / ids.json) from
#   ~/Developer/VST-Plugins/organics-library/compiled
# into
#   ~/Library/Application Support/WavesCrate/Terrain/Organics
# --copy copies instead of linking (use it for a machine without the build tree).
#
# Safety: this script only creates/replaces entries whose names match a compiled instrument id or
# index.json / ids.json. It never deletes anything else in the destination (user content, other
# installs, older instruments that are no longer compiled all stay untouched).
#
# The runtime also honours TERRAIN_ORGANICS_DIR, so a dev can skip installing entirely:
#   export TERRAIN_ORGANICS_DIR=~/Developer/VST-Plugins/organics-library/compiled
#
# Windows: the library root is %APPDATA%\WavesCrate\Terrain\Organics. From PowerShell:
#   $src = "$HOME\Developer\VST-Plugins\organics-library\compiled"
#   $dst = "$env:APPDATA\WavesCrate\Terrain\Organics"
#   New-Item -ItemType Directory -Force -Path $dst | Out-Null
#   Get-ChildItem $src | ForEach-Object { Copy-Item $_.FullName -Destination $dst -Recurse -Force }
# (or set the TERRAIN_ORGANICS_DIR environment variable to $src). Robocopy /E works too; do NOT use
# /MIR, which would delete other content in the destination.
set -euo pipefail

SRC="${HOME}/Developer/VST-Plugins/organics-library/compiled"
DST="${HOME}/Library/Application Support/WavesCrate/Terrain/Organics"
MODE="link"
DRY=0
while [ $# -gt 0 ]; do
  case "$1" in
    --copy) MODE="copy" ;;
    --src) SRC="$2"; shift ;;
    --dst) DST="$2"; shift ;;
    --dry-run) DRY=1 ;;
    -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

if [ ! -f "$SRC/index.json" ]; then
  echo "no compiled library at $SRC (run Tools/organics/torgc.py --all first)" >&2
  exit 1
fi

run() { if [ "$DRY" = 1 ]; then echo "+ $*"; else "$@"; fi; }

run mkdir -p "$DST"
n=0
for item in "$SRC"/*; do
  name="$(basename "$item")"
  case "$name" in
    index.json|ids.json) ;;
    *) [ -f "$item/map.json" ] || continue ;;      # only compiled instrument folders
  esac
  target="$DST/$name"
  # replace only what we own: a previous link/copy of the same instrument or index file
  if [ -L "$target" ]; then
    run rm "$target"
  elif [ -e "$target" ]; then
    if [ "$MODE" = "link" ] || [ -d "$target" ]; then
      run rm -rf "$target"
    fi
  fi
  if [ "$MODE" = "link" ]; then
    run ln -s "$item" "$target"
  else
    run cp -R "$item" "$target"
  fi
  n=$((n + 1))
done
echo "installed $n entries ($MODE) into $DST"
