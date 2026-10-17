# 本机嵌入式 skill 安装说明（DSH）

来源：https://github.com/LeoKemp223/embed-ai-tool （clone 在 `.dsh/vendor/embed-ai-tool`）

## 目录布局

| 路径 | 作用 |
|---|---|
| `.claude/skills/<name>/` | install.py 的正式安装位置，含 `.em_skill_meta.json` 记录 |
| `.dsh/skills/<name>` | 指向 `.claude/skills/<name>` 的符号链接，DSH 从这里发现 skill |
| `.em_skill.json` | 工具路径配置（cmake / make / JLinkExe / openocd / gdb-multiarch 等） |
| `.dsh/vendor/embed-ai-tool` | 上游仓库副本，用于更新和重新安装 |
| `.dsh/skills/flash-openocd-rp2350/` | **本工程自建** skill（非上游），记录实测可用的 openocd 烧写流程 |
| `.dsh/skills/debug-openocd-rp2350/` | **本工程自建** skill（非上游），OpenOCD GDB Server + gdb-multiarch 调试 |
| `.dsh/patches/workflow-openocd.patch` | 对已安装 `workflow` skill 的改动补丁（cmake 烧录改为 OpenOCD） |
| `.dsh/bin/dsh-bwrap-usb` | bwrap 包装器：给 DSH 沙箱追加 `/dev/bus/usb` 直通（见文末「沙箱访问 USB」） |

DSH 的 skill 发现根：`<git root>/.dsh/skills/<name>/SKILL.md`，符号链接可用。

## 已安装（推荐集 6 个）

build-cmake, flash-jlink, debug-jlink, memory-analysis, serial-monitor, workflow

## 更新

```bash
cd .dsh/vendor/embed-ai-tool && git pull
cd - && python3 .dsh/vendor/embed-ai-tool/scripts/install.py . --force
```

（`--force` 不带 `--skills` 会重装全部 24 个；只想更新已有的 6 个就再带一次 `--skills` 列表。）

## 卸载

```bash
python3 .dsh/vendor/embed-ai-tool/scripts/install.py . --uninstall
rm -rf .dsh/skills/*
```

## 本工程自建 skill：flash-openocd-rp2350

上游 24 个 skill 里没有「OpenOCD + J-Link 烧 RP2350」这一条，本工程实测通道后补建：

```bash
# 环境自检（openocd / cfg / 产物 / lsusb）
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh --detect
# 烧写（默认 build-arm/PicoW_USB_BT_Audio.elf、rp2350、1000 kHz、verify+reset）
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh
# 只校验 / 提速 / 起 GDB Server
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh --verify-only
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh --speed 4000
.dsh/skills/flash-openocd-rp2350/scripts/flash_openocd.sh --attach
```

等价的裸命令（脚本内部就是它）：

```bash
openocd -f interface/jlink.cfg -c "transport select swd" -c "adapter speed 1000" \
        -f target/rp2350.cfg \
        -c "program build-arm/PicoW_USB_BT_Audio.elf verify reset exit"
```

该目录是真实目录（不是符号链接），所以上游 `install.py --force` 不会覆盖它；
另在 `.claude/skills/flash-openocd-rp2350` 放了一个反向符号链接，让 `workflow_runner.py`
（它按 `__file__` 解析出 `.claude/skills` 作为 skills 根）也能找到它。

## 本工程自建 skill：debug-openocd-rp2350（GDB 调试）

同一条 OpenOCD 通道起 GDB Server，前台 `gdb-multiarch`，退出时自动清理：

```bash
.dsh/skills/debug-openocd-rp2350/scripts/gdb_openocd.sh --detect   # 环境自检
.dsh/skills/debug-openocd-rp2350/scripts/gdb_openocd.sh            # 连上并 halt，进 (gdb)
.dsh/skills/debug-openocd-rp2350/scripts/gdb_openocd.sh --load     # 先烧再调
.dsh/skills/debug-openocd-rp2350/scripts/gdb_openocd.sh --batch --ex "info registers" --ex "bt"
```

行为要点：后台起 `openocd ... -c "gdb_port 3333" -c "telnet_port 4444"`，轮询 3333 最多 20 秒；
openocd 提前退出或端口就绪后进程消失都按 `connection-failure` 报错并打印日志尾部（退出码 3），
不会把环境问题伪装成调试问题；`trap` 保证 GDB 退出后 openocd 被清理。

同样在 `.claude/skills/debug-openocd-rp2350` 有反向符号链接，供 `workflow_runner.py` 解析。

## workflow 的本工程改动（cmake 烧录/调试走 OpenOCD）

上游 `workflow` 的 cmake 分支把烧录指向 `flash-openocd`（本工程没装），流水线在烧录步必然
`environment-missing`。改了三处：

