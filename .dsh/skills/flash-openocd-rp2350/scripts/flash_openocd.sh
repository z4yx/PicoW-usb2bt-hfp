#!/usr/bin/env bash
# OpenOCD + J-Link 烧写助手（RP2350 / pico2_w）
#
# 用法：flash_openocd.sh [选项]
#   --artifact PATH   固件产物，相对工作区根目录或绝对路径（默认 build-arm/PicoW_USB_BT_Audio.elf）
#   --target NAME     openocd target 名（默认 rp2350，RP2040 板用 rp2040）
#   --interface NAME  openocd interface 名（默认 jlink）
#   --speed KHZ       adapter speed，默认 1000
#   --no-verify       跳过 verify
#   --no-reset        跳过 reset
#   --verify-only     只校验，不下载
#   --attach          只起 openocd（GDB Server，阻塞），不烧写
#   --dry-run         只打印将执行的命令
#   --detect          只做环境检查
#   -h, --help        显示本帮助
#
# 示例：
#   flash_openocd.sh
#   flash_openocd.sh --artifact build-arm/PicoW_USB_BT_Audio.elf --speed 4000
#   flash_openocd.sh --verify-only
#   flash_openocd.sh --attach
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

OPENOCD_BIN="${OPENOCD:-openocd}"
ARTIFACT="build-arm/PicoW_USB_BT_Audio.elf"
TARGET="rp2350"
IFACE="jlink"
SPEED="1000"
VERIFY=1
RESET=1
MODE="flash"        # flash | verify-only | attach
DRY_RUN=0
DETECT_ONLY=0

die() { echo "错误：$*" >&2; exit 2; }

while [ $# -gt 0 ]; do
  case "$1" in
    --artifact)    [ $# -ge 2 ] || die "--artifact 需要参数"; ARTIFACT="$2"; shift 2 ;;
    --target)      [ $# -ge 2 ] || die "--target 需要参数"; TARGET="$2"; shift 2 ;;
    --interface)   [ $# -ge 2 ] || die "--interface 需要参数"; IFACE="$2"; shift 2 ;;
    --speed)       [ $# -ge 2 ] || die "--speed 需要参数"; SPEED="$2"; shift 2 ;;
    --verify-only) MODE="verify-only"; shift ;;
    --attach)      MODE="attach"; shift ;;
    --no-verify)   VERIFY=0; shift ;;
    --no-reset)    RESET=0; shift ;;
    --dry-run)     DRY_RUN=1; shift ;;
    --detect)      DETECT_ONLY=1; shift ;;
    -h|--help)     awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "${BASH_SOURCE[0]}"; exit 0 ;;
    *)             die "未知参数：$1（用 -h 看用法）" ;;
  esac
done

case "$ARTIFACT" in
  /*) ARTIFACT_ABS="$ARTIFACT" ;;
  *)  ARTIFACT_ABS="$WS_ROOT/$ARTIFACT" ;;
esac

IFACE_CFG="interface/${IFACE}.cfg"
TARGET_CFG="target/${TARGET}.cfg"
OPENOCD_ABS="$(command -v "$OPENOCD_BIN" 2>/dev/null || true)"
SCRIPTS_DIR="${OPENOCD_SCRIPTS:-}"
if [ -z "$SCRIPTS_DIR" ] && [ -n "$OPENOCD_ABS" ]; then
  SCRIPTS_DIR="$(cd "$(dirname "$(readlink -f "$OPENOCD_ABS")")/../share/openocd/scripts" 2>/dev/null && pwd || true)"
fi

echo "== 环境检查 =="
ok=1
if [ -n "$OPENOCD_ABS" ]; then
  echo "  [ok]   openocd: $OPENOCD_ABS ($("$OPENOCD_ABS" --version 2>&1 | head -1))"
else
  echo "  [FAIL] 找不到 openocd（可用 OPENOCD=/path/to/openocd 指定）"; ok=0
fi
if [ -z "$SCRIPTS_DIR" ]; then
  echo "  [WARN] 未能定位 openocd 脚本目录，跳过 cfg 检查（可设 OPENOCD_SCRIPTS 指定）"
elif [ -f "$SCRIPTS_DIR/$IFACE_CFG" ]; then
  echo "  [ok]   $IFACE_CFG  <- $SCRIPTS_DIR"
else
  echo "  [FAIL] 缺少 $IFACE_CFG（检查 openocd 安装，或设 OPENOCD_SCRIPTS）"; ok=0
fi
if [ -z "$SCRIPTS_DIR" ]; then
  :
elif [ -f "$SCRIPTS_DIR/$TARGET_CFG" ]; then
  echo "  [ok]   $TARGET_CFG  <- $SCRIPTS_DIR"
else
  echo "  [FAIL] 缺少 $TARGET_CFG（RP2350 需要较新的 openocd 脚本）"; ok=0
fi
if [ "$MODE" != "attach" ]; then
  if [ -f "$ARTIFACT_ABS" ]; then
    echo "  [ok]   产物: $ARTIFACT_ABS ($(stat -c%s "$ARTIFACT_ABS") 字节)"
  else
    echo "  [FAIL] 产物不存在：$ARTIFACT_ABS（先构建，或用 --artifact 指定）"; ok=0
  fi
fi
if [ ! -e /dev/bus/usb ]; then
  echo "  [WARN] 当前环境看不到 /dev/bus/usb，USB 打不开（沙箱内必然失败，需在沙箱外执行）"
fi
if command -v lsusb >/dev/null 2>&1; then
  probe="$(lsusb 2>/dev/null | grep -i '1366:' || true)"
  if [ -n "$probe" ]; then echo "  [ok]   探针: $probe"; else echo "  [WARN] lsusb 未发现 J-Link (VID 1366)"; fi
fi

[ "$ok" -eq 1 ] || die "环境检查未通过"
if [ "$DETECT_ONLY" -eq 1 ]; then echo "== 检查通过 =="; exit 0; fi
echo

ARGS=(-f "$IFACE_CFG" -c "transport select swd" -c "adapter speed $SPEED" -f "$TARGET_CFG")
case "$MODE" in
  flash)
    acts="program $ARTIFACT"
    [ "$VERIFY" -eq 1 ] && acts="$acts verify"
    [ "$RESET" -eq 1 ]  && acts="$acts reset"
    ARGS+=(-c "$acts exit")
    ;;
  verify-only)
    ARGS+=(-c "init" -c "reset halt" -c "verify_image $ARTIFACT" -c "shutdown")
    ;;
  attach)
    : # 不追加命令：openocd 起服务后阻塞（GDB :3333 / telnet :4444）
    ;;
esac

echo "== 执行 =="
echo "  工作区: $WS_ROOT"
echo "  模式:   $MODE"
[ "$MODE" != "attach" ] && echo "  产物:   $ARTIFACT"
render() { local out="" a; for a in "$@"; do case "$a" in *" "*) out+="'$a' " ;; *) out+="$a " ;; esac; done; printf '%s' "${out% }"; }
printf '  命令:  %s\n' "$(render "$OPENOCD_BIN" "${ARGS[@]}")"

if [ "$DRY_RUN" -eq 1 ]; then
  echo "  (dry-run，未执行)"
  exit 0
fi

cd "$WS_ROOT"
exec "$OPENOCD_BIN" "${ARGS[@]}"
