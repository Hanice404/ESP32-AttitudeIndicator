// AttitudeBall.ino —— 姿态水平仪 / 3D 姿态球
//
// 目标板：Waveshare ESP32-S3-Touch-LCD-1.85（ESP32-S3R8 / 16MB Flash / 8MB PSRAM）
// 屏幕：ST77916 360×360 QSPI    触摸：CST816（自带手势）    IMU：QMI8658 @0x6B
//
// 功能：
//   1. 水平仪：气泡 + 数字角度 + pitch/roll
//   2. 3D 姿态球：线框球 + 重力箭头
//   3. 长按屏幕校准零位（存 NVS，掉电不丢）
//   4. 左右滑动切换模式
//   5. 电池电压/百分比显示，PWR 键长按关机
//   6. 闲置降亮度 → 熄屏，触摸唤醒
//   7. WiFi OTA 无线升级
//   8. 角速度与峰值记录显示

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_lcd_panel_ops.h>
#include <esp_system.h>

#include "App_Attitude.h"
#include "App_Status.h"
#include "BAT_Driver.h"
#include "Boot_Splash.h"
#include "Config.h"
#include "Display_ST77916.h"
#include "Gyro_QMI8658.h"
#include "I2C_Driver.h"
#include "LVGL_Driver.h"
#include "PWR_Key.h"
#include "Store.h"
#include "TCA9554PWR.h"
#include "Touch_CST816.h"
#include "UI_Horizon.h"
#include "WifiOta.h"

// ---------------- 全局对象 ----------------
static app::Store g_store;
static bool g_hadStore = false;   // 本次启动是否成功加载了已存配置（诊断用）
static att::Attitude g_att;
static net::WifiOta g_wifi;
static app::RuntimeStatus g_status;
static ui::HorizonView g_horizon;

// ---------------- 运行时状态 ----------------
static app::Mode g_mode = app::Mode::Horizon;
static bool g_horizonReady = false;

static uint32_t tImu = 0, tUi = 0, tTouch = 0, tBat = 0, tPwr = 0, tDbg = 0;
static uint32_t lastImuUs = 0;
static uint32_t tLastTouch = 0;   // 最后一次有效交互
static bool g_dimmed = false;
static bool g_screenOff = false;
static float g_batteryV = 0.0f;
static int g_batteryPct = -1;

// IMU 轴向映射（运行时可调，见 runCmd）
static uint8_t g_rot = 4;     // 0..3 = 绕Z转+Z取反；4 = XY互换+Z取反（本板实测正确）
static bool g_invz = true;    // Z 取反（仅 rot=0..3 生效）
static uint8_t g_ballFlip = 0;  // 姿态球方向微调：bit0=翻 pitch, bit1=翻 roll

// 屏幕方向（BOOT 键长按循环切换）
static uint8_t g_screenRot = 0;
static bool g_sdir = false;      // 屏幕旋转方向：false=顺时针(+90°/次) true=逆时针
static uint8_t g_bfix = 0;       // 气泡/姿态方向最终微调（0..3，每步 90°）
static bool g_yflip = false;     // Y 轴单独取反（X 对、上下反时用）
static uint16_t g_swipeMinPx = 70;  // 左右滑动判定阈值（像素，越大越不灵敏）
static uint32_t tBtn = 0;        // 按键轮询计时
static uint32_t tBootDown = 0;   // BOOT 键按下起始时刻
static bool bootHandled = false; // 本次长按是否已触发

// 前置声明（runCmd / applyScreenRotation 会用到，定义在下方）
static uint8_t contentRotSteps();
static void rotateVec90(float& x, float& y, uint8_t k);
static void applyCalibration();
static bool doCalibrate();
static bool captureHorizonRef();
static void applyHorizonRef();

// ---------------- 屏幕方向 ----------------
// Display_ST77916.cpp 里定义了全局 panel_handle（非 static），可直接操作
extern esp_lcd_panel_handle_t panel_handle;

// 应用屏幕方向：0=0° 1=90° 2=180° 3=270°
// 通过 MADCTL 的 MV(bit5) / MX(bit6) / MY(bit7) 组合实现，驱动已实现 swap_xy/mirror
static void applyScreenRotation(uint8_t r) {
  g_screenRot = r & 3;

  if (panel_handle) {
    bool swap = false, mx = false, my = false;
    switch (g_screenRot) {
      case 1: swap = true;  mx = true;  my = false; break;  // 90°
      case 2: swap = false; mx = true;  my = true;  break;  // 180°
      case 3: swap = true;  mx = false; my = true;  break;  // 270°
      default: break;                                        // 0°
    }
    esp_lcd_panel_swap_xy(panel_handle, swap);
    esp_lcd_panel_mirror(panel_handle, mx, my);
  }

  // 让 LVGL 整屏重绘，否则旧画面残留
  if (lv_scr_act()) lv_obj_invalidate(lv_scr_act());
  DBG("[lcd] screen rotation -> %u deg\n", (unsigned)(g_screenRot * 90));

  // 屏幕方向变了，校准零位要按新方向重新换算
  applyCalibration();
  applyHorizonRef();  // 地平线基准同样要按新方向换算
}

// 触摸坐标随屏幕方向变换（CST816 上报的是面板物理坐标）
// 必须与加速度补偿使用【同一个】旋转量 contentRotSteps()，否则触摸和重力会各转各的。
static void transformTouch(uint16_t& x, uint16_t& y) {
  const uint16_t S = SCREEN_W;  // 360，正方形屏
  const uint16_t last = (uint16_t)(S - 1);
  uint16_t nx = x, ny = y;
  switch (contentRotSteps()) {
    case 1: nx = (uint16_t)(last - y); ny = x; break;
    case 2: nx = (uint16_t)(last - x); ny = (uint16_t)(last - y); break;
    case 3: nx = y;                    ny = (uint16_t)(last - x); break;
    default: return;
  }
  x = nx;
  y = ny;
}

// ---------------- BOOT 键长按：切换屏幕方向 ----------------
// ⚠️ 默认【关闭】：拿板子/插线时极易误触，误触会把屏幕转 180°，
//    画面一颠倒，看起来就像"气泡上下左右全反"（为此白折腾过两轮）。
//    需要时串口发 `bootrot 1` 启用。
// ⚠️ 上电保护：必须先看到一次"松开"(高电平)，长按才开始计数，
//    避免上电瞬间引脚状态被误判成按下。
static bool g_bootArm = false;  // 是否已见过松开状态

