# Flashing guide / 烧录说明

Two ways: **A. flash the prebuilt binary** (fastest, no toolchain needed) or
**B. build from source** with arduino-cli / Arduino IDE.

两种方式：**A 直接刷预编译固件**（最简单），**B 从源码编译**。

---

## A. Prebuilt firmware / 刷预编译固件

File: `firmware/attitude-indicator-merged.bin` (≈1.3 MB, bootloader + partition table + app, **flash at 0x0**).

### A1. Windows + Python (recommended / 推荐)

```powershell
pip install esptool
# put the board in download mode if needed: hold BOOT, tap RESET, release BOOT
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash 0x0 firmware\attitude-indicator-merged.bin
```

Replace `COM6` with your port (`Get-PnpDevice -Class Ports` / Device Manager).

Or simply double-click **`flash.bat`** (it asks for the COM port).

### A2. Windows with esptool from the ESP32 Arduino core

If you already have the ESP32 core installed, no pip needed:

```powershell
& "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\4.9.dev3\esptool.exe" `
  --chip esp32s3 -p COM6 -b 921600 write_flash 0x0 firmware\attitude-indicator-merged.bin
```

### A3. Browser flasher / 浏览器刷机（免安装）

Open <https://esphome.github.io/esp-web-tools/> or <https://esp.huhn.me/>,
connect the board with a **data** USB-C cable, choose chip **ESP32-S3**,
add the file at offset **0x0**, and flash.

> Note: the board has a native USB-Serial/JTAG port, no CP210x driver is required.

### First run after flashing / 首次上电

1. The screen shows the logo, then cross-fades into the instrument.
2. **The firmware zeroes itself at boot** — whatever attitude it is in becomes "level".
   If you moved the device afterwards, place it in the attitude you want and
   **short press BOOT** to re-cage.
3. Drag up/down on the screen to set the brightness you like (it is remembered).

1. 屏幕显示 Logo，0.8 秒后渐隐进入仪表画面。
2. **开机自动归零**：开机时的姿态就是"水平"。如果之后挪动过设备，
   把它摆到你要的水平姿态，**短按 BOOT** 重新对齐即可。
3. 上/下拖动屏幕调节亮度（会记住）。

---

## B. Build from source / 从源码编译

### Requirements

| Item | Version |
| --- | --- |
| arduino-cli (or Arduino IDE 2.x) | 1.0+ |
| ESP32 Arduino core | **3.1.1** |
| LVGL | **8.3.10** (place in a `libs/` folder next to the sketch) |

```bash
# one-time setup
arduino-cli core install esp32:esp32@3.1.1
git clone --branch v8.3.10 --depth 1 https://github.com/lvgl/lvgl.git libs/lvgl
```

### Board settings / 开发板设置

| Setting | Value |
| --- | --- |
| Board | *Waveshare ESP32-S3-Touch-LCD-1.85* |
| FQBN | `esp32:esp32:waveshare_esp32_s3_touch_lcd_185` |
| Partition Scheme | `app3M_fat9M_16MB` |
| USB CDC On Boot | `cdc` |
| USB Mode | `hwcdc` |
| Upload Speed | `921600` |
| PSRAM | OPI PSRAM (default for this board) |

Windows one-liner:

```powershell
.\src\build.ps1 -Upload -Monitor      # compile + flash + open serial monitor
```

Manual:

```powershell
arduino-cli compile --fqbn "esp32:esp32:waveshare_esp32_s3_touch_lcd_185:PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600,DebugLevel=error" --libraries .\libs --build-path .\_build .\src\AttitudeBall
arduino-cli upload  -p COM6 --fqbn "<same FQBN>" --input-dir .\_build .\src\AttitudeBall
```

### Wireless update / 无线升级

Configure `src/AttitudeBall/Config.h` (`WIFI_SSID` / `WIFI_PASS` / `OTA_PASSWORD`),
then:

```powershell
.\src\ota.ps1 -Ip 192.168.1.50
```

The board announces itself as `attitude-indicator.local` (mDNS).

---

## Troubleshooting / 常见问题

| Symptom | Cause / fix |
| --- | --- |
| **Port not found** | Use a data cable; hold BOOT + tap RESET to force download mode; native USB-JTAG needs no driver |
| **Black screen after flash** | Wrong offset (must be `0x0` for the merged image) or a non-16 MB partition layout — use the merged file |
| **Image upside down** | Long press BOOT to rotate 180° |
| **Horizon tilted / drifts** | Re-cage: place the device level, short press BOOT |
| **Wrong tilt direction** | The firmware has runtime switches: `hpf` (pitch flip), `hrf` (roll flip), `hflip` (180° flip), `xflip` — send them over serial, see `src/README.md` |
| **Screen flashes colours at power-on** | Fixed in this version: backlight is enabled only after the first frame is flushed |
| **Flashing flips the screen by 180°** | Known ESP32-S3 quirk: esptool toggles DTR → GPIO0 → the firmware thinks BOOT is held. Just long press BOOT once or send `srot 0` over serial |
