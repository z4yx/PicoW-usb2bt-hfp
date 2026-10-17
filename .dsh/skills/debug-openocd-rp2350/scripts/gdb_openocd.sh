#!/usr/bin/env bash
# OpenOCD + GDB 调试启动器（RP2350 / pico2_w）
#
# 后台起 openocd 作 GDB Server，前台连上 gdb-multiarch，退出时自动清理 openocd。
#
# 用法：gdb_openocd.sh [选项]
#   --elf PATH        参与调试的 ELF（相对工作区根目录或绝对路径，默认 build-arm/PicoW_USB_BT_Audio.elf）
#   --target NAME     openocd target 名（默认 rp2350）
#   --interface NAME  openocd interface 名（默认 jlink）
#   --speed KHZ       adapter speed，默认 1000
#   --gdb-port PORT   GDB 端口，默认 3333
#   --telnet-port P   telnet 端口，默认 4444
#   --load            连上后先 load 固件进目标，再用 compare-sections 校验
#   --no-reset-halt   不执行 monitor reset halt（默认会 halt 到复位向量）
#   --batch           非交互：执行完 -ex 命令后退出（适合流水线/回归）
#   --ex CMD          追加一条 gdb 命令，可重复
#   --dry-run         只打印将执行的命令
#   --detect          只做环境检查
#   -h, --help        显示本帮助
#
# 示例：
#   gdb_openocd.sh                        # 连上并停在复位向量，进入 (gdb)
#   gdb_openocd.sh --load                 # 先把 ELF 烧进去再调试
#   gdb_openocd.sh --batch --ex "info registers" --ex "bt"
#   gdb_openocd.sh --elf build-arm/x.elf --gdb-port 3334
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

OPENOCD_BIN="${OPENOCD:-openocd}"
GDB_BIN="${GDB:-gdb-multiarch}"
ARTIFACT="build-arm/PicoW_USB_BT_Audio.elf"
TARGET="rp2350"
IFACE="jlink"
SPEED="1000"
GDB_PORT="3333"
TELNET_PORT="4444"
LOAD=0
RESET_HALT=1
BATCH=0
DRY_RUN=0
DETECT_ONLY=0
EX_CMDS=()
OPENOCD_PID=""
OPENOCD_LOG=""

die() { echo "错误：$*" >&2; exit 2; }

cleanup() {
  if [ -n "$OPENOCD_PID" ] && kill -0 "$OPENOCD_PID" 2>/dev/null; then
    kill "$OPENOCD_PID" 2>/dev/null || true
    wait "$OPENOCD_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

while [ $# -gt 0 ]; do
  case "$1" in
    --elf|--artifact) [ $# -ge 2 ] || die "$1 需要参数"; ARTIFACT="$2"; shift 2 ;;
    --target)         [ $# -ge 2 ] || die "--target 需要参数"; TARGET="$2"; shift 2 ;;
    --interface)      [ $# -ge 2 ] || die "--interface 需要参数"; IFACE="$2"; shift 2 ;;
    --speed)          [ $# -ge 2 ] || die "--speed 需要参数"; SPEED="$2"; shift 2 ;;
    --gdb-port)       [ $# -ge 2 ] || die "--gdb-port 需要参数"; GDB_PORT="$2"; shift 2 ;;
    --telnet-port)    [ $# -ge 2 ] || die "--telnet-port 需要参数"; TELNET_PORT="$2"; shift 2 ;;
    --load)           LOAD=1; shift ;;
    --no-reset-halt)  RESET_HALT=0; shift ;;
    --batch)          BATCH=1; shift ;;
    --ex)             [ $# -ge 2 ] || die "--ex 需要参数"; EX_CMDS+=("$2"); shift 2 ;;
    --dry-run)        DRY_RUN=1; shift ;;
    --detect)         DETECT_ONLY=1; shift ;;
    -h|--help)        awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "${BASH_SOURCE[0]}"; exit 0 ;;
    *)                die "未知参数：$1（用 -h 看用法）" ;;
  esac
done