static void handleBootKey() {
  const bool down = (digitalRead(BTN_BOOT_PIN) == LOW);

  if (!down) {
    g_bootArm = true;  // 见到松开 → 武装
    const uint32_t dur = (tBootDown != 0) ? (millis() - tBootDown) : 0;
    const bool wasHandled = bootHandled;  // 长按动作已执行过？
    if (tBootDown != 0) {
      DBG("[btn] boot up (%lu ms)\n", (unsigned long)dur);
    }
    tBootDown = 0;
    bootHandled = false;

    // ---- 短按 = 校准零位（长按动作没触发、且时长在短按窗口内）----
    if (!wasHandled && dur >= BOOT_CAL_MIN_MS && dur < BOOT_CAL_MAX_MS) {
      doCalibrate();
      tLastTouch = millis();
    }
    return;
  }
  if (!g_bootArm) return;  // 上电后还没松开过，不认
  if (tBootDown == 0) {
    tBootDown = millis();
    DBG("[btn] boot down\n");
    return;
  }
  if (!bootHandled && millis() - tBootDown >= BOOT_LONG_PRESS_MS) {
    bootHandled = true;  // 一次长按只触发一次，松开才能再触发

    if (!g_store.settings().bootRotEnable) {
      DBG("[btn] boot long press ignored (bootrot disabled; send 'bootrot 1' to enable)\n");
      return;
    }

    // 熄屏/暗屏状态下先唤醒，否则转了屏也看不见
    if (g_screenOff || g_dimmed) {
      g_screenOff = false;
      g_dimmed = false;
      applyBrightness(g_store.settings().brightness);
    }

    // BOOT 长按只在 0°(USB 口朝下/正常竖拿) ↔ 180°(USB 口朝上/倒装) 之间切换。
    // 本项目只用这两个方向；90°/270° 需要配合横拿，容易让人误以为"方向算错了"，
    // 所以不再放进按键循环（仍可用串口 `srot 1` / `srot 3` 设置）。
    const uint8_t next = (g_screenRot == 0) ? 2 : 0;
    applyScreenRotation(next);
    g_store.settings().screenRot = next;
    g_store.save();
    tLastTouch = millis();  // 视为一次交互，重置息屏计时
  }
}

// ---------------- 工具 ----------------
static void applyBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  LCD_Backlight = pct;
  Set_Backlight(pct);
}

// 加减亮度（上下拖动时调用），立即生效并存盘
static void adjustBrightness(int delta) {
  int b = (int)g_store.settings().brightness + delta;
  if (b < BRIGHT_MIN) b = BRIGHT_MIN;
  if (b > BRIGHT_MAX) b = BRIGHT_MAX;
  if (b == (int)g_store.settings().brightness) return;
  g_store.settings().brightness = (uint8_t)b;
  g_store.save();
  g_dimmed = false;
  g_screenOff = false;
  applyBrightness((uint8_t)b);
  if (g_horizonReady) g_horizon.showOverlay(b);
  DBG("[ui] brightness=%d%%\n", b);
}

static int voltsToPct(float v) {
  if (v <= 0.0f) return -1;

  // 单节锂电放电曲线（分段线性插值，比纯线性贴近实际）
  static const struct { float v; uint8_t pct; } kCurve[] = {
      {4.20f, 100}, {4.10f, 90}, {4.00f, 78}, {3.92f, 65}, {3.85f, 50},
      {3.75f, 30},  {3.65f, 15}, {3.50f, 5},  {3.40f, 0},
  };
  const int n = sizeof(kCurve) / sizeof(kCurve[0]);

  if (v >= kCurve[0].v) return 100;
  if (v <= kCurve[n - 1].v) return 0;

  for (int i = 0; i < n - 1; i++) {
    if (v <= kCurve[i].v && v >= kCurve[i + 1].v) {
      const float span = kCurve[i].v - kCurve[i + 1].v;
      const float t = (v - kCurve[i + 1].v) / span;
      const int p = kCurve[i + 1].pct + (int)lroundf(t * (kCurve[i].pct - kCurve[i + 1].pct));
      return p < 0 ? 0 : (p > 100 ? 100 : p);
    }
  }
  return -1;
}

#if BOOT_I2C_SCAN
static void scanBus(TwoWire& bus, const char* name) {
  DBG("[i2c] scan %s ...\n", name);
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    bus.beginTransmission(addr);
    if (bus.endTransmission() == 0) {
      DBG("[i2c]   %s 0x%02X\n", name, addr);
      found++;
    }
    delay(2);
  }
  DBG("[i2c] %s: %d device(s)\n", name, found);
}
#endif

// ---------------- 界面（只保留姿态仪）----------------
static void updateModeVisibility() {
  if (g_horizonReady) g_horizon.setVisible(true);
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(NULL);
}

static const char* modeName(app::Mode m) {
  (void)m;
  return "horizon";
}

static void showMode(app::Mode m) {
  if (m == g_mode) return;
  g_mode = m;
  g_store.settings().mode = (uint8_t)m;
  g_store.save();
  updateModeVisibility();
  DBG("[ui] mode = %s\n", modeName(m));
}

// ---------------- 校准 ----------------
// ⚠️ 坐标系约定（这是之前一个隐蔽 bug 的根源）：
//   IMU 数据在喂给姿态解算前，已经按屏幕方向旋转到了【内容坐标系】。
//   如果把校准零位也按内容坐标系存盘，一旦屏幕方向变了，零位就整体转错
//   （现象：换个方向后气泡上下左右全反）。
//   所以：NVS 里一律存【物理板体坐标系】的零位；使用时再按当前屏幕方向换算。
static void applyCalibration() {
  const app::Settings& s = g_store.settings();
  if (!s.calibValid) {
    g_att.clearCalibration();
    return;
  }
  float x = s.gx, y = s.gy;
  rotateVec90(x, y, contentRotSteps());  // 物理 → 内容
  if (g_yflip) y = -y;                    // 与 handleImu 的 Y 取反保持一致
  g_att.setCalibGravity(att::v3(x, y, s.gz));
  DBG("[calib] applied phys=(%.3f,%.3f,%.3f) -> content=(%.3f,%.3f,%.3f)\n", s.gx, s.gy, s.gz, x,
      y, s.gz);
}

