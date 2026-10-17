---
name: debug-openocd-rp2350
description: 当需要用 OpenOCD（J-Link 探针，SWD）+ gdb-multiarch 给 RP2350 板卡（pico2_w / Pico 2 W）做在线调试、断点、看寄存器或崩溃现场分析时使用；自动起停 GDB Server。
whenToUse: 用户说"调试/attach/断点/看崩溃/看寄存器/单步"，且工程是 pico2_w（RP2350）并用 OpenOCD 烧写时优先用本 skill，而不是 debug-jlink。
---

# RP2350 OpenOCD 在线调试（gdb-multiarch）

一条命令完成：后台起 `openocd` 作 GDB Server → 前台用 `gdb-multiarch` 连上 → 退出时自动清理。
GDB Server 的烧写通道与 `flash-openocd-rp2350` 同源（`interface/jlink.cfg` + `target/rp2350.cfg`），
两者参数命名保持一致，便于在流水线里替换。

## 适用场景

- 需要在 RP2350 上打断点、单步、看寄存器/内存/调用栈。
- 固件跑飞后要看崩溃现场（配合 `build-arm/*.map` 可查符号）。
- `flash-jlink` 用不了（本机 J-Link 软件包 V6.88c 没有 RP2350 器件条目），
  所以 GDB 不该走 JLinkGDBServer，而走 OpenOCD 的 GDB Server。

## 环境（本机已验证）

| 项 | 值 |
|---|---|
| OpenOCD | `/opt/openocd/bin/openocd`（0.12.0+dev），提供 gdb server |
| GDB | `/usr/bin/gdb-multiarch` |
| 端口 | GDB `3333`，telnet `4444`（可用 `--gdb-port` / `--telnet-port` 改） |
| 目标 | `target/rp2350.cfg`；探针 `interface/jlink.cfg`（SAM-ICE，VID 1366） |

## 用法

```bash
# 环境自检
.dsh/skills/debug-openocd-rp2350/scripts/gdb_openocd.sh --detect

# 只连不写：halt 到复位向量后进 (gdb) 提示符
.dsh/skills/debug-openocd-rp2350/scripts/gdb_openocd.sh

# 先烧再调（会改写 flash）
.dsh/skills/debug-openocd-rp2350/scripts/gdb_openocd.sh --load

# 非交互：跑几条命令就退出（回归/CI 友好）
.dsh/skills/debug-openocd-rp2350/scripts/gdb_openocd.sh --batch \
  --ex "info registers" --ex "bt" --ex "info threads"

# 看计划命令，不动硬件
.dsh/skills/debug-openocd-rp2350/scripts/gdb_openocd.sh --dry-run
```

## 脚本行为

1. 校验 openocd / gdb-multiarch / 两个 cfg / ELF，缺一个就报 `environment-missing` 并退出。
2. 在后台启动 `openocd -f interface/jlink.cfg -c "transport select swd" -c "adapter speed 1000" -f target/rp2350.cfg -c "gdb_port 3333" -c "telnet_port 4444"`，日志写临时文件
   （openocd 0.12 会打印 `DEPRECATED! use 'gdb port'`，属正常：`gdb_port` 在 0.11/0.12 都可用，改成新写法会破坏旧版兼容）。
3. 轮询 `127.0.0.1:3333` 最多 20 秒；openocd 提前退出或超时就打印日志尾部并以退出码 3 失败（环境类问题不会伪装成调试问题）。
4. 前台执行 `gdb-multiarch <elf> -ex "target extended-remote 127.0.0.1:3333" -ex "monitor reset halt" [--ex ...]`。
5. `trap` 保证 GDB 退出后杀掉 openocd，端口不会被占住。

## 流水线衔接

`workflow` 的 cmake 调试步骤已映射到本 skill：

```bash
python .claude/skills/workflow/scripts/workflow_runner.py \
  --run build-flash-debug --build-system cmake \
  --project . --build-dir build-arm --build-type Release \
  --flash-interface jlink --flash-target rp2350
```

流水线会传 `--elf <产物>`、`--interface`、`--target`；调试步骤用继承终端的方式运行，
所以是"进 GDB 提示符"的交互形态，退出 GDB 后流水线才继续收尾。

## 失败分流

| 现象 | 归类 | 处置 |
|---|---|---|
| 找不到 openocd / gdb-multiarch | environment-missing | 装 gdb-multiarch，或 `GDB=/path/gdb` |
| `openocd 未在 20 秒内监听 3333` + 日志里 `No J-Link device found` | connection-failure | 沙箱外重跑；查 USB 直通、`lsusb` 见 `1366:0105`、探针是否被别的进程占用 |
| 日志里 `Error: init mode failed` | connection-failure | 查 SWD 接线（SWCLK/SWDIO/GND）、板上电、把 `--speed` 降到 1000 以下 |
| 连上但符号错乱 / 源码行号不对 | project-config-error | `--elf` 指到与板上固件同一份构建产物 |
| `monitor reset halt` 报错 | target-response-abnormal | 加 `--no-reset-halt` 后手动 `monitor reset halt` / `monitor halt` |

## 注意

- **沙箱内必失败**：DSH 的 bash 沙箱没有 `/dev/bus/usb`，openocd 会 `LIBUSB_ERROR_NO_DEVICE`，
  这是环境限制，不是接线问题；真实调试要在沙箱外执行。
- 调试与烧写**不要同时占探针**：先结束 `flash_openocd.sh --attach` 或其它 JLinkExe 会话。
- RP2350 是双核（cm0/cm1），`info threads` 能看到两个硬件线程；跨核断点用 `thread apply all bt`。

## 交接关系

- 固件来自 `build-cmake`（输出目录 `build-arm/`），烧写用 `flash-openocd-rp2350`。
- 调试结束想看运行日志 → `serial-monitor`。
- 内存/符号问题 → `memory-analysis`（吃 `build-arm/*.map`）。
