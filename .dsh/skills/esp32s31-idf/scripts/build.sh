#!/usr/bin/env bash
# ESP32-S31 编译（首次自动 set-target）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

# shellcheck source=/dev/null
source "$WS_ROOT/esp32s31/env.sh" >/dev/null 2>&1 || {
  echo "错误：无法 source esp32s31/env.sh（检查 .esp-idf / .espressif 是否存在）" >&2
  exit 2
}

cd "$WS_ROOT/esp32s31"
if [ ! -f sdkconfig ]; then
  echo "== 首次配置 target: esp32s31 =="
  idf.py --preview set-target esp32s31
fi
exec idf.py --preview build
