# AgentTouch 2.16" 板 → Hermes Gadget 移植：本地笔记本操作手册

> 给本地 agent 的指令。目标：把 Waveshare ESP32-S3-Touch-AMOLED-2.16 刷成 Hermes Gadget，
> 连上 Hermes 服务端（<HERMES_SERVER_IP>，gadget 端口 8790）。
> 服务端（<HERMES_SERVER_IP>）已完成：插件已装、gateway 已监听 8765→8790、模拟器已配对验证过。
> 移植代码已在服务端 `/root/hermes-gadget-sdk` 写好并编译出镜像。

## 前提

- 笔记本用 USB-C 数据线插着板子（板子的 USB-C 是 ESP32-S3 原生 USB，串口和烧录都走它）。
- 找到串口号：Linux 看 `/dev/serial/by-id/` 或 `ls /dev/ttyACM*`；Windows 看设备管理器 COM 号。
- 笔记本能访问 <HERMES_SERVER_IP>（Hermes 服务端）。

## 第 1 步：从服务端取固件（三合一镜像）

服务端已编译好整板镜像（bootloader+分区表+OTA+app，从 0x0 烧）：

```bash
scp root@<HERMES_SERVER_IP>:/root/hermes-gadget-sdk/firmware/esp32/dist/hermes-gadget-esp32s3-touch-amoled-2.16-0.1.1.bin ./gadget.bin
# 校验（sha256）：4d5b4fb70d28848856c99ae4d8c1b341e71c831edaaf4f6aac2ece805e458a7c  gadget.bin
```

## 第 2 步：烧录

笔记本只需要 esptool（`pip install esptool`），不需要 PlatformIO/ESP-IDF：

```bash
# 确认能识别芯片（应报 ESP32-S3, chip rev v0.2 左右）
esptool.py --chip esp32s3 -p <串口号> chip_id

# 整板镜像从 0x0 烧（会擦掉旧 AgentTouch 固件和 NVS）
esptool.py --chip esp32s3 -p <串口号> -b 921600 write_flash 0x0 gadget.bin
```

注意：
- 如果烧录失败报连接错误，让板子进下载模式再烧：按住**左键（BOOT）**不放的同时按一下复位（或拔插 USB），松开左键后再执行 write_flash。
- 烧完板子自动重启。整板镜像擦掉了 NVS，所以设备会以新身份重新配对（这是预期）。

## 第 3 步：配 Wi-Fi + 服务器地址

板子重启后，用 SDK 的串口配置工具（笔记本上，SDK 仓库任意 checkout 都行，或 `pip install hermes-gadget`）：

```bash
python -m pip install -e ".[serial]"   # 或 pip install hermes-gadget pyserial
hermes-gadget provision --port <串口号> \
  --wifi-ssid "<你的Wi-Fi名>" \
  --wifi-pass "<Wi-Fi密码>" \
  --server ws://<HERMES_SERVER_IP>:8790/gadget \
  --name "Desk216"
```

也可以用板子自带串口终端（`gadget>` 提示符）手敲：

```
set wifi_ssid <你的Wi-Fi名>
set wifi_pass <你的Wi-Fi密码>
set server ws://<HERMES_SERVER_IP>:8790/gadget
set name Desk216
status
```

或者手机配网：板子串口敲 `wifi-setup`，手机连板子发出的热点，浏览器打开配置页填 Wi-Fi 和服务器地址。

## 第 4 步：配对（在服务端做，或直接跟 Hermes 说）

板子连上 Wi-Fi 后会连 gateway，屏幕上显示 8 位配对码。然后在服务端：

```bash
hermes pairing list                 # 看到待配对设备和码
hermes pairing approve gadget <码>
```

或者直接对 Hermes 说「批准 gadget 配对」。配对成功后板子进入 Ready。

## 第 5 步：使用

- **按住屏幕任意处说话**，松开 → 转头发给 Hermes → 屏幕显示回复 + 喇叭播报。
- 左键 BOOT = 备用 TALK；右键 IO18 = CANCEL（长按 2 秒开新会话）。
- 点一下屏幕 = 对确认问题回答「是」。
- 串口诊断：`gadget> diag`（看 display/mic/speaker/touch 是否都 ready）。

## 已知风险 / 排错

- 这是**新移植的 board profile**（服务端写的，未上官方 CI 发布），首次运行留意：
  - 屏幕不亮但 `diag` 显示 `display co5300`：MADCTL/供电轨问题，把串口日志发回给 Hermes。
  - 触摸方向不对：改 `board.cpp` 里 touch 的 mirror/swap 三个开关重编译。
  - 没声音：看 `diag` 的 speaker 是否 es8311；PA 使能脚 GPIO46。
  - 麦克风没声：`diag` 看 mic 是否 es7210。
- 中键（PWR）是 AXP2101 电源键，本移植未接它（保持硬件开关角色）。
- 服务端 STT/TTS 已配置（edge-tts 中文）；板子端不做语音识别。
- 以后升级固件不用拆机：服务端 `hermes gadget update "Desk216" --latest`（或指定 bin）走 OTA。
