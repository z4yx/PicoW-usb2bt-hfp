#!/usr/bin/env bash
#
# Apply the ESP-IDF patches needed by this project.
#
# The IDF checkout is expected at $IDF_PATH (or <repo>/.esp-idf).
# By default only the functional SCO packet-length patch is applied;
# pass --debug to also enable the per-packet SCO TX log.
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
IDF_DIR="${IDF_PATH:-$ROOT/.esp-idf}"
TARGET="$IDF_DIR/components/bt/host/bluedroid/bta/hf_ag/bta_ag_sco.c"

PATCH_SCO="$HERE/0001-bta-ag-sco-default-cvsd-pkt-len.patch"
PATCH_LOG="$HERE/0002-bta-ag-sco-log-sco-tx-status.patch"

DEBUG=0
REVERT=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [--debug] [--revert]

  --debug    also apply 0002 (per-packet SCO TX status log)
  --revert   revert the applied patch(es), debug patch first

IDF checkout: $IDF_DIR
  override with IDF_PATH=/path/to/esp-idf
EOF
}

for arg in "$@"; do
    case "$arg" in
        --debug)  DEBUG=1 ;;
        --revert) REVERT=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $arg" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! -f "$TARGET" ]]; then
    echo "error: $TARGET not found" >&2
    echo "set IDF_PATH to an ESP-IDF checkout that supports esp32s31" >&2
    exit 1
fi

if ! git -C "$IDF_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "error: $IDF_DIR is not a git checkout" >&2
    echo "apply manually: patch -p1 -d \"$IDF_DIR\" < $PATCH_SCO" >&2
    exit 1
fi

patches=("$PATCH_SCO")
if [[ $DEBUG -eq 1 ]]; then
    patches+=("$PATCH_LOG")
fi

apply_one() {
    local patch="$1"

    if git -C "$IDF_DIR" apply --reverse --check "$patch" >/dev/null 2>&1; then
        echo "already applied: $(basename "$patch")"
        return 0
    fi

    git -C "$IDF_DIR" apply --check "$patch"
    git -C "$IDF_DIR" apply "$patch"
    echo "applied:         $(basename "$patch")"
}

revert_one() {
    local patch="$1"

    if git -C "$IDF_DIR" apply --reverse --check "$patch" >/dev/null 2>&1; then
        git -C "$IDF_DIR" apply --reverse "$patch"
        echo "reverted:        $(basename "$patch")"
    else
        echo "not applied:     $(basename "$patch")"
    fi
}

if [[ $REVERT -eq 1 ]]; then
    for ((i = ${#patches[@]} - 1; i >= 0; i--)); do
        revert_one "${patches[$i]}"
    done
else
    for patch in "${patches[@]}"; do
        apply_one "$patch"
    done
fi

echo
echo "IDF tree: $IDF_DIR"
git -C "$IDF_DIR" status --short -- components/bt/host/bluedroid/bta/hf_ag/bta_ag_sco.c || true
echo "Rebuild with: cd \"$ROOT/esp32s31\" && source ./env.sh && idf.py --preview build"