// 把 NVS 里存的"物理坐标系"基准换算到当前内容坐标系，交给姿态仪
static void applyHorizonRef() {
  const app::Settings& s = g_store.settings();
  if (!s.hCalValid) {
    g_horizon.setReference(false, 0.0f, 1.0f, 0.0f);
    return;
  }
  float x = s.hgx, y = s.hgy;
  rotateVec90(x, y, contentRotSteps());  // 物理 → 内容（与 handleImu 一致）
  g_horizon.setReference(true, x, y, s.hgz);
  DBG("[href] applied phys=(%.3f,%.3f,%.3f) -> content=(%.3f,%.3f,%.3f)\n", s.hgx, s.hgy, s.hgz, x,
      y, s.hgz);
}

static bool doCalibrate() {
  // 0) 安全检查：刚 calclear 过、或 IMU 还没采到有效数据时，
  //    filtered 可能接近零向量。若直接归一化会存成 (0,0,0)，
  //    基准正交基就退化了（现象：tilt 乱跳、方向随机）。实测踩过。
  {
    const att::State& st0 = g_att.state();
    const float len0 = sqrtf(st0.filtered.ax * st0.filtered.ax +
                             st0.filtered.ay * st0.filtered.ay +
                             st0.filtered.az * st0.filtered.az);
    if (len0 < 0.5f) {
      DBG("[calib] rejected: gravity magnitude %.2f too small, move/settle first\n", len0);
      return false;
    }
  }

  // 1) 内容坐标系下立即锁零（当前画面立刻归零）
  g_att.calibrateNow();

  // 2) 把零位换算回物理坐标系再存盘，保证换屏幕方向后依然有效
  const att::State& st = g_att.state();
  float x = st.filtered.ax, y = st.filtered.ay, z = st.filtered.az;
  const float len = sqrtf(x * x + y * y + z * z);
  if (len > 0.1f) {
    x /= len;
    y /= len;
    z /= len;
  }
  rotateVec90(x, y, (uint8_t)((4 - contentRotSteps()) & 3));  // 内容 → 物理
  // 注意：x,y 是"内容坐标系"的测量值，需按与 handleImu 完全相反的次序逐层撤销。
  // handleImu 顺序：crot → yflip，因此逆序为：撤销 crot → 撤销 yflip。
  if (g_yflip) y = -y;

  g_store.saveCalib(x, y, z);
  DBG("[calib] saved phys=(%.3f, %.3f, %.3f)\n", x, y, z);

  // 3) 【姿态仪地平线基准】短按 BOOT 的核心作用：把当前姿态对齐成"地平线居中水平"
  return captureHorizonRef();
}

// 把"当前姿态"记为地平线基准（开机自动归零与短按 BOOT 共用）
static bool captureHorizonRef() {
  const att::State& st = g_att.state();
  float hx = st.filtered.ax, hy = st.filtered.ay, hz = st.filtered.az;
  const float hl = sqrtf(hx * hx + hy * hy + hz * hz);
  if (hl < 0.5f) {
    DBG("[href] rejected: gravity too small (%.2f)\n", hl);
    return false;
  }
  hx /= hl;
  hy /= hl;
  hz /= hl;
  rotateVec90(hx, hy, (uint8_t)((4 - contentRotSteps()) & 3));  // 内容 → 物理
  app::Settings& s = g_store.settings();
  s.hCalValid = 1;
  s.hgx = hx;
  s.hgy = hy;
  s.hgz = hz;
  g_store.save();
  applyHorizonRef();
  DBG("[href] set phys=(%.3f,%.3f,%.3f)  horizon aligned\n", hx, hy, hz);
  return true;
}

// ---------------- 串口命令行（实测调参用，不影响正常使用）----------------
// 可用命令：
//   rot N      轴向映射模式：
//                0/1/2/3 = 绕 Z 轴转 0/90/180/270 度（再叠加 invz）
//                4       = XY 互换 + Z 取反（本板实测正确的组合，行列式 +1）
//   invz 0|1   Z 轴取反开关（仅对 rot=0..3 有效）
//   bflip N    姿态球方向微调：0=正常 1=翻pitch 2=翻roll 3=都翻
//   srot N     屏幕方向 0/1/2/3 = 0°/90°/180°/270°（与 BOOT 键长按等价，会立即保存）
//   save       把当前 rot/invz/bflip 存进 NVS
//   show       打印原始与映射后的加速度
//   cal        校准零位（同长按屏幕）
//   bat        打印电池 ADC 原始毫伏与换算电压
//   peak       复位峰值
//   mode l|b   切换水平仪 / 姿态球
static char g_cmd[48];
static uint8_t g_cmdLen = 0;

static void printShow() {
  const att::State& st = g_att.state();
  DBG("[show] raw     = (%.3f, %.3f, %.3f) g\n", Accel.x, Accel.y, Accel.z);
  DBG("[show] mapped  = (%.3f, %.3f, %.3f) g\n", st.filtered.ax, st.filtered.ay, st.filtered.az);
  DBG("[show] proj    = axc=%.3f ayc=%.3f azc=%.3f\n", st.axc, st.ayc, st.azc);
  DBG("[show] tilt=%.1f pitch=%.1f roll=%.1f rate=%.0f\n", st.tilt, st.pitch, st.roll, st.rate);
  DBG("[show] rot=%u invz=%u yflip=%u xflip=%u hpf=%u hrf=%u srot=%u sdir=%u crot=%u calib=%d\n",
      g_rot, (unsigned)g_invz, (unsigned)g_yflip, (unsigned)g_store.settings().outXFlip,
      (unsigned)g_store.settings().horizonPitchFlip, (unsigned)g_store.settings().horizonRollFlip,
      g_screenRot, (unsigned)g_sdir, contentRotSteps(), g_att.calibrated());
}

