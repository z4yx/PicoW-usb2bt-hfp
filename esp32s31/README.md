# ESP32-S31 固件（USB UAC ↔ Bluetooth HFP AG）

本目录是项目从 RP2350 / Pico 2 W 迁移到 **ESP32-S31-WROOM-3-N16R16V**（16 MB flash + 16 MB PSRAM）后的固件。目标功能不变：

```
USB Host ──UAC──> ESP32-S31 ──HFP AG (SCO over HCI)──> Bluetooth 耳机/车机
          48 kHz 2ch out                         CVSD 8 kHz / mSBC 16 kHz
          48 kHz 1ch in
```

## 为什么换到 S31

- Pico W 的 CYW43439 固件无法完成 SCO over HCI（[pico-sdk#1461](https://github.com/raspberrypi/pico-sdk/issues/1461)），且 Pico W 没有引出 CYW43439 的 PCM/I2S，HFP 音频在 stock Pico W 上走不通。
- ESP32-S31 同时具备 **Bluetooth Classic（SCO/eSCO）** 和 **USB 2.0 HS OTG**，ESP-IDF 的 HFP AG/HF 例程已支持 S31，且 S31 的 HFP 明确走 **HCI 数据通路**（正好适合本项目的 USB 音频桥）。
- USB UAC 使用 Espressif 的 `usb_device_uac` 组件（TinyUSB），targets 列表包含 `esp32s31`。

## 目录

| 文件 | 说明 |
|---|---|
| `main/main.c` | NVS / BT controller / Bluedroid / HFP AG 初始化，启动 UAC 桥 |
| `main/bt_app_hf.c` | HFP AG 回调（基于 ESP-IDF `hfp_ag` 例程，音频回调改为接 UAC 桥） |
| `main/uac_bridge.c` | USB UAC ↔ HFP PCM 双向桥，FreeRTOS stream buffer 解耦 |
| `main/resampler.c` | 48 kHz ↔ 8/16 kHz 重采样（下采样盒式平均、上采样线性插值） |
| `main/app_hf_msg_set.c` | HFP AG 串口控制台（模拟来电/接听/挂断等，测试用） |
| `sdkconfig.defaults` | S31 / 16 MB flash / PSRAM / HFP HCI / UAC 配置 |
| `partitions.csv` | 4 MB app 分区 |
| `env.sh` | 本机 ESP-IDF 环境（工具装在仓库根目录的 `.esp-idf` / `.espressif`） |
| `main/idf_component.yml` | 依赖 `espressif/usb_device_uac` |

> Pico 相关代码仍保留在仓库根目录（`src/`、`CMakeLists.txt` 等）作为历史参考，本工程不编译它们；Pico 的 HFP 调试状态在 `wip/hfp-sco` 分支。

## 构建

要求 ESP-IDF **master（v6.2 预览版，支持 esp32s31）** 和 `riscv32-esp-elf` 工具链。

本机已安装在仓库根目录（被 `.gitignore` 忽略）：

```
.esp-idf/        # ESP-IDF master
.espressif/      # 工具链 + Python venv + component cache
```

```bash
cd esp32s31
source ./env.sh                 # 设置 IDF_PATH / IDF_TOOLS_PATH / 缓存目录
idf.py --preview set-target esp32s31
idf.py --preview build
```

> S31 目前是 preview target，所有 `idf.py` 命令都需要 `--preview`。

产物：

```
build/picow_usb2bt_hfp_s31.bin
build/bootloader/bootloader.bin
build/partition_table/partition-table.bin
```

## 烧写 / 串口

`usb_device_uac` 使用 S31 的 **USB OTG**（模块 `USB_DP/USB_DM`）。烧写和 HFP 控制台建议使用开发板的 **USB-UART / USB Serial-JTAG** 口（模块的 `TX0/RX0` 或调试 USB），不要把 OTG 口同时当成 UART。

```bash
idf.py --preview -p /dev/ttyUSB0 flash monitor
```

如果使用 USB Serial/JTAG 烧写，端口通常是 `/dev/ttyACM0`；具体以开发板原理图为准。

## 测试流程

1. 上电后主机应枚举出 USB 声卡（`PicoW USB2BT HFP`，48 kHz、2ch 播放 / 1ch 录音）。
2. 蓝牙耳机/车机作为 **HF** 连接 dongle 的 HFP AG（设备名 `Pico USB2BT HFP`）。
3. 通过 UART 控制台使用 HFP 例程命令（见 `app_hf_msg_set.c`）建立通话/音频；音频状态变为 connected 后，UAC 桥自动开始双向传输：
   - USB 播放 → 下混单声道 → 48k→8k/16k → HFP SCO 发送
   - HFP SCO 接收 → 8k/16k→48k → USB 麦克风
4. 用 `aplay` 播 1 kHz 正弦、`arecord` 录麦克风做端到端验证。

## 已知限制 / TODO

- **LC3-SWB（32 kHz）尚未桥接**：当前只处理 CVSD（8k）和 mSBC（16k）；LC3 状态会输出静音。
- USB 音量/静音回调目前只打印日志，尚未与 HFP AG 的 speaker/mic gain 双向同步。
- 重采样是简单盒式/线性算法，语音够用；音乐级质量需要 polyphase FIR。
- 缓冲区/延迟/时钟漂移还没有在硬件上调优（stream buffer 16 KB，HFP 7.5 ms 块）。
- ESP32-S31 + ESP-IDF v6.2 仍是 preview，需要在真实模组上验证 USB HS、PSRAM、HFP SCO。
- HFP AG 控制台默认走 UART；产品化时应改为按键/GPIO 或与 USB 音频状态联动。
