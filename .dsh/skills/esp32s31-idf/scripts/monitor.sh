#!/usr/bin/env bash
# ESP32-S31 串口日志（不 reset，避免 monitor 把芯片拉回 download）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
PORT="${ESP32S31_PORT:-/dev/ttyACM0}"

# shellcheck source=/dev/null
source "$WS_ROOT/esp32s31/env.sh" >/dev/null 2>&1 || {
  echo "错误：无法 source esp32s31/env.sh（检查 .esp-idf / .espressif 是否存在）" >&2
  exit 2
}

cd "$WS_ROOT/esp32s31"
exec idf.py --preview -p "$PORT" monitor --no-reset
