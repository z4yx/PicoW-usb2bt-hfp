# PicoW USB2BT HFP — ESP32-S31

当前活跃固件在 [esp32s31/](esp32s31/README.md)，目标平台 **ESP32-S31-WROOM-3-N16R16V**（16 MB flash / 16 MB PSRAM）。

```
USB Host ──UAC──> ESP32-S31 ──HFP AG (SCO over HCI)──> Bluetooth 耳机
          48 kHz 2ch out                         CVSD 8 kHz / mSBC 16 kHz
          48 kHz 1ch in
```

- 协议栈：ESP-IDF v6.2（S31 preview target）、Bluedroid HFP AG、TinyUSB UAC
- 原 RP2350 / Pico 2 W 固件已移除：CYW43439 固件无法完成 SCO over HCI（[pico-sdk#1461](https://github.com/raspberrypi/pico-sdk/issues/1461)），且 Pico W 未引出 CYW43439 的 PCM/I2S，HFP 音频不可用
- Pico 旧代码可在 git 历史与 `wip/hfp-sco` 分支中查看

## 构建

```bash
cd esp32s31
source ./env.sh
idf.py --preview set-target esp32s31
idf.py --preview build
```

本机 ESP-IDF 与工具链装在仓库根目录的 `.esp-idf/`、`.espressif/`（已被 git 忽略）；新环境请参考 [esp32s31/README.md](esp32s31/README.md) 安装 ESP-IDF master（支持 esp32s31）。

## License

见 [LICENSE.txt](LICENSE.txt)。
