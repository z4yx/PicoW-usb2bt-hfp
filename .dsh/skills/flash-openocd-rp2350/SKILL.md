---
name: flash-openocd-rp2350
description: 当需要用 OpenOCD + J-Link 探针（SWD）给 RP2350 板卡（pico2_w / Pico 2 W）烧写或校验 build-arm 下的 ELF 固件时使用；内置本工程已实测通过的 openocd 命令行。
whenToUse: 用户说"烧录/烧写/下载固件/flash/verify 固件"，且工程是 pico2_w（RP2350）并按 build-arm 目录构建时优先用本 skill，而不是 flash-jlink。
---

# RP2350 OpenOCD 烧写（J-Link 探针）

本 skill 记录的是**在本工程实测可用**的烧写流程，命令与参数来自实际验证，不是推测。

## 适用场景

- 目标板：RP2350（本工程 `PICO_BOARD=pico2_w`，`PICO_PLATFORM=rp2350-arm-s`）。
- 探针：SEGGER J-Link，通过 SWD 连接（OpenOCD 的 `interface/jlink.cfg` 驱动）。
- 固件产物：`build-arm/` 下由 CMake 构建的 ELF（如 `build-arm/PicoW_USB_BT_Audio.elf`）。
- 需要烧写、校验、复位，或起一个 GDB Server 做在线调试。

## 环境（本机已验证）

| 项 | 值 |
|---|---|
| OpenOCD | `/opt/openocd/bin/openocd`，`0.12.0+dev-gacff23f` |
| 脚本目录 | `/opt/openocd/share/openocd/scripts`（`-f target/xxx.cfg` 由此解析，无需设 `OPENOCD_SCRIPTS`） |
| 配置文件 | `interface/jlink.cfg`、`target/rp2350.cfg`（另有 `rp2350-riscv.cfg`） |
| 探针 | USB VID:PID `1366:0105`（枚举为 SAM-ICE，序列号 20171111） |

## 已验证命令（复制即用）

```bash
cd <workspace-root>   # 即本工程根目录
openocd -f interface/jlink.cfg -c "transport select swd" -c "adapter speed 1000" \
        -f target/rp2350.cfg \
        -c "program build-arm/PicoW_USB_BT_Audio.elf verify reset exit"
```

参数含义与保留理由：

| 片段 | 作用 |
|---|---|
| `-f interface/jlink.cfg` | 用 J-Link 探针驱动（不是 cmsis-dap / ftdi） |
| `-c "transport select swd"` | 显式选 SWD；`jlink.cfg` 默认已是 swd，此条冗余但无害（会打印 `Warn: Transport "swd" was already selected`），保留是为了与实测命令逐字一致 |
| `-c "adapter speed 1000"` | 1000 kHz，实测稳定；如需更快可试 4000，失败则回退 |
| `-f target/rp2350.cfg` | RP2350 双核目标；RP2040 板要用 `target/rp2040.cfg` |
| `-c "program <elf> verify reset exit"` | 烧写 + 校验 + 复位 + 退出；ELF 自带地址信息，无需 `base_address` |

## 用脚本执行（推荐，省去手写路径与引号）

```bash
# 探测：openocd/配置文件/产物是否存在
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh --detect

# 看命令不执行
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh --dry-run

# 烧写（默认产物 build-arm/PicoW_USB_BT_Audio.elf、rp2350、1000 kHz、verify+reset）
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh

# 指定产物 / 提速 / 不校验
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh --artifact build-arm/foo.elf --speed 4000 --no-verify

# 只校验不写（先确认板上固件与 elf 一致）
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh --verify-only

# 起 GDB Server（阻塞，另开终端用 gdb-multiarch -ex "target extended-remote :3333"）
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh --attach
```

## 执行步骤

1. 先确认产物存在：`ls -l build-arm/*.elf`；没有就先走 `build-cmake` skill 构建。
2. 运行 `--detect`，确认 openocd、`interface/jlink.cfg`、`target/rp2350.cfg`、elf 四项齐全。
3. 执行烧写，观察输出中的 `** Programming Finished **`、`** Verify Started **` / `** Verified OK **`、`** Resetting Target **`。
4. 退出码 0 且出现 `Verified OK` 才算成功；随后可交 `serial-monitor` 抓日志。

## 前置条件（踩过的坑）

- **USB 直通**：`/dev/bus/usb` 必须可见。DSH 的 bash 沙箱内 `/dev` 只有最小节点集合，openocd 会报
  `Error: libusb_open() failed with LIBUSB_ERROR_NOT_FOUND` / `No J-Link device found`；
  这类失败要先在沙箱外（或 danger-full-access）重试，不要误判为探针或板子坏了。
- **板子供电与接线**：SWD 需接 `SWCLK`/`SWDIO`/`GND`（Pico 2 W 底部三针调试口）；只接 USB 供电而没接 SWD 会
  `Error: init mode failed (unable to connect to the target)`。
- **别同时占用探针**：另一个 JLinkExe / JLinkGDBServer / 上一次 openocd 未退出时，会抢不到 USB 句柄。
- **产物与平台匹配**：`rp2350.cfg` 配 RP2040 板，或反过来，都会在 halt 阶段失败。

## 失败分流

| 现象 | 归类 | 处置 |
|---|---|---|
| `openocd: command not found` / 无 `/opt/openocd/bin/openocd` | environment-missing | 确认 `/opt/openocd/bin` 在 PATH，或直接用绝对路径 |
| `No J-Link device found` / `LIBUSB_ERROR_NOT_FOUND` | connection-failure | 检查 USB 直通、`lsusb` 是否见 `1366:0105`、是否被别的进程占用 |
| `Error: init mode failed (unable to connect to the target)` | connection-failure | 查 SWD 接线、板上电、`adapter speed` 降到 1000 以下 |
| `Can't find target/rp2350.cfg` | project-config-error | 该 openocd 安装缺脚本；确认 `/opt/openocd/share/openocd/scripts/target/` |
| `** Verify Failed **` | target-response-abnormal | 重新烧写；仍失败则怀疑 flash 供电/QSPI 异常 |
| `Error: 无法打开 build-arm/xxx.elf` | artifact-missing | 产物路径错或未构建，先跑 `build-cmake` |

## 输出约定

报告里固定给出四项：**产物路径**、**target cfg**、**adapter speed**、**verify 结果**。

## 交接关系

- 构建产物来自 `build-cmake`（本工程输出目录 `build-arm/`）。
- 烧写后要跑日志 → `serial-monitor`（RP2350 的 USB CDC 或 UART 视固件配置）。
- 需要断点/单步/看崩溃 → `debug-openocd-rp2350`（同一 OpenOCD 通道起 GDB Server）。
  注意本机 J-Link 软件包是 V6.88c（2020 年），没有 RP2350 器件条目，所以 `flash-jlink` /
  `debug-jlink` 都走不通，OpenOCD 是本工程唯一已验证的烧写与调试通道。
