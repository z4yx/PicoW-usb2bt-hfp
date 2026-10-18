#!/usr/bin/env bash
# Convenience environment for this checkout.
# ESP-IDF and the toolchains are installed under the repository root in the
# (git-ignored) .esp-idf / .espressif directories.
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

export IDF_PATH="$ROOT/.esp-idf"
export IDF_TOOLS_PATH="$ROOT/.espressif"
export IDF_COMPONENT_MANAGER_CACHE_DIR="$IDF_TOOLS_PATH/component_manager_cache"
export XDG_CACHE_HOME="$ROOT/.cache"

if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "ESP-IDF not found at $IDF_PATH (see esp32s31/README.md)" >&2
    exit 1
fi

. "$IDF_PATH/export.sh"