| 文件 | 改动 |
|---|---|
| `scripts/workflow_runner.py` | cmake 的 flash 映射改为 `flash-openocd-rp2350/scripts/flash_openocd.sh`、debug 映射改为 `debug-openocd-rp2350/scripts/gdb_openocd.sh`；新增 `script_cmd()` 让非 `.py` 脚本直接执行；新增 `--build-dir` / `--build-type` / `--flash-speed`；烧录步合并 stderr 输出，缺 `Verified OK` 时告警 |
| `SKILL.md` | 记录覆盖关系、本工程实测命令与参数要点 |
| `references/usage.md` | 参数表与脚本映射表同步更新 |

为什么必须加 `--build-dir/--build-type`：`build-cmake` 缺省会在源码目录新建 `build/`，且缺省按
Debug 配置；本工程实际是 `build-arm` + Release，不给这两个参数等于从零全量重编并改构建类型。

改动以补丁留档（上游更新会覆盖，需要重打）：

```bash
python3 .dsh/vendor/embed-ai-tool/scripts/install.py . --force --skills workflow  # 覆盖回上游版本
patch -p1 -d .claude/skills < .dsh/patches/workflow-openocd.patch                 # 重新应用改动
```

## 本机注意事项

1. **板卡是 RP2350，不是 RP2040**。`build-arm/CMakeCache.txt` 里 `PICO_BOARD=pico2_w`、
   `PICO_PLATFORM=rp2350-arm-s`（`CMakeLists.txt:27` 的 `set(FAMILY rp2040)` 是遗留变量，不决定平台）。
   `/opt/SEGGER/JLink_Linux_V688c_x86_64` 是 V6.88c（2020-12），器件库里没有 RP2350 条目，
   所以 `flash-jlink` 走不通（未知器件 / project-config-error），除非把 J-Link 软件包升到 V7.9x+。
   **本工程已验证的烧写通道是 OpenOCD + J-Link**。
2. **USB 访问**：DSH 的 bash 沙箱内 `/dev` 只有最小节点集合，没有 `/dev/bus/usb`，
   因此沙箱里跑 openocd / JLinkExe 都会打不开设备（`--detect` 仍能看到探针，因为走 sysfs 枚举）。
   要让沙箱看到 USB 设备，见下节「沙箱访问 USB 设备」。
3. **探针**：USB 上枚举到的是 `SAM-ICE`（序列号 20171111），旧款 OEM J-Link；
   OpenOCD 用 `interface/jlink.cfg` 能正常驱动它（已实测烧写成功），但升级到 V7.x 软件包时
   可能被要求更新固件，克隆/OEM 探针有失败风险。
4. **工具链**：`arm-none-eabi-gcc` 与 `ninja` 不在 PATH 中；交叉编译器来自 pico-sdk 自带工具链
   （`build-arm/` 下已有成功产物），CMake 生成器回退为 `Unix Makefiles`。

## 沙箱访问 USB 设备（已应用，已实测烧写）

根因在 `@deepseek-ai/dsh-sandbox-local` 的 bwrap profile：`--ro-bind / / --dev /dev ...`
里 `--dev /dev` 会新建一个只含基础节点（null/zero/urandom/tty…）的 devtmpfs，
宿主 `/dev/bus/usb` 因此不可见。该包只暴露 `runnerCommand` 一个入口，没有"设备白名单"配置。

做法：用包装器在 profile 参数之后（`--` 之前）补设备挂载。当前绑三类：

```bash
--dev-bind /dev/bus/usb /dev/bus/usb     # J-Link 探针
--dev-bind /dev/ttyACM*  <同路径>         # CDC-ACM（本机串口就是它）
--dev-bind /dev/ttyUSB*  <同路径>         # USB-UART 桥（CH340/CP210x 等）
--dev-bind /dev/serial   /dev/serial     # by-id 稳定符号链接
```

**当前状态**（已落地）：

- 包装器：`~/.dsh/bin/dsh-bwrap-usb`（源文件就是本仓库 `.dsh/bin/dsh-bwrap-usb`）
- 配置：`~/.dsh/profiles/web/cordis.patch.yml` 末尾追加了 `id: sandbox` 覆盖，
  原文件已备份为 `cordis.patch.yml.bak-<时间戳>`

```yaml
- id: sandbox
  name: '@deepseek-ai/dsh-sandbox-local'
  config:
    runnerCommand: ['/home/zhang/.dsh/bin/dsh-bwrap-usb']
    runnerFailureSignatures: ['bwrap: ']
```

**不用重启**：`dsh-hmr` 会 watch 这个 patch 文件，改完立刻 `reconcileProfilePatches`，
下一条 bash 就生效（实测改完当场就能 `ls /dev/bus/usb`）。

验证记录：

