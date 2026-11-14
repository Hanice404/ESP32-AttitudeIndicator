# ESP32 Attitude Indicator

A flight-instrument style **artificial horizon (attitude indicator)** running on the
[Waveshare ESP32-S3-Touch-LCD-1.85](https://www.waveshare.com/esp32-s3-touch-lcd-1.85.htm)
round 360×360 display.

![Screenshot](docs/screenshot.png)

*Actual framebuffer captured from the device (360×360, RGB565).*

---

## Features

| Feature | Description |
| --- | --- |
| **Artificial horizon** | Sky/ground with dithered gradient, white horizon line, pitch ladder (±10/20/30° with digits), roll scale ±60° with a moving red pointer, and a fixed yellow aircraft symbol |
| **One-button cage** | Short press **BOOT** → the current attitude becomes "level" (stored in NVS). Also happens automatically at every boot |
| **Boot logo** | Built-in logo with a 0.8 s cross-fade into the instrument |
| **Touch dimming** | Drag up/down to change brightness in 10 % steps; a yellow bar appears while adjusting. Value is persisted. **No auto dim / auto screen-off** |
| **Battery** | ADC readout on the serial console |
| **WiFi OTA** | ArduinoOTA, password-protected |
| **Crash guard** | After 2 abnormal resets the firmware enters a safe mode (serial + WiFi only) so the board cannot get bricked |

No gimbal lock: the horizon is computed from the gravity vector with a projective
(elevation-based) model, so it stays correct even when the device is vertical or
tilted 90°.

---

## Hardware

- **Board**: Waveshare ESP32-S3-Touch-LCD-1.85 (ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM)
- **Display**: ST77916, 360×360, QSPI @ 80 MHz (circular visible area)
- **Touch**: CST816 (I²C 0x15)
- **IMU**: QMI8658 (I²C 0x6B)
- **IO expander**: TCA9554PWR, **RTC**: PCF85063
- **Power**: Li-Po + USB-C, PWR button (long press = power off)

---

## Quick start (prebuilt firmware)

1. Download **[`attitude-indicator-merged.bin`](https://github.com/Hanice404/ESP32-AttitudeIndicator/releases/latest)** (1.27 MB) from the Releases page — or take it from `firmware/` in this repo.
2. Flash it at offset **0x0** (see [FLASHING.md](FLASHING.md) for all methods).
3. Place the device in the attitude you consider "level", then **short press BOOT**
   (or simply power-cycle — the firmware zeroes itself at boot).

```bash
pip install esptool
esptool.py --chip esp32s3 -p COM6 write_flash 0x0 attitude-indicator-merged.bin
```

---

## Controls

| Input | Action |
| --- | --- |
| **Short press BOOT** (40 ms – 1 s) | Cage the horizon: current attitude becomes level |
| **Long press BOOT** (~2 s) | Rotate display 0° ↔ 180° |
| **Drag up / down** | Brightness +10 % / −10 % (5–100 %), yellow bar feedback |
| **Double tap** | Reset peak values |
| **Long press PWR** | Power off |
| Serial `info` / `show` / `cal` / `save` / `reboot` … | Debug and tuning commands (see docs) |

---

## Build from source

Requirements: **arduino-cli** (or Arduino IDE 2), **ESP32 core 3.1.1**, **LVGL 8.3.10**.

```bash
arduino-cli compile \
  --fqbn esp32:esp32:waveshare_esp32_s3_touch_lcd_185:PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600,DebugLevel=error \
  --libraries ./libs \
  src/AttitudeBall
```

See `src/README.md` for the full development notes: IMU axis mapping, NVS layout,
every pitfall that was hit during development (31 entries), and all serial commands.

---

## Customising

| What | Where |
| --- | --- |
| Boot logo | `src/tools/make-logo.ps1 my-logo.svg` (SVG / PNG / JPG → `Boot_Logo.h`) |
| Logo hold / fade time | `src/AttitudeBall/Config.h` → `SPLASH_HOLD_MS`, `SPLASH_FADE_MS` |
| Brightness limits / step | `Config.h` → `BRIGHT_MIN/MAX/STEP` |
| Sky & ground colours | `src/AttitudeBall/UI_Horizon.cpp` → `skyR0…`, `gndR0…` |
| Horizon line thickness, ladder spacing | `UI_Horizon.cpp` (`FOCAL`, `LADDER_L`) |

---

## 中文说明

**这是一个给 Waveshare ESP32-S3-Touch-LCD-1.85（1.85 寸圆形屏）写的姿态仪（人工地平仪）固件。**

- 蓝天/棕地渐变 + 地平线 + 俯仰刻度梯（±10/20/30，带数字）+ 滚转刻度弧与红色指针 + 固定飞机符号
- **短按 BOOT = 对齐地平线**（把当前姿态记为水平）；**每次开机也会自动归零**
- 开机显示 Logo，0.8 秒渐隐进入仪表画面
- **上下拖动屏幕调亮度**（每 10% 一档，底部黄色亮度条提示），不自动降亮度、不自动熄屏
- 支持 WiFi OTA、电池监测、崩溃保护

烧录：用 `firmware/attitude-indicator-merged.bin`，偏移 **0x0** 一次写入即可，详见 [FLASHING.md](FLASHING.md)。

编译源码：arduino-cli + ESP32 core 3.1.1 + LVGL 8.3.10，
详细踩坑记录和全部串口命令见 `src/README.md`。

---

## Credits

- Display / touch / IMU drivers are derived from the **Waveshare ESP32-S3-Touch-LCD-1.85**
  demo code (kept under their original notices).
- UI rendering and attitude maths written from scratch for this project.
- Fonts: LVGL built-in.

## License

MIT — see [LICENSE](LICENSE).
