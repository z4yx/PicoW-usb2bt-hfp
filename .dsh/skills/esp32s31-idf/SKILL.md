---
name: esp32s31-idf
description: 当需要为 ESP32-S31-WROOM-3-N16R16V（本工程 esp32s31/）配置 ESP-IDF、编译、烧写、复位或抓串口日志时使用；记录本工程实测的 ESP-IDF v6.2 preview target、USB-Serial/JTAG 端口、hard-reset 与 WDT 兜底流程。
whenToUse: 用户提到 ESP32-S31 / esp32s31 / idf.py / 给 S31 烧写固件 / 复位 S31 / 看 HFP AG 启动日志时优先用本 skill。
---

# ESP32-S31 ESP-IDF 编译 / 烧写 / 复位

本 skill 记录本工程在 **ESP32-S31-WROOM-3-N16R16V** 上实测通过的流程；命令和参数来自实际板卡验证，不是推测。

## 工程与环境

| 项 | 值 |
|---|---|
| 工程目录 | `esp32s31/`（ESP-IDF project） |
| Target | `esp32s31`（**preview**，所有 `idf.py` 命令都要带 `--preview`） |
| ESP-IDF | 本机装在仓库根目录 `.esp-idf/`；工具链 / Python venv / 组件缓存在 `.espressif/`（均被 gitignore） |
| 环境脚本 | `esp32s31/env.sh`：设置 `IDF_PATH`、`IDF_TOOLS_PATH`、组件缓存、`XDG_CACHE_HOME`，再 source `export.sh` |
| 编译产物 | `esp32s31/build/picow_usb2bt_hfp_s31.bin` |
| 调试串口 | `/dev/ttyACM0`（USB-Serial/JTAG，VID:PID `303a:1001`） |
| 应用 console | 已配置为 USB-Serial/JTAG（`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`） |
| Flash / PSRAM | 16 MB flash / 16 MB PSRAM；4 MB app 分区 |
| 功能 USB | USB OTG 与调试 USB 是两回事：UAC 需要功能 USB 接主机；烧写/日志用 USB-Serial/JTAG |

> `.esp-idf/`、`.espressif/` 是本机安装（被 `.gitignore` 忽略）。换机器时需要先按 ESP-IDF 文档安装支持 `esp32s31` 的 master/preview 版本。

## 一键脚本（推荐）

```bash
.dsh/skills/esp32s31-idf/scripts/detect.sh       # 环境 / 端口 / 产物检查
.dsh/skills/esp32s31-idf/scripts/build.sh        # 首次自动 set-target，然后 build
.dsh/skills/esp32s31-idf/scripts/flash.sh        # idf.py flash
.dsh/skills/esp32s31-idf/scripts/reset.sh hard   # 普通 hard reset（推荐）
.dsh/skills/esp32s31-idf/scripts/reset.sh wdt    # download 模式兜底
.dsh/skills/esp32s31-idf/scripts/monitor.sh      # monitor --no-reset
```

端口默认 `/dev/ttyACM0`，可用环境变量覆盖：

```bash
ESP32S31_PORT=/dev/ttyACM1 .dsh/skills/esp32s31-idf/scripts/flash.sh
```

## 裸命令（复制即用）

```bash
cd esp32s31
source ./env.sh

# 首次，或 sdkconfig 不存在时
idf.py --preview set-target esp32s31

# 编译
idf.py --preview build

# 烧写（USB-Serial/JTAG）
idf.py --preview -p /dev/ttyACM0 flash

# 普通复位（BOOT=1 稳定时足够）
python -m esptool --chip esp32s31 -p /dev/ttyACM0 \
  --before default-reset --after hard-reset chip-id

# 看日志：必须 --no-reset
idf.py --preview -p /dev/ttyACM0 monitor --no-reset
```

## 复位 / 启动模式（本工程实测）

- **BOOT=1 稳定后，hard reset 就能正常从 flash 启动**。启动日志应出现：
  ```
  boot:0x58 (SPI_FAST_FLASH_BOOT)
  ```
  随后是 bootloader、PSRAM、`Bluedroid enabled`、`Own address`、`AG PROF STATE: Init Complete`、`hfp_ag>`。
