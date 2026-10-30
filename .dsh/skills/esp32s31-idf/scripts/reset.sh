#!/usr/bin/env bash
# ESP32-S31 复位：hard（默认，正常启动） / wdt（download 模式兜底）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
PORT="${ESP32S31_PORT:-/dev/ttyACM0}"
MODE="${1:-hard}"

case "$MODE" in
  hard) AFTER="hard-reset" ;;
  wdt)  AFTER="watchdog-reset" ;;
  *)
    echo "用法：$0 [hard|wdt]" >&2
    exit 2
    ;;
esac

# shellcheck source=/dev/null
source "$WS_ROOT/esp32s31/env.sh" >/dev/null 2>&1 || {
  echo "错误：无法 source esp32s31/env.sh（检查 .esp-idf / .espressif 是否存在）" >&2
  exit 2
}

exec python -m esptool --chip esp32s31 -p "$PORT" \
  --before default-reset --after "$AFTER" chip-id