static void runCmd(char* cmd) {
  if (!strncmp(cmd, "rot ", 4)) {
    int v = atoi(cmd + 4);
    if (v < 0 || v > 5) v = 0;   // 0..3 常规旋转，4 = XY互换+Z取反，5 = 4 再绕 Z 转 180°
    g_rot = (uint8_t)v;
    DBG("[cmd] rot=%u (not saved yet, type save)\n", g_rot);
  } else if (!strcmp(cmd, "rot")) {
    DBG("[cmd] rot=%u invz=%u\n", g_rot, (unsigned)g_invz);
  } else if (!strncmp(cmd, "invz ", 5)) {
    g_invz = atoi(cmd + 5) != 0;
    DBG("[cmd] invz=%u\n", (unsigned)g_invz);
  } else if (!strcmp(cmd, "save")) {
    app::Settings& s = g_store.settings();
    s.imuRot = g_rot;
    s.imuInvZ = g_invz ? 1 : 0;
    g_store.save();
    DBG("[cmd] saved rot=%u invz=%u\n", g_rot, (unsigned)g_invz);
  } else if (!strncmp(cmd, "xflip ", 6)) {
    const bool en = atoi(cmd + 6) != 0;
    g_store.settings().outXFlip = en ? 1 : 0;
    g_store.save();
    g_att.setOutputXFlip(en);
    DBG("[cmd] outputXflip=%u saved (mirror axc only)\n", (unsigned)en);
  } else if (!strncmp(cmd, "hrf ", 4)) {
    const bool en = atoi(cmd + 4) != 0;
    g_store.settings().horizonRollFlip = en ? 1 : 0;
    g_store.save();
    g_horizon.setFlipRoll(en);
    DBG("[cmd] horizon rollFlip=%u saved\n", (unsigned)en);
  } else if (!strncmp(cmd, "hpf ", 4)) {
    const bool en = atoi(cmd + 4) != 0;
    g_store.settings().horizonPitchFlip = en ? 1 : 0;
    g_store.save();
    g_horizon.setFlipPitch(en);
    DBG("[cmd] horizon pitchFlip=%u saved\n", (unsigned)en);
  } else if (!strncmp(cmd, "hflip ", 6)) {
    const bool en = atoi(cmd + 6) != 0;
    g_store.settings().horizonFlip = en ? 1 : 0;
    g_store.save();
    g_horizon.setFlipXY(en);
    DBG("[cmd] horizon flipXY=%u saved\n", (unsigned)en);
  } else if (!strncmp(cmd, "ytau ", 5)) {
    int v = atoi(cmd + 5);
    if (v < 0) v = 0;
    if (v > 60000) v = 60000;
    g_att.setYawTau((uint16_t)v);
    DBG("[cmd] yaw tau=%d ms (0=disable yaw, larger=slower recenter)\n", v);
  } else if (!strcmp(cmd, "yawreset")) {
    g_att.resetYaw();
    DBG("[cmd] yaw reset\n");
  } else if (!strncmp(cmd, "tau ", 4)) {
    int v = atoi(cmd + 4);
    if (v < 20) v = 20;
    if (v > 1000) v = 1000;
    g_att.setFilterTau((float)v);
    g_store.settings().filterTauMs = (uint16_t)v;
    g_store.save();
    DBG("[cmd] filter tau=%d ms saved (larger = steadier, more lag)\n", v);
  } else if (!strncmp(cmd, "swipe ", 6)) {
    int v = atoi(cmd + 6);
    if (v < 20) v = 20;
    if (v > 250) v = 250;
    g_swipeMinPx = (uint16_t)v;
    g_store.settings().swipeMinPx = g_swipeMinPx;
    g_store.save();
    DBG("[cmd] swipeMinPx=%u saved (larger = less sensitive)\n", (unsigned)g_swipeMinPx);
  } else if (!strncmp(cmd, "bootrot ", 8)) {
    const bool en = atoi(cmd + 8) != 0;
    g_store.settings().bootRotEnable = en ? 1 : 0;
    g_store.save();
    DBG("[cmd] bootRotEnable=%u saved (BOOT long-press rotation %s)\n", (unsigned)en,
        en ? "ENABLED" : "DISABLED");
  } else if (!strncmp(cmd, "yflip ", 6)) {
    g_yflip = atoi(cmd + 6) != 0;
    g_store.settings().yflip = g_yflip ? 1 : 0;
    g_store.save();
    applyCalibration();
    DBG("[cmd] yflip=%u saved\n", (unsigned)g_yflip);
  } else if (!strncmp(cmd, "bfix ", 5)) {
    int v = atoi(cmd + 5);
    if (v < 0 || v > 3) v = 0;
    g_bfix = (uint8_t)v;
    g_store.settings().bfix = g_bfix;
    g_store.save();
    g_att.setOutputRot(g_bfix);  // 在投影之后旋转输出，不动校准零点
    DBG("[cmd] bfix=%u saved (bubble rotated %u deg)\n", g_bfix, (unsigned)(g_bfix * 90));
  } else if (!strncmp(cmd, "sdir ", 5)) {
    g_sdir = atoi(cmd + 5) != 0;
    g_store.settings().sdir = g_sdir ? 1 : 0;
    g_store.save();
    applyCalibration();  // 内容坐标系变了，校准零位要重算
    DBG("[cmd] sdir=%u (contentRot=%u steps) saved\n", (unsigned)g_sdir, contentRotSteps());
  } else if (!strcmp(cmd, "btn")) {
    // 诊断：打印 BOOT / PWR 键当前电平（0=按下）
    DBG("[btn] GPIO%u(BOOT)=%d  GPIO%u(PWR)=%d  srot=%u\n", BTN_BOOT_PIN,
        digitalRead(BTN_BOOT_PIN), PWR_KEY_Input_PIN, digitalRead(PWR_KEY_Input_PIN),
        g_screenRot);
  } else if (!strncmp(cmd, "srot ", 5)) {
    int v = atoi(cmd + 5);
    if (v < 0 || v > 3) v = 0;
    applyScreenRotation((uint8_t)v);
    g_store.settings().screenRot = g_screenRot;
    g_store.save();
    DBG("[cmd] screenRot=%u saved\n", g_screenRot);
  } else if (!strcmp(cmd, "bat")) {
    const int mv = analogReadMilliVolts(BAT_ADC_PIN);
    DBG("[bat] adc=%d mV  volts=%.3f V  pct=%d%%\n", mv, g_batteryV, g_batteryPct);
  } else if (!strcmp(cmd, "show")) {
    printShow();
  } else if (!strcmp(cmd, "cal")) {
    if (doCalibrate()) DBG("[cmd] calibrated (horizon aligned)\n");
  } else if (!strcmp(cmd, "calclear")) {
    // 清除校准（旧数据可能来自不同坐标系，换版本后建议清一次）
    g_store.settings().calibValid = false;
    g_store.settings().gx = 0.0f;
    g_store.settings().gy = 0.0f;
    g_store.settings().gz = 1.0f;
    g_store.save();
    g_att.clearCalibration();
    DBG("[cmd] calibration cleared\n");
  } else if (!strcmp(cmd, "info")) {
    const app::Settings& st = g_store.settings();
    DBG("[info] hadStore=%d calibValid=%d g0=(%.3f,%.3f,%.3f)\n", (int)g_hadStore,
        (int)st.calibValid, st.gx, st.gy, st.gz);
    DBG("[info] srot=%u sdir=%u rot=%u invz=%u yflip=%u bfix=%u bflip=%u mode=%u bright=%u\n",
        st.screenRot, (unsigned)st.sdir, st.imuRot, (unsigned)st.imuInvZ, (unsigned)st.yflip, st.bfix,
        st.ballFlip, st.mode, st.brightness);
    DBG("[info] runtime: srot=%u sdir=%u crot=%u mode=%u\n", g_screenRot, (unsigned)g_sdir,
        contentRotSteps(), (uint8_t)g_mode);
  } else if (!strcmp(cmd, "reboot")) {
    DBG("[cmd] rebooting...\n");
    delay(50);
    ESP.restart();
  } else if (!strcmp(cmd, "shot")) {
    // 抓屏：把帧缓冲原样从串口输出（发布文档截图用）
    if (g_horizonReady) g_horizon.dumpToSerial();
  } else if (!strcmp(cmd, "peak")) {
    g_att.resetPeaks();
    DBG("[cmd] peaks reset\n");
  } else if (!strncmp(cmd, "mode ", 5)) {
    // 现在只有姿态仪一个界面
    showMode(app::Mode::Horizon);
  } else if (strlen(cmd)) {
    DBG("[cmd] unknown: %s\n", cmd);
  }
}