- `esptool --after hard-reset` 会走完整复位/启动流程，**不需要每次用 WDT**。
- **WDT 兜底**：芯片意外停在 download、hard reset / EN 无效时：
  ```bash
  python -m esptool --chip esp32s31 -p /dev/ttyACM0 \
    --before default-reset --after watchdog-reset chip-id
  ```
  WDT 是系统复位，实测也能正常启动；但它只是兜底，不是常规手段。
- **不要用**：
  - `idf.py monitor` 自带的 reset：它走 USB-Serial/JTAG 下载复位序列，会把芯片拉回 download。
  - OpenOCD / JTAG 的 `reset run`：只是 CPU reset，实测会停在 ROM（`0x2F8036CC`）。
- 如果 hard reset 仍落 download：检查 BOOT/GPIO0 在 CHIP_EN 上升沿是否为高、USB JTAG 的 DTR/RTS 是否把它拉低；必要时按 EN/断电复位。
- **不要把完整应用链接到 SRAM**：应用 `text+data ≈ 1.01 MB`，S31 SRAM 只有 512 KB（`0x2F000000–0x2F080000`）。bootloader 虽小，但 JTAG 直跳会触发 PMP fault；正常 flash 启动即可。

## 日志与验证

```bash
# 不 reset，直接看日志
.dsh/skills/esp32s31-idf/scripts/monitor.sh

# 或限时抓取
timeout 15 idf.py --preview -p /dev/ttyACM0 monitor --no-reset
```

期望的关键行：

```
boot:0x58 (SPI_FAST_FLASH_BOOT)
...
BT_APP_HF: Bluedroid enabled
BT_APP_HF: Own address:[...]
BT_APP_HF: AG PROF STATE: Init Complete
hfp_ag>
```

## 常见问题

| 现象 | 处理 |
|---|---|
| `esp32s31 is still in preview` | 所有 `idf.py` 命令加 `--preview` |
| `/dev/ttyACM0` 不存在 | WDT/复位时 USB 会短暂重枚举；等 1–2 秒，或按 `/dev/serial/by-id/...` 重试 |
| `LIBUSB_ERROR_ACCESS`（OpenOCD） | 需要 udev 规则让 `303a:1001` 节点为 `root:plugdev`（本机已修） |
| 烧写后停在 download | 按 EN/复位键，或 `reset.sh hard`；仍不行再 `reset.sh wdt` |
| `monitor` 一开就 download | 用 `monitor --no-reset` |
| HFP AG 卡在 Bluedroid enable | 确认 `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y` 和 `CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=1`；S31 默认可能是 BLE-only |
| UAC 没枚举 | 功能 USB（OTG）是否接主机；`main.c` 的 `ENABLE_UAC_BRINGUP` 是否为 1 |

## HFP/蓝牙调试备注

- AG console 支持 `help`、`con`、`cona`、`ac`、`dn <num>` 等命令。
- 用 Bumble HF 做测试时，**不要在通话/`dn`/`ac` 之前主动发 `AT+BCC`**：提前 BCC 会让 AG 的 SCO 状态进入 `BTA_AG_SCO_OPEN_ST`，后续音频打开事件被忽略，AG 停在 `Audio State connecting`。
- 正确路径是让 AG 的 `ac`/`dn` 驱动 codec/SCO；HF 收到的是 **SCO 请求**时，用 `HCI_Accept_Synchronous_Connection_Request`（不是 Enhanced Accept）。

## 交接关系

- 编译/烧写后看日志 → 本 skill 的 `monitor.sh`（或 `serial-monitor`）。
- JTAG 断点/寄存器 → OpenOCD `interface/esp_usb_jtag.cfg` + `target/esp32s31.cfg`，GDB 使用 `gdb-multiarch`（本机没有 `riscv32-esp-elf-gdb`）。
- 功能 USB / UAC 音频桥测试 → 接 OTG 后单独验证，不要与调试 USB 混淆。
