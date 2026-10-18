# 微雪 ESP32-S3-Touch-AMOLED-2.16 × Hermes Gadget 烧录与接入教程

本分支（`216-board`）为该板提供完整 Hermes Gadget 固件：中文屏显（GB2312 字库）、
中文唤醒词免提（包内置"小爱同学"，WakeNet9）、本板麦克风/静音检测调优、默认音量 100、插电可常亮。

前提：一台运行 Hermes gateway 的机器（下文称"服务器"），已按根 README 安装 gadget
插件并监听（默认端口 8765；被占用时改 `platforms.gadget.extra.port` 后重启 gateway）。

## 第 0 步：准备

- 板子用 **USB-C 数据线**插到一台电脑（这块板的 USB-C 是 ESP32-S3 原生 USB，串口和烧录都走它）
- 找串口号：Linux 看 `ls /dev/serial/by-id/` 或 `/dev/ttyACM*`；Windows 看设备管理器 COM 号
- 烧录只需要 `esptool`（`pip install esptool`），不需要 PlatformIO / ESP-IDF

## 第 1 步：获取固件

**方式 A（推荐）：Release 直接下载**

Releases 页下载 `gadget-216-*.zip`（解压即得下表全部文件 + 本说明副本）。

**方式 B：自己编译**

```bash
pip install platformio
cd firmware/esp32
pio run -e esp32s3-touch-amoled-216
# 产物（.pio/build/esp32s3-touch-amoled-216/）：
#   bootloader.bin  partitions.bin  firmware.bin  ota_data_initial.bin
# 唤醒词模型分区需额外两步：
ninja -C .pio/build/esp32s3-touch-amoled-216 srmodels/srmodels.bin
esptool.py --chip esp32s3 merge_bin -o merged.bin --flash_mode dio \
  --flash_freq 80m --flash_size 16MB \
  0x0 .pio/build/esp32s3-touch-amoled-216/bootloader.bin \
  0x8000 .pio/build/esp32s3-touch-amoled-216/partitions.bin \
  0xf000 .pio/build/esp32s3-touch-amoled-216/ota_data_initial.bin \
  0x20000 .pio/build/esp32s3-touch-amoled-216/firmware.bin \
  0x400000 .pio/build/esp32s3-touch-amoled-216/srmodels/srmodels.bin
```

（Release zip 里的对应文件已改名：`partitions.bin`→`partition_table.bin`、
`firmware.bin`→`hermes_gadget.bin`。）

## 第 2 步：烧录（首次必须 USB；之后可 OTA）

分件烧录：

```bash
esptool.py --chip esp32s3 -p <串口> -b 921600 write_flash \
  0x0      bootloader.bin \
  0x8000   partition_table.bin \
  0xf000   ota_data_initial.bin \
  0x20000  hermes_gadget.bin \
  0x400000 srmodels.bin
```

或合并镜像一把梭：

```bash
esptool.py --chip esp32s3 -p <串口> -b 921600 write_flash 0x0 \
  hermes-gadget-216-merged-0.2.0-cjk1.bin
```

⚠️ **`srmodels.bin` 必须烧**（唤醒词模型分区）。烧漏会无限 `Bootloader corrupted`
重启循环——恢复方法：按住 **BOOT0** 同时插 USB 进下载模式，重烧全套。

烧录失败报连接错误时，同样用"按住 BOOT0 插 USB"进下载模式再烧。
整板镜像会擦掉旧固件和 NVS，设备将以新身份重新配对（预期行为）。

## 第 3 步：配 Wi-Fi + 服务器地址

三选一：

**A. 串口配置工具**（任意机器 checkout 本仓库即可）：

```bash
pip install -e ".[serial]"
hermes-gadget provision --port <串口> \
  --wifi-ssid "<你的Wi-Fi名>" --wifi-pass "<密码>" \
  --server ws://<服务器IP>:<端口>/gadget \
  --name "Desk216"
```

**B. 板子自带串口终端**（`gadget>` 提示符手敲）：

```
set wifi_ssid <你的Wi-Fi名>
set wifi_pass <密码>
set server ws://<服务器IP>:<端口>/gadget
set name Desk216
status
```

**C. 手机配网**：串口敲 `wifi-setup`，手机连板子发出的热点，浏览器打开配置页填 Wi-Fi 与服务器地址。

## 第 4 步：配对

板子连上服务器后屏幕显示 **8 位配对码**，在服务器批准：

```bash
hermes pairing list
hermes pairing approve gadget <配对码>
```

配对成功后板子进入 READY。此后所有通信走服务器，**板端不存任何模型密钥**；
换服务器只需改 `set server`。

## 第 5 步：使用

- **按住屏幕任意处（TALK）说话**，松开发给 Hermes → 屏幕显示中文回复 + 喇叭播报
- 免提：说唤醒词 **"小爱同学"**（本固件包内置该模型；换其他唤醒词需重新编译并补烧
  `srmodels.bin`，或在 Settings → wakeword 关闭）
- **CANCEL**（IO18 键）打断播报；长按 2 秒开新会话
- Settings → display → screen timeout = 0：屏幕常亮
- 唤醒词生效期间麦克风 20ms 帧实时在检，检测只在本地、**不上传音频**；
  只有唤醒/按键之后才向服务器上传录音
- 串口自检：`gadget> diag`（display co5300 / touch cst9220 / mic es7210 / speaker es8311 全 ready 即硬件 OK）

## 排错

| 症状 | 处理 |
|---|---|
| 无限重启循环 | `srmodels.bin` 没烧，按住 BOOT0 插 USB 重烧全套 |
| 屏幕不亮但 diag 显示 display ready | 显示供电轨问题，串口日志反馈 issue |
| 说话无反应 | 先按 TALK 键测试；服务器端确认 `stt.local.language = zh`；唤醒词需 Settings 里确认已 on |
| 有显示没声音 | 服务器端确认 `tts.edge.voice = zh-CN-XiaoxiaoNeural`（或你的中文嗓音） |
| 配对码不出现 | `curl -i http://<服务器>:<端口>/gadget` 应返回 426 Upgrade Required；检查防火墙 |
| 升级固件 | 不用拆机：服务器执行 `hermes gadget update "<设备名>" --latest` 走 OTA |

## 致谢与许可

见根目录 `NOTICE`：上游 [Adolanium/hermes-gadget-sdk](https://github.com/Adolanium/hermes-gadget-sdk)（MIT）、
字库 [Noto Sans CJK SC](https://fonts.google.com/noto)（SIL OFL 1.1）、
引脚映射参考 [AgentTouch](https://github.com/wentong2022-arch/agenttouch)、唤醒词模型来自 Espressif [esp-sr](https://github.com/espressif/esp-sr)。
