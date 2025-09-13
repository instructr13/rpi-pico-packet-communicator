#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PATCH_SET=(
  "$HOME/.platformio/packages/framework-arduinopico/libraries/Adafruit_TinyUSB_Arduino"
  "$REPO_ROOT/patches/fix-tusb-deadlock.patch"
)

apply_patch() {
  local root_path="$1"
  local patch_file="$2"

  if [ ! -d "$root_path" ]; then
    echo "Error: Directory $root_path does not exist."
    exit 1
  fi

  if [ ! -f "$patch_file" ]; then
    echo "Error: Patch file $patch_file does not exist."
    exit 1
  fi

  echo -n "Applying $(basename "$patch_file") to $root_path..."

  if patch -d "$root_path" -p1 --dry-run < "$patch_file" > /dev/null 2>&1; then
    patch -d "$root_path" -p1 < "$patch_file"
    echo " done."
  else
    echo " already applied or failed."
  fi
}

main() {
  local total_patches=${#PATCH_SET[@]}

  for ((i=0; i<total_patches; i+=2)); do
    local root_path="${PATCH_SET[i]}"
    local patch_file="${PATCH_SET[i+1]}"

    apply_patch "$root_path" "$patch_file"
  done
}

main