case "$ARTIFACT" in
  /*) ARTIFACT_ABS="$ARTIFACT" ;;
  *)  ARTIFACT_ABS="$WS_ROOT/$ARTIFACT" ;;
esac

IFACE_CFG="interface/${IFACE}.cfg"
TARGET_CFG="target/${TARGET}.cfg"
OPENOCD_ABS="$(command -v "$OPENOCD_BIN" 2>/dev/null || true)"
GDB_ABS="$(command -v "$GDB_BIN" 2>/dev/null || true)"
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
if [ -n "$GDB_ABS" ]; then
  echo "  [ok]   gdb: $GDB_ABS ($("$GDB_ABS" --version 2>&1 | head -1))"
else
  echo "  [FAIL] 找不到 $GDB_BIN（可用 GDB=/path/to/gdb 指定，如 gdb-multiarch）"; ok=0
fi
if [ -z "$SCRIPTS_DIR" ]; then
  echo "  [WARN] 未能定位 openocd 脚本目录，跳过 cfg 检查（可设 OPENOCD_SCRIPTS 指定）"
elif [ -f "$SCRIPTS_DIR/$IFACE_CFG" ]; then
  echo "  [ok]   $IFACE_CFG  <- $SCRIPTS_DIR"
else
  echo "  [FAIL] 缺少 $IFACE_CFG（检查 openocd 安装）"; ok=0
fi
if [ -z "$SCRIPTS_DIR" ]; then
  :
elif [ -f "$SCRIPTS_DIR/$TARGET_CFG" ]; then
  echo "  [ok]   $TARGET_CFG  <- $SCRIPTS_DIR"
else
  echo "  [FAIL] 缺少 $TARGET_CFG（RP2350 需要较新的 openocd 脚本）"; ok=0
fi
if [ -f "$ARTIFACT_ABS" ]; then
  echo "  [ok]   ELF: $ARTIFACT_ABS"
else
  echo "  [WARN] ELF 不存在：$ARTIFACT_ABS（仅 --load 与符号/源码调试需要它）"
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

OC_ARGS=(-f "$IFACE_CFG" -c "transport select swd" -c "adapter speed $SPEED" -f "$TARGET_CFG"
         -c "gdb_port $GDB_PORT" -c "telnet_port $TELNET_PORT")
GDB_ARGS=()
[ "$BATCH" -eq 1 ] && GDB_ARGS+=(-batch)
GDB_ARGS+=("$ARTIFACT_ABS" -ex "target extended-remote 127.0.0.1:$GDB_PORT")
[ "$RESET_HALT" -eq 1 ] && GDB_ARGS+=(-ex "monitor reset halt")
if [ "$LOAD" -eq 1 ]; then
  GDB_ARGS+=(-ex "load" -ex "compare-sections" -ex "monitor reset halt")
fi
for c in ${EX_CMDS[@]+"${EX_CMDS[@]}"}; do GDB_ARGS+=(-ex "$c"); done
[ "$BATCH" -eq 1 ] && GDB_ARGS+=(-ex "quit")

render() { local out="" a; for a in "$@"; do case "$a" in *" "*) out+="'$a' " ;; *) out+="$a " ;; esac; done; printf '%s' "${out% }"; }
echo "== 计划 =="
echo "  工作区: $WS_ROOT"
echo "  服务端: $(render "$OPENOCD_BIN" "${OC_ARGS[@]}")"
echo "  客户端: $(render "$GDB_BIN" "${GDB_ARGS[@]}")"
[ "$LOAD" -eq 1 ] && echo "  模式:   attach + load（会改写目标 flash）" || echo "  模式:   attach（只连不写）"
echo

if [ "$DRY_RUN" -eq 1 ]; then
  echo "(dry-run，未执行)"
  exit 0
fi

OPENOCD_LOG="$(mktemp -t openocd-gdb-XXXXXX.log)"
echo "== 启动 openocd（日志: $OPENOCD_LOG）=="
cd "$WS_ROOT"
"$OPENOCD_BIN" "${OC_ARGS[@]}" >"$OPENOCD_LOG" 2>&1 &
OPENOCD_PID=$!

wait_port() {
  local port="$1" deadline=$((SECONDS + 20))
  while [ "$SECONDS" -lt "$deadline" ]; do
    if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
      exec 3>&- 3<&-
      return 0
    fi
    if ! kill -0 "$OPENOCD_PID" 2>/dev/null; then return 1; fi
    sleep 0.2
  done
  return 1
}

if ! wait_port "$GDB_PORT"; then
  echo "  [FAIL] openocd 未在 20 秒内监听 $GDB_PORT，日志尾部：" >&2
  tail -15 "$OPENOCD_LOG" | sed 's/^/    /' >&2
  echo "  （沙箱内看不到 /dev/bus/usb 时必然如此，请在沙箱外重试）" >&2
  exit 3
fi

# 端口就绪但进程随即退出 = 探针/目标初始化失败，别把环境问题伪装成调试问题
sleep 0.3
if ! kill -0 "$OPENOCD_PID" 2>/dev/null; then
  echo "  [FAIL] openocd 在端口就绪后退出（探针/目标初始化失败），日志尾部：" >&2
  tail -15 "$OPENOCD_LOG" | sed 's/^/    /' >&2
  exit 3
fi
echo "  [ok]   openocd 已监听 127.0.0.1:$GDB_PORT（telnet :$TELNET_PORT，PID $OPENOCD_PID）"
echo "  [ok]   探针/目标已连接"
echo
echo "== 进入 GDB =="
echo
"$GDB_BIN" "${GDB_ARGS[@]}"
rc=$?
echo
echo "== GDB 退出（rc=$rc），openocd 已清理 =="
exit "$rc"
