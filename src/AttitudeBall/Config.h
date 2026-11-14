// Config.h —— 用户配置（唯一需要你改的文件）
//
// ⚠️ 填好 WiFi 信息后 OTA 升级才可用；不填也不影响主功能。
#pragma once

// ============================================================
// 1. WiFi / OTA
// ============================================================
#define ENABLE_WIFI_OTA 1               // 0 = 完全编掉 WiFi 相关代码
#define WIFI_SSID "Hanice@Wi-Fi"
#define WIFI_PASS "1231231234"
#define OTA_HOSTNAME "attitude-ball"    // 局域网主机名：attitude-ball.local
#define OTA_PASSWORD "attball"          // OTA 密码，建议改
#define WIFI_CONNECT_TIMEOUT_MS 15000   // 连不上就放弃，不阻塞主功能

// ============================================================
// 2. IMU（QMI8658）
// ============================================================
// 官方示例 Gyro_QMI8658.cpp 里 Device_addr = QMI8658_L_SLAVE_ADDRESS = 0x6B，
// 这是本板的实际地址（0x6A 是 SA0 接高的另一种板型，这里只留 0x6B）。
// 若换板或读数全 0，先看开机时打印的 I2C 扫描结果。
#define QMI8658_ADDR 0x6B

// 采样与滤波
#define IMU_PERIOD_MS 10    // 100 Hz 采样
#define FILTER_TAU_MS 300   // 低通时间常数默认值（可被 NVS / 串口 tau N 覆盖）

// ---- IMU 安装方向 ----
// 实机测得：屏幕朝上平放时 QMI8658 的 az ≈ -1g（芯片 Z 轴指向板子背面），
// 与"屏幕法线朝上"相反，必须取反，否则平放会被算成 180°。
// 该修正已改为【运行时可调 + NVS 持久化】（默认 invz=1, rot=0），
// 通过串口命令 rot N / invz 0|1 / save 调整，无需重新编译。
// 见 AttitudeBall.ino 里的 runCmd()。

// ============================================================
// 3. 屏幕 / 背光
// ============================================================
#define SCREEN_W 360
#define SCREEN_H 360
// 背光由官方 Backlight_Init()/Set_Backlight(0~100) 管，这里只定义亮度档位
#define BRIGHT_DEFAULT 70
#define BRIGHT_DIM 12
#define BRIGHT_MIN 5      // 上下滑动调光的下限
#define BRIGHT_MAX 100    // 上限
#define BRIGHT_STEP 10    // 每次滑动调整的步进

// ---- 开机 Logo / 渐隐 ----
#define SPLASH_ENABLE 1
#define SPLASH_HOLD_MS 700   // Logo 完全显示的停留时间
#define SPLASH_FADE_MS 800   // 渐隐到姿态仪画面的时长（需求：0.8 s）

// ============================================================
// 4. 交互
// ============================================================
#define TOUCH_POLL_MS 20         // 触摸轮询周期
// 自动降亮度 / 熄屏：均为 0 = 关闭（按需求：不自动息屏，亮度靠上下滑动调）
#define IDLE_DIM_MS 0UL          // 闲置多久降亮度（0 = 关闭）
#define IDLE_OFF_MS 0UL          // 闲置多久熄屏（0 = 关闭）
#define BAT_PERIOD_MS 1000       // 电池采样周期
#define UI_PERIOD_MS 33          // 界面刷新上限（约 30 fps）

// ---- 物理按键 ----
// BOOT 键（GPIO0，板载 BOOT 按键）：长按切换屏幕方向 0°→90°→180°→270°
#define BTN_BOOT_PIN 0
// BOOT 键长按切换屏幕方向（0°↔180°）
// ⚠️ 门槛设 2 秒是故意的：实测 0.8 秒时拿板子/插线很容易误触，
//    而误触会把屏幕转 180°，看起来就像"气泡方向全反了"（踩过两次）。
#define BOOT_LONG_PRESS_MS 2000UL

// BOOT 键【短按】校准零位（替代原来的"长按屏幕校准"）
// 短按 = 按住时长在 [MIN, MAX) 之间且未触发长按动作；<MIN 视为抖动丢弃
#define BOOT_CAL_MIN_MS 40UL
#define BOOT_CAL_MAX_MS 1000UL
// PWR 键（GPIO6）：长按关机，见 PWR_Key.h
#define PWR_LONG_PRESS_MS 1500UL
// 按键轮询周期
#define BTN_POLL_MS 30

// 手势去抖：CST816 在手指按住/滑动期间会持续上报同一个手势值，
// 必须"一次触摸只处理一个手势 + 冷却时间"，否则一次滑动会触发几十次切换。
#define GESTURE_COOLDOWN_MS 500UL

// ============================================================
// 5. 电池
// ============================================================
// 官方 BAT_Driver.h：BAT_ADC_PIN = 8，Measurement_offset = 0.990476
// 单节锂电 3.7V 电压→电量映射（线性近似，够用）
#define BAT_V_MIN 3.30f
#define BAT_V_MAX 4.20f

// ============================================================
// 6. 姿态解算
// ============================================================
#define LEVEL_FULLSCALE_DEG 30.0f  // 倾斜多少度时气泡贴边

// ---- 水平仪气泡方向微调 ----
// 实机若发现"往右倾但气泡往左跑"，把对应项改成 1 即可，不用改算法。
#define LEVEL_INVERT_X 0
#define LEVEL_INVERT_Y 0

// ============================================================
// 7. 调试
// ============================================================
#define DEBUG_SERIAL 1
#if DEBUG_SERIAL
#define DBG(...) Serial.printf(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

#define BOOT_I2C_SCAN 1  // 开机扫描两条 I2C 总线并打印在线设备
