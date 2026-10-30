#!/usr/bin/env bash
# ESP32-S31 烧写（USB-Serial/JTAG）
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
if [ ! -f build/picow_usb2bt_hfp_s31.bin ]; then
  echo "错误：尚未构建，先运行 build.sh" >&2
  exit 2
fi
exec idf.py --preview -p "$PORT" flash
