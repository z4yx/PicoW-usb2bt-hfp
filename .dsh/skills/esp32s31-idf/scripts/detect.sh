#!/usr/bin/env bash
# ESP32-S31 环境 / 端口 / 产物检查
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
PORT="${ESP32S31_PORT:-/dev/ttyACM0}"

# shellcheck source=/dev/null
source "$WS_ROOT/esp32s31/env.sh" >/dev/null 2>&1 || {
  echo "错误：无法 source esp32s31/env.sh（检查 .esp-idf / .espressif 是否存在）" >&2
  exit 2
}

echo "== 环境 =="
command -v idf.py
idf.py --version 2>/dev/null || true

echo "== 目标 / 配置 =="
grep -E "CONFIG_IDF_TARGET=|CONFIG_ESP_CONSOLE_(USB_SERIAL_JTAG|UART_DEFAULT)=" \
  "$WS_ROOT/esp32s31/sdkconfig" 2>/dev/null || true
grep -E "CONFIG_BTDM_CTRL_MODE_(BR_EDR_ONLY|BLE_ONLY|BTDM)=" \
  "$WS_ROOT/esp32s31/sdkconfig" 2>/dev/null || true

echo "== USB =="
if command -v lsusb >/dev/null 2>&1; then
  lsusb -d 303a:1001 2>/dev/null || echo "  [WARN] 未见 303a:1001（USB-Serial/JTAG）"
fi
ls -l "$PORT" 2>/dev/null || echo "  [WARN] $PORT 不存在（可用 ESP32S31_PORT 覆盖）"

echo "== 产物 =="
ls -l "$WS_ROOT/esp32s31/build/picow_usb2bt_hfp_s31.bin" 2>/dev/null \
  || echo "  [WARN] 尚未构建（先跑 build.sh）"