| 检查 | 结果 |
|---|---|
| 包装器对假源注入 | 沙箱内可见并 `cat` 到 `/dev/bus/usb/001/016` |
| 源不存在 | 跳过挂载，命令照常执行（不会 `Can't find source path`） |
| 真实宿主 `/dev` 经包装器 | 沙箱内出现完整 `/dev/bus/usb`，0666 节点可 `open(RW)` |
| 沙箱内 `openocd -f interface/jlink.cfg -c "transport select swd" -c init` | `J-Link OB-STM32F072-CortexM`、`VTarget = 3.300 V`、`clock speed 1000 kHz`，rc=0 |
| 沙箱内 `flash_openocd.sh` 真实烧写 | `** Programming Finished **` → `** Verified OK **` → `** Resetting Target **`，rc=0 |
| 沙箱内 `/dev/ttyACM0` | `os.open(O_RDWR)` 成功；`serial_monitor.py --port /dev/ttyACM0 --baud 115200` 抓到完整固件日志 |

代价与边界：该改动作用于**所有**受沙箱约束的 bash 命令（fs 工具不受影响），并把整条
USB 总线与所有串口节点暴露给命令；`runnerCommand` 会跳过 bwrap 可用性探测并强制断言
`enforcement: full`，包装器一旦写错将导致所有 bash 命令失败
（`runnerFailureSignatures` 用于识别这种失败）。
更窄的写法是只绑具体节点 `--dev-bind /dev/bus/usb/001/016 ...`，但 USB 重新枚举后编号会变。

回退：删掉 `cordis.patch.yml` 里那段 `- id: sandbox` 即可（HMR 会立刻恢复默认 bwrap）。

替代方案（不改配置）：单条命令用 `sandbox_permissions: danger-full-access` 提权（逐次批准），
或启动时 `DSH_PERMISSION_MODE=danger-full-access`（注意此时审批策略会变成 never）。

## 串口监控（已验证可用）

端口 `/dev/ttyACM0`，115200 8N1。已经写进 `.em_skill.json` 的 `project_profile`
（`serial_port` / `baud_rate`），下游 skill 会把它当默认值复用。

**设备拓扑（重要，别被名字骗了）**：

- `/dev/ttyACM0` 背后是 **J-Link 探针的 CDC-ACM**（`1366:0105`，serial `000020171111`，
  `/sys/class/tty/ttyACM0/device` 指到它），也就是 J-Link OB 的虚拟 COM，接到目标板 UART。
- 板子自己的 USB 设备是 `cafe:4010 TinyUSB BT`，**只有 3 个 UAC 音频接口**，
  没有 CDC：`tusb_config.h` 里 `CFG_TUD_CDC 0`、`CFG_TUD_AUDIO 1`。
- 固件确实把 printf 走 UART：`build-arm/CMakeFiles/PicoW_USB_BT_Audio.dir/link.txt` 里链了
  `pico_stdio_uart/stdio_uart.c.o`，且 `CMakeLists.txt` 没调 `pico_enable_stdio_usb`。

所以"串口 = ttyACM0"是对的，但不是板子的 USB CDC，而是探针的 VCOM 转发 UART。

依赖：`pyserial` 已装到用户级（`pip install --user --break-system-packages pyserial`，3.5，
在 `~/.local/lib/python3.12/site-packages`），系统 python3 直接可用，流水线里的
`serial_monitor.py` 也能用。

```bash
# 抓 5 秒
python3 .claude/skills/serial-monitor/scripts/serial_monitor.py --port /dev/ttyACM0 --baud 115200 --duration 5
# 自动选口（当前只有 ttyACM0，选它）
python3 .claude/skills/serial-monitor/scripts/serial_monitor.py --auto --duration 3
# 先清缓冲再抓，避免读到打开前残留的半个包（不清会看到几行乱码）
python3 .claude/skills/serial-monitor/scripts/serial_monitor.py --port /dev/ttyACM0 --baud 115200 --duration 3 --clear
# 抓启动日志：先起监控，再复位目标
( sleep 2; openocd -f interface/jlink.cfg -c "transport select swd" -c "adapter speed 1000" \
    -f target/rp2350.cfg -c "init" -c "reset run" -c "shutdown" ) &
python3 .claude/skills/serial-monitor/scripts/serial_monitor.py --port /dev/ttyACM0 --baud 115200 --duration 8 --clear --timestamp
```

实测抓到的启动日志（节选）：

```
init slot is 1
init ctw43.
Stored First link key:
currect slot iss 1
cur slot1 flash addr is 04:7A:0B:4D:B7:60
 Start scanning...
Get cfgdesc: 0 / Get string desc: 2 / Clock get 1 freq ranges / Range 0 (48000, 48000, 0)
```

注意：不清缓冲时开头几行会是上个会话残留的乱码；空闲时串口没有输出（固件只在事件上 printf）。

流水线里带端口跑（已验证 3 步全绿）：

```bash
python .claude/skills/workflow/scripts/workflow_runner.py \
  --run build-flash-monitor --build-system cmake \
  --project . --build-dir build-arm --build-type Release \
  --port /dev/ttyACM0 --baud 115200
```