static void handleSerialCmd() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (g_cmdLen) {
        g_cmd[g_cmdLen] = 0;
        runCmd(g_cmd);
        g_cmdLen = 0;
      }
    } else if (g_cmdLen < sizeof(g_cmd) - 1) {
      g_cmd[g_cmdLen++] = c;
    }
  }
}

// 手势方向随屏幕方向变换：CST816 的手势是按面板物理坐标算的，
// 屏幕一转，"物理上往右滑"就不再等于"画面里往右滑"。同样使用 contentRotSteps()。
static GESTURE transformGesture(GESTURE g) {
  switch (contentRotSteps()) {
    case 1:  // dx'=-dy, dy'=dx
      if (g == SWIPE_RIGHT) return SWIPE_DOWN;
      if (g == SWIPE_LEFT) return SWIPE_UP;
      if (g == SWIPE_UP) return SWIPE_RIGHT;
      if (g == SWIPE_DOWN) return SWIPE_LEFT;
      return g;
    case 2:  // dx'=-dx, dy'=-dy
      if (g == SWIPE_RIGHT) return SWIPE_LEFT;
      if (g == SWIPE_LEFT) return SWIPE_RIGHT;
      if (g == SWIPE_UP) return SWIPE_DOWN;
      if (g == SWIPE_DOWN) return SWIPE_UP;
      return g;
    case 3:  // dx'=dy, dy'=-dx
      if (g == SWIPE_RIGHT) return SWIPE_UP;
      if (g == SWIPE_LEFT) return SWIPE_DOWN;
      if (g == SWIPE_UP) return SWIPE_LEFT;
      if (g == SWIPE_DOWN) return SWIPE_RIGHT;
      return g;
    default:
      return g;
  }
}

// 把 (x,y) 绕原点逆时针旋转 k*90°（k=0..3）
static void rotateVec90(float& x, float& y, uint8_t k) {
  float nx = x, ny = y;
  switch (k & 3) {
    case 1: nx = -y; ny = x; break;
    case 2: nx = -x; ny = -y; break;
    case 3: nx = y;  ny = -x; break;
    default: return;
  }
  x = nx;
  y = ny;
}

// 当前屏幕方向对应的"画面坐标系旋转量"
static uint8_t contentRotSteps() {
  const int r = (int)g_screenRot;
  return (uint8_t)((g_sdir ? (4 - r) : r) & 3);
}

// ---------------- 触摸处理 ----------------
// ⚠️ 关于 CST816 手势寄存器（实测结论，务必保留这段注释）：
//   · 芯片会在【手指按住期间】持续上报同一个手势值；
//   · 手指离开后【寄存器不清零】，一直锁存上一次的值（实测空闲时仍读到 3）；
//   · 于是"读手势寄存器"的方案必然有两个毛病：
//       ① 按住时重复触发 → 画面高速交替；
//       ② 下次按下瞬间读到上次的残值 → 假触发，而真正的滑动被"一次触摸只处理一个手势"挡掉。
//   结论：【左右滑动改用触摸坐标位移判定】，不再依赖 gesture 寄存器；
//   寄存器只用来识别"双击"（该事件是瞬时的，没有锁存问题）。
static GESTURE g_handledGest = NONE;
static uint32_t tGestHandled = 0;
static bool s_prevTouching = false;
static int16_t s_startX = 0, s_startY = 0, s_lastX = 0, s_lastY = 0;
static bool s_haveStart = false;

// 滑动判定阈值（内容坐标系下的像素）
// 竖向调光单独用更低的阈值：手感更灵敏，轻轻一划就能调（原来和横向共用 70 px 太长）
static constexpr int SWIPE_RATIO_NUM = 3;  // |dx| 需大于 |dy|*3/2，避免斜划误判
static constexpr int SWIPE_RATIO_DEN = 2;
static constexpr int SWIPE_VERT_PX = 18;   // 竖向拖动判定阈值（像素，越小越灵敏）

static void handleTouch() {
  Touch_Read_Data();  // 读一次寄存器（含芯片手势，仅用于双击）

  const bool touching = (touch_data.points != 0);
  if (touching) {
    transformTouch(touch_data.x, touch_data.y);  // 坐标随屏幕方向变换
    s_lastX = touch_data.x;
    s_lastY = touch_data.y;
  }
  const GESTURE g = transformGesture(touch_data.gesture);

  const bool pressed = (touching && !s_prevTouching);
  const bool released = (!touching && s_prevTouching);
  s_prevTouching = touching;

  // ---- 有交互就先处理唤醒/息屏计时 ----
  if (touching || pressed || released) {
    tLastTouch = millis();
    if (g_screenOff || g_dimmed) {
      g_screenOff = false;
      g_dimmed = false;
      applyBrightness(g_store.settings().brightness);
      s_haveStart = false;
      return;
    }
  }

  // ---- 按下：记录起点 ----
  if (pressed) {
    s_startX = touch_data.x;
    s_startY = touch_data.y;
    s_haveStart = true;
    return;
  }

  // ---- 竖向拖动：连续调光（跟手；不必等抬手指）----
  // 每拖过 SWIPE_VERT_PX 像素就调一档，然后以当前位置为新基准继续累积，
  // 所以一口气拖到底可以连续调好几档。
  if (touching && s_haveStart) {
    const int dx = (int)s_lastX - (int)s_startX;
    const int dy = (int)s_lastY - (int)s_startY;
    const int adx = abs(dx), ady = abs(dy);
    if (ady >= SWIPE_VERT_PX && ady * SWIPE_RATIO_DEN > adx * SWIPE_RATIO_NUM) {
      adjustBrightness((dy < 0) ? BRIGHT_STEP : -BRIGHT_STEP);  // 上滑加亮、下滑减亮
      s_startX = s_lastX;
      s_startY = s_lastY;
    }
  }

  // ---- 抬起：结束拖动 ----
  if (released) {
    s_haveStart = false;
  }

  // ---- 双击：复位峰值（仅手指按在屏上时接受，避免寄存器残值误触发）----
  if (touching && g == DOUBLE_CLICK) {
    if (g != g_handledGest && millis() - tGestHandled >= GESTURE_COOLDOWN_MS) {
      g_handledGest = g;
      tGestHandled = millis();
      g_att.resetPeaks();
      DBG("[ui] peaks reset\n");
    }
    return;
  }
  if (g == NONE) g_handledGest = NONE;  // 寄存器回到 0 时复位去抖标记
}

