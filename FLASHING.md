# 烧录说明

两种方式：**A. 直接刷预编译固件**（最简单，不需要装工具链），
或 **B. 从源码编译**（arduino-cli / Arduino IDE）。

---

## A. 刷预编译固件

文件：`firmware/attitude-indicator-merged.bin`（约 1.3 MB，已包含引导程序 + 分区表 + 应用，
**烧录偏移 0x0**）。也可从 [Releases](https://github.com/Hanice404/ESP32-AttitudeIndicator/releases/latest) 下载。

### A1. Windows + Python（推荐）

```powershell
pip install esptool
# 如果识别不到端口：按住 BOOT，点一下 RESET，再松开 BOOT
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash 0x0 firmware\attitude-indicator-merged.bin
```

把 `COM6` 换成你的实际端口（`Get-PnpDevice -Class Ports` 或设备管理器查看）。

或者直接双击 **`flash.bat`**，脚本会列出可用串口并提示你输入。

### A2. 用 ESP32 Arduino 核心自带的 esptool（已装过核心就不需要 pip）

```powershell
& "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\4.9.dev3\esptool.exe" `
  --chip esp32s3 -p COM6 -b 921600 write_flash 0x0 firmware\attitude-indicator-merged.bin
```

### A3. 浏览器在线烧录（免安装）

打开 <https://esp.huhn.me/> 或 <https://esphome.github.io/esp-web-tools/>，
用**数据线**连接开发板，芯片选 **ESP32-S3**，文件偏移填 **0x0**，选择上面的 bin 文件烧录。

> 说明：本板使用 ESP32-S3 原生 USB-Serial/JTAG，**不需要装 CH340/CP210x 驱动**。
> 必须用能传数据的 USB-C 线（很多充电线只有电源）。

### 首次上电

1. 屏幕先显示 Logo，0.8 秒渐隐后进入仪表画面。
2. **固件在开机时会自动归零**：开机那一刻的姿态就是"水平"。
   如果之后挪动过设备，把它摆到你要的水平姿态，**短按 BOOT** 重新对准即可。
3. 上下拖动屏幕调节亮度（会记住，掉电不丢）。

---

## B. 从源码编译

### 环境要求

| 项目 | 版本 |
| --- | --- |
| arduino-cli（或 Arduino IDE 2.x） | 1.0 以上 |
| ESP32 Arduino 核心 | **3.1.1** |
| LVGL | **8.3.10**（放在 `libs/` 目录下） |

```bash
# 一次性准备
arduino-cli core install esp32:esp32@3.1.1
git clone --branch v8.3.10 --depth 1 https://github.com/lvgl/lvgl.git libs/lvgl
```

### 开发板设置

| 设置项 | 取值 |
| --- | --- |
| 开发板 | 微雪 ESP32-S3-Touch-LCD-1.85 |
| FQBN | `esp32:esp32:waveshare_esp32_s3_touch_lcd_185` |
| 分区方案 | `app3M_fat9M_16MB` |
| USB CDC On Boot | `cdc` |
| USB Mode | `hwcdc` |
| 上传速度 | `921600` |
| PSRAM | OPI PSRAM（本板默认） |

Windows 一条命令（编译 + 烧录 + 开监视器）：

```powershell
.\src\build.ps1 -Upload -Monitor
```

手动方式：

```powershell
arduino-cli compile --fqbn "esp32:esp32:waveshare_esp32_s3_touch_lcd_185:PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600,DebugLevel=error" --libraries .\libs --build-path .\_build .\src\AttitudeBall
arduino-cli upload  -p COM6 --fqbn "<同上>" --input-dir .\_build .\src\AttitudeBall
```

### 无线升级（OTA）

先在 `src/AttitudeBall/Config.h` 里填好 `WIFI_SSID` / `WIFI_PASS` / `OTA_PASSWORD`，然后：

```powershell
.\src\ota.ps1 -Ip 192.168.1.50
```

设备会以 `attitude-indicator.local`（mDNS）广播自己，也可以直接用这个域名。

---

## 常见问题

| 现象 | 原因 / 解决 |
| --- | --- |
| **找不到串口** | 换一根能传数据的线；按住 BOOT 点 RESET 强制进入下载模式；原生 USB-JTAG 不需要驱动 |
| **烧完黑屏** | 偏移写错（合并固件必须写 `0x0`），或分区方案不是 16 MB —— 请用合并后的 bin |
| **画面倒过来** | 长按 BOOT 切回 180° 方向 |
| **地平线歪了 / 慢慢偏** | 重新对准：摆好姿态后短按 BOOT |
| **倾斜方向不对** | 固件内置运行时开关：`hpf`（翻俯仰）、`hrf`（翻滚转）、`hflip`（整体翻转）、`xflip`，用串口发送即可，详见 `src/README.md` |
| **上电瞬间闪红绿蓝** | 本版本已修复：背光改为"首帧刷完才点亮" |
| **烧录后屏幕上下颠倒** | ESP32-S3 已知现象：esptool 会拉 DTR → GPIO0，固件误判为按住 BOOT。长按一次 BOOT 或串口发 `srot 0` 即可恢复 |
