# ESP-IDF 本地补丁

本目录保存 S31 工程依赖、但不在 ESP-IDF 上游的本地修改。`.esp-idf/` 本身被
`.gitignore` 忽略，重新安装/切换 IDF 后需要重新打补丁。

## 补丁列表

| 补丁 | 作用 | 是否必需 |
|---|---|---|
| `0001-bta-ag-sco-default-cvsd-pkt-len.patch` | S31 的 HCI SCO 连接上报 `tx_pkt_len == 0`，导致 `bta_ag_sco.c` 的 `out_pkt_len/in_pkt_len` 为 0，HFP outgoing 回调拿到 `sz=0`，`BTM_WriteScoData()` 永远不发包。补丁把 CVSD 的包长默认成实测的 **40 字节**。 | 必需 |
| `0002-bta-ag-sco-log-sco-tx-status.patch` | 每包打印 `BTM_WriteScoData()` 的返回值和 SCO 状态，仅用于 bring-up 调试，日志多且是 ERROR 级。 | 可选 |

两个补丁都只改：

```
components/bt/host/bluedroid/bta/hf_ag/bta_ag_sco.c
```

## 应用

```bash
# 只打必需补丁
esp32s31/patches/apply.sh

# 同时打调试补丁
esp32s31/patches/apply.sh --debug

# 撤销（debug 补丁先撤）
esp32s31/patches/apply.sh --debug --revert
```

`apply.sh` 默认使用 `$IDF_PATH`；未设置时使用仓库根目录的 `.esp-idf`。也可以显式指定：

```bash
IDF_PATH=/path/to/esp-idf esp32s31/patches/apply.sh
```

脚本会检测补丁是否已经打过，重复执行不会重复应用。

## 手工应用 / 撤销

```bash
IDF_PATH=.esp-idf
patch -p1 -d "$IDF_PATH" < esp32s31/patches/0001-bta-ag-sco-default-cvsd-pkt-len.patch
patch -p1 -d "$IDF_PATH" < esp32s31/patches/0002-bta-ag-sco-log-sco-tx-status.patch

# 撤销
patch -p1 -R -d "$IDF_PATH" < esp32s31/patches/0002-bta-ag-sco-log-sco-tx-status.patch
patch -p1 -R -d "$IDF_PATH" < esp32s31/patches/0001-bta-ag-sco-default-cvsd-pkt-len.patch
```

## 验证

打完后重新编译：

```bash
cd esp32s31
source ./env.sh
idf.py --preview build
```

启动 HFP 音频后，日志里应看到 outgoing 回调的 `sz=40`，以及（如果打了 0002）
`SCO TX write_stat=0 len=40`。没有 0001 时典型现象是 `sz=0`、`SCO TX` 完全不出现。

本目录补丁基于 ESP-IDF `1c42e514` 验证。