// ---------------- 闲置与背光 ----------------
// IDLE_DIM_MS / IDLE_OFF_MS 为 0 表示关闭对应功能（当前配置：全关，不自动息屏）
static void handleIdle() {
  const uint32_t idle = millis() - tLastTouch;

  if (IDLE_OFF_MS > 0 && !g_screenOff && idle > IDLE_OFF_MS) {
    g_screenOff = true;
    g_dimmed = false;
    applyBrightness(0);
    DBG("[pwr] screen off (idle %lu ms)\n", (unsigned long)idle);
  } else if (IDLE_DIM_MS > 0 && !g_dimmed && !g_screenOff && idle > IDLE_DIM_MS) {
    g_dimmed = true;
    applyBrightness(BRIGHT_DIM);
    DBG("[pwr] dim\n");
  }
}

// ---------------- IMU ----------------
static void handleImu() {
  getAccelerometer();
  getGyroscope();

  // ---- 轴向映射 ----
  float ax = Accel.x, ay = Accel.y, az = Accel.z;
  float gx = Gyro.x, gy = Gyro.y, gz = Gyro.z;

  if (g_rot == 5) {
    // rot=5 = 在 rot=4 基础上再绕 Z 转 180°。
    // 起因：实测"抬右侧"时 raw ay 为负，而 rot=4 直接取 ax=raw_ay 得到负值，
    // 于是气泡朝反方向走。加 180° 后符号正确（az 仍取反，保证平放时 az=+1）。
    const float tx = ax, tgx = gx;
    ax = -ay;
    gx = -gy;
    ay = -tx;
    gy = -tgx;
    az = -az;
    gz = -gz;
  } else if (g_rot == 4) {
    // 坐标系映射：屏幕右 = +Y_imu，屏幕上 = +X_imu，屏幕外法线 = -Z_imu
    // 行列式 = +1（真旋转，姿态球不会镜像）
    const float tx = ax, tgx = gx;
    ax = ay;
    gx = gy;
    ay = tx;
    gy = tgx;
    az = -az;
    gz = -gz;
  } else {
    switch (g_rot & 3) {
      case 1: {  // 90°
        const float tx = ax, tgx = gx;
        ax = -ay;
        gx = -gy;
        ay = tx;
        gy = tgx;
        break;
      }
      case 2:  // 180°
        ax = -ax;
        ay = -ay;
        gx = -gx;
        gy = -gy;
        break;
      case 3: {  // 270°
        const float tx = ax, tgx = gx;
        ax = ay;
        gx = gy;
        ay = -tx;
        gy = -tgx;
        break;
      }
      default:
        break;
    }
    if (g_invz) {
      az = -az;
      gz = -gz;
    }
  }

  // ---- 屏幕方向补偿：物理坐标系 → 画面坐标系 ----
  // 屏幕旋转后，"画面里的右/上"已不等于物理的右/上。不补偿的话，
  // 气泡和姿态球的方向都会与眼睛看到的画面对不上（这就是旋转后方向反了的原因）。
  const uint8_t k = contentRotSteps();
  if (k) {
    rotateVec90(ax, ay, k);
    rotateVec90(gx, gy, k);
  }

  // ---- 最终方向微调（bfix）不在这里做 ----
  // 曾在输入上旋转 bfix，但校准零位也会同步旋转 → 相对偏差不变，等于空操作。
  // 现在统一由 att::Attitude::setOutputRot() 在投影之后旋转。

  // ---- Y 轴单独取反（左右已对、只差上下时用）----
  if (g_yflip) {
    ay = -ay;
    gy = -gy;
  }

  const uint32_t nowUs = micros();
  float dt = (lastImuUs == 0) ? 0.01f : (nowUs - lastImuUs) / 1e6f;
  lastImuUs = nowUs;

  att::ImuRaw raw;
  raw.ax = ax;
  raw.ay = ay;
  raw.az = az;
  raw.gx = gx;
  raw.gy = gy;
  raw.gz = gz;

  g_att.update(raw, dt);
}

// ---------------- 电源键长按关机（自实现）----------------
// 官方 PWR_Loop() 依赖 PWR_Key.cpp 里的 BAT_State，而它只在"开机时按着 PWR"才置位，
// 接了电池后这个前提经常不成立，导致长按关机失效。这里直接读引脚，行为确定。
static uint32_t tPwrDown = 0;
static bool pwrShutdown = false;

static void handlePwrKey() {
  const bool down = (digitalRead(PWR_KEY_Input_PIN) == LOW);
  if (!down) {
    tPwrDown = 0;
    return;
  }
  if (tPwrDown == 0) {
    tPwrDown = millis();
    return;
  }
  if (!pwrShutdown && millis() - tPwrDown >= 1500UL) {
    pwrShutdown = true;
    DBG("[pwr] long press -> shutdown\n");
    applyBrightness(0);
    delay(200);
    digitalWrite(PWR_Control_PIN, LOW);  // 拉低 SYS_EN，切断电池供电路径
  }
}

// ---------------- 电池 ----------------
static void handleBattery() {
  g_batteryV = BAT_Get_Volts();
  g_batteryPct = voltsToPct(g_batteryV);
}

// ---------------- 状态汇总 ----------------
static void updateStatus() {
  g_status.batteryPct = g_batteryPct;
  g_status.charging = false;  // 本板无独立充电状态引脚
  g_status.wifiRssi = g_wifi.rssi();
  g_status.wifiText = g_wifi.stateText();
  g_status.otaActive = g_wifi.otaActive();
  g_status.dimmed = g_dimmed;
  g_status.screenOff = g_screenOff;
  g_status.uptimeSec = millis() / 1000;
}

// ---------------- 复位原因（判断是掉电、崩溃还是看门狗）----------------
static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC(crash)";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT(voltage drop)";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB(reset by USB/DTR)";
    case ESP_RST_JTAG: return "JTAG";
    case ESP_RST_EFUSE: return "EFUSE";
    case ESP_RST_PWR_GLITCH: return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    default: return "UNKNOWN";
  }
}

// ---------------- OTA 环境诊断 ----------------
#include <esp_ota_ops.h>
#include <esp_partition.h>

static void dumpOtaEnv() {
  DBG("[ota] flash=%u MB  sketch=%u KB  freeSketch=%u KB\n",
      (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
      (unsigned)(ESP.getSketchSize() / 1024), (unsigned)(ESP.getFreeSketchSpace() / 1024));

  const esp_partition_t* run = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  DBG("[ota] running: %s (sub=%u, 0x%06x, %u KB)\n", run ? run->label : "?",
      (unsigned)(run ? run->subtype : 0), (unsigned)(run ? run->address : 0),
      (unsigned)(run ? (run->size / 1024) : 0));
  DBG("[ota] next   : %s (sub=%u, 0x%06x, %u KB)\n", next ? next->label : "NONE",
      (unsigned)(next ? next->subtype : 0), (unsigned)(next ? next->address : 0),
      (unsigned)(next ? (next->size / 1024) : 0));

  // 打印全部 app 分区
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  for (; it != nullptr; it = esp_partition_next(it)) {
    const esp_partition_t* p = esp_partition_get(it);
    DBG("[part] %-6s sub=%u addr=0x%06x size=%u KB\n", p->label, (unsigned)p->subtype,
        (unsigned)p->address, (unsigned)(p->size / 1024));
  }
  esp_partition_iterator_release(it);
}

// ---------------- 崩溃循环保护（safe mode）----------------
// 教训：曾经因为一次初始化断言崩溃，设备陷入 PANIC 重启循环，
// USB-CDC 也随之不稳定，最后连烧录都进不去 —— 差点变砖。
// 机制：用 RTC 内存记录连续"异常复位"次数；若 >=2 次，就进入安全模式，
//       只初始化串口 + WiFi/OTA，跳过 LCD/LVGL/IMU 等高危初始化，
//       保证设备一定能通过网络或串口被救回来。
//       初始化全部成功后才清零计数，所以正常重启不会误入安全模式。
RTC_NOINIT_ATTR static uint32_t g_rtcMagic;
RTC_NOINIT_ATTR static uint32_t g_rtcFailCount;
static constexpr uint32_t kRtcMagic = 0x41544231;  // 'ATB1'
static bool g_safeMode = false;

static void crashGuardBegin() {
  if (g_rtcMagic != kRtcMagic) {  // 首次上电/RTC 被清：重新开始计数
    g_rtcMagic = kRtcMagic;
    g_rtcFailCount = 0;
  }

  const esp_reset_reason_t rr = esp_reset_reason();
  const bool abnormal = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT ||
                         rr == ESP_RST_WDT || rr == ESP_RST_CPU_LOCKUP);
  if (abnormal && g_rtcFailCount < 0xFFFFFFFFu) g_rtcFailCount++;

  // 只有"连续异常"才进安全模式：一次正常上电会把它清零
  g_safeMode = (g_rtcFailCount >= 2);
  DBG("[guard] abnormalReset=%d failCount=%u safeMode=%d\n", (int)abnormal,
      (unsigned)g_rtcFailCount, (int)g_safeMode);
}

// 初始化全部成功后才调用
static void crashGuardHealthy() {
  g_rtcFailCount = 0;
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  const esp_reset_reason_t rr = esp_reset_reason();
  DBG("\n=== AttitudeBall boot ===\n");
  DBG("[reset] reason=%d %s\n", (int)rr, resetReasonName(rr));
  DBG("[heap] free=%u KB  psram=%u KB\n",
      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

  // ---- 0. 崩溃循环保护 ----
  crashGuardBegin();
  if (g_safeMode) {
    DBG("[guard] *** SAFE MODE *** skip LCD/LVGL/IMU; OTA still available\n");
    g_wifi.begin();
    DBG("[boot] safe mode ready (send OTA or reset to retry)\n");
    return;
  }

  // ---- 1. 持久化参数 ----
  const bool hadStore = g_store.begin();
  g_hadStore = hadStore;
  app::Settings& s = g_store.settings();
  g_rot = s.imuRot;
  if (g_rot > 4) g_rot = 0;
  g_invz = (s.imuInvZ != 0);
  g_screenRot = s.screenRot & 3;
  g_sdir = (s.sdir != 0);
  g_bfix = s.bfix & 3;
  g_att.setOutputRot(g_bfix);  // 输出方向微调（投影之后旋转）
  g_att.setOutputXFlip(s.outXFlip != 0);  // 输出 X 镜像（只影响气泡/show）
  g_horizon.setFlipXY(s.horizonFlip != 0);  // 姿态仪画面翻转（持久化）
  g_horizon.setFlipPitch(s.horizonPitchFlip != 0);  // 俯仰方向翻转（持久化）
  g_horizon.setFlipRoll(s.horizonRollFlip != 0);    // 滚转方向翻转（持久化）
  applyHorizonRef();                                // 地平线基准（短按 BOOT 设置）
  g_yflip = (s.yflip != 0);
  g_swipeMinPx = s.swipeMinPx;
  if (g_swipeMinPx < 20) g_swipeMinPx = 20;
  if (g_swipeMinPx > 250) g_swipeMinPx = 250;
  DBG("[store] %s, bright=%u rot=%u invz=%u yflip=%u xflip=%u hpf=%u hrf=%u hflip=%u srot=%u\n",
      hadStore ? "loaded" : "defaults", s.brightness, g_rot, (unsigned)g_invz, (unsigned)g_yflip,
      s.outXFlip, s.horizonPitchFlip, s.horizonRollFlip, s.horizonFlip, g_screenRot);

  // ---- 2. 电源按键（必须在最早期，决定是否保持供电）----
  PWR_Init();
  // BOOT 键：长按切换屏幕方向
  pinMode(BTN_BOOT_PIN, INPUT_PULLUP);
  // ★ 强制维持供电：
  //   官方 PWR_Init() 只在"开机瞬间按着 PWR 键"时才拉高 SYS_EN。
  //   如果板子接了电池，这会导致系统实际靠 USB 供电，USB 一有抖动（比如移动板子）
  //   就整机掉电重启。这里无条件把 SYS_EN 拉高，让电池能无缝接管。
  digitalWrite(PWR_Control_PIN, HIGH);

  // ---- 3. 电池 ADC ----
  BAT_Init();

  // ---- 4. 共享 I2C 总线（GPIO10/11）----
  I2C_Init();
#if BOOT_I2C_SCAN
  scanBus(Wire, "shared(10/11)");
#endif

  // ---- 5. IMU（地址 0x6B，与官方示例一致）----
  QMI8658_Init();
  g_att.setFilterTau((float)(s.filterTauMs ? s.filterTauMs : (uint16_t)FILTER_TAU_MS));
  if (s.calibValid) {
    applyCalibration();
  } else {
    DBG("[calib] not calibrated yet (long-press screen to calibrate)\n");
  }

  // ---- 6. 扩展 IO（LCD/触摸复位都靠它，必须早于 LCD_Init）----
  TCA9554PWR_Init(0x00);

  // ---- 7. 背光（先灭着，等首帧画完再点亮）----
  // 若在 LCD_Init() 之前就点亮，屏上会露出尚未初始化的显存内容 → 开机红绿蓝闪屏
  Backlight_Init();
  LCD_Backlight = 0;
  Set_Backlight(0);

  // ---- 8. LCD + 触摸（LCD_Init 内部会调 Touch_Init）----
  LCD_Init();
  DBG("[lcd] init done\n");

  // ---- 9. LVGL ----
  Lvgl_Init();
  lv_obj_clean(lv_scr_act());  // 去掉 Lvgl_Init 里的示例 label

  // 只创建姿态仪（其余界面已移除，省下 253 KB PSRAM）
  g_horizonReady = g_horizon.create(lv_scr_act());
  if (!g_horizonReady) DBG("[ui] horizon view create failed (PSRAM?)\n");

  g_mode = app::Mode::Horizon;
  updateModeVisibility();

  // ---- 9.1 恢复上次的屏幕方向（必须等 LVGL 起来后才能 invalidate）----
  applyScreenRotation(g_screenRot);

  // ---- 9.2 预热 IMU（让低通滤波先收敛），再渲染首帧 ----
  // 不预热的话首帧用的是零向量 → 刻度梯会被跳过，出现一帧"没有刻度"的画面
  for (int i = 0; i < 24; i++) {
    handleImu();
    delay(5);
  }

  // ---- 9.2b 开机自动归零（等价于开机自动按一次 BOOT）----
  // 每次上电都以当前姿态为"水平"，所以装机后不用手动对齐
  if (!captureHorizonRef()) {
    DBG("[boot] auto-zero skipped (sensor not ready), keeping stored reference\n");
  }

  // ---- 9.3 先渲染首帧（还在内存里，背光未开）----
  if (g_horizonReady) {
    g_horizon.update(g_att.state(), g_status);
  }
  lv_refr_now(NULL);
  LCD_WaitFlush();

  // ---- 9.4 开机 Logo + 渐隐 ----
  // 顺序：画 Logo（背光灭）→ 点背光 → 停留 → 0.8 s 交叉渐隐露出姿态仪
  bool splashOk = false;
#if SPLASH_ENABLE
  splashOk = splash::begin();
  if (splashOk) {
    splash::showLogo();
  } else {
    DBG("[splash] disabled (alloc failed)\n");
  }
#endif
  applyBrightness(s.brightness);
  DBG("[lcd] backlight on (%u%%)\n", (unsigned)s.brightness);
  if (splashOk) {
    delay(SPLASH_HOLD_MS);
    splash::crossfade(g_horizonReady ? g_horizon.pixels() : nullptr, SPLASH_FADE_MS);
  }

  // ---- 10. WiFi / OTA（非阻塞）----
  dumpOtaEnv();
  g_wifi.begin();

  tLastTouch = millis();
  tImu = tUi = tTouch = tBat = tPwr = tDbg = tBtn = millis();

  // 全部初始化都过了，清掉异常计数，下次正常重启不会进安全模式
  crashGuardHealthy();
  DBG("[boot] ready\n");
}

// ============================================================
void loop() {
  const uint32_t now = millis();

  // ---- 安全模式：只维持串口命令与网络，便于 OTA 修复 ----
  if (g_safeMode) {
    handleSerialCmd();
    g_wifi.loop();
    if (now - tDbg >= 2000) {
      tDbg = now;
      DBG("[safe] alive, heap=%u KB, wifi=%s\n",
          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), g_wifi.stateText());
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
  }

  // ---- IMU 100 Hz ----
  if (now - tImu >= IMU_PERIOD_MS) {
    tImu = now;
    handleImu();
  }

  // ---- 触摸 50 Hz ----
  if (now - tTouch >= TOUCH_POLL_MS) {
    tTouch = now;
    handleTouch();
  }

  // ---- 电源键 ----
  if (now - tPwr >= 50) {
    tPwr = now;
    handlePwrKey();
  }

  // ---- BOOT 键（长按切屏幕方向）----
  if (now - tBtn >= BTN_POLL_MS) {
    tBtn = now;
    handleBootKey();
  }

  // ---- 界面刷新（限速，避免刷爆 QSPI）----
  if (now - tUi >= UI_PERIOD_MS) {
    tUi = now;
    updateStatus();
    if (!g_screenOff && g_horizonReady) {
      g_horizon.update(g_att.state(), g_status);
    }
  }

  // ---- 电池 1 Hz ----
  if (now - tBat >= BAT_PERIOD_MS) {
    tBat = now;
    handleBattery();
  }

  // ---- 闲置管理 ----
  handleIdle();

  // ---- 网络 ----
  g_wifi.loop();

  // ---- 串口命令 ----
  handleSerialCmd();

  // ---- LVGL 主循环 ----
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(2));

  // ---- 调试输出 ----
  // ⚠️ 格式串与参数必须严格一一对应：之前少给一个参数导致 %s 读到野指针、每 2.5 s 崩溃重启
  if (now - tDbg >= 500) {
    tDbg = now;
    const att::State& st = g_att.state();
    DBG("up=%lus tilt=%.1f P=%.1f R=%.1f raw=(%+.2f,%+.2f,%+.2f) rate=%.0f bat=%.2fV %d%% rot=%u mode=%s\n",
        (unsigned long)(now / 1000), st.tilt, st.pitch, st.roll, Accel.x, Accel.y, Accel.z,
        st.rate, g_batteryV, g_batteryPct, g_rot, "H");
  }
}
