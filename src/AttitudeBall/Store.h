// Store.h —— 参数持久化（NVS + Preferences）
#pragma once

#include <Arduino.h>

namespace app {

// 显示模式
// 界面：现在只保留姿态仪（人工地平仪）
enum class Mode : uint8_t { Horizon = 0, Count = 1 };

struct Settings {
  // —— 校准零位重力向量（板体坐标系，单位向量）——
  float gx = 0.0f, gy = 0.0f, gz = 1.0f;
  bool calibValid = false;

  // —— 姿态仪地平线基准（短按 BOOT 校准）——
  // 存"物理板体坐标系"下的重力方向，使用时按屏幕方向换算，与校零位互不影响
  uint8_t hCalValid = 0;
  float hgx = 0.0f, hgy = 1.0f, hgz = 0.0f;

  // —— 界面 ——
  uint8_t mode = (uint8_t)Mode::Horizon;  // 只剩姿态仪
  uint8_t brightness = 70;              // 0~100

  // —— 省电 ——
  uint16_t idleDimSec = 10;    // 闲置多少秒降到暗背光（0 = 关闭该功能）
  uint16_t idleOffSec = 30;    // 闲置多少秒熄屏（0 = 不熄屏）
  uint8_t dimBrightness = 12;  // 暗背光百分比

  // —— 灵敏度 ——
  float levelGain = 1.0f;  // 水平仪气泡灵敏度倍数（1.0 = 原始手感）
  float ballGain = 1.0f;   // 姿态球跟随灵敏度倍数
  // 低通滤波时间常数（ms）：越大越平滑、越滞后；可串口 tau N 调
  uint16_t filterTauMs = 300;
  // 水平仪满偏角度（度）：倾斜到该角度气泡贴边。平放探测用 30，竖直使用可设 90
  uint8_t fullScaleDeg = 30;

  // ================= IMU 轴向映射（2026-11-09 实机标定，勿改）=================
  // 最终正确组合：imuRot=0 + imuInvZ=1 + yflip=1
  //     (ax, ay, az) = (+raw_ax, −raw_ay, −raw_az)      行列式 = +1（真旋转）
  // 物理含义：这颗 QMI8658 相对屏幕坐标系是【绕 X 轴翻转 180°】安装的。
  // ⚠️ 为什么以前怎么调都不对：imuRot 只能做"绕 Z 轴"的 0/90/180/270° 旋转，
  //    而绕 Z 的旋转永远无法表达"绕 X 轴 180°"，所以必须配合 yflip（Y 单独取反）。
  // ⚠️ 标定方向时【屏幕方向必须等于最终使用方向】(srot=0)，否则两种旋转会互相抵消，
  //    得到"看起来对、其实错"的配置（这个坑我们踩了十几轮，见 README §7 第 22 条）。
  uint8_t imuRot = 0;   // 绕 Z 旋转步数：0 = 不转
  uint8_t imuInvZ = 1;  // Z 轴取反（平放时 az=+1）
  uint8_t yflip = 1;    // Y 轴单独取反 —— 关键，缺它上下必反

  // —— 姿态球方向微调：bit0=翻转 pitch, bit1=翻转 roll ——
  uint8_t ballFlip = 0;

  // —— 屏幕方向：0/1/2/3 = 0°/90°/180°/270°（BOOT 键长按在 0°↔180° 切换）——
  uint8_t screenRot = 0;
  // 屏幕旋转方向补偿：0 = 正转(+90°/次)，1 = 反转（-90°/次）
  uint8_t sdir = 0;
  // BOOT 长按转屏开关：默认【开启】（用户要求）。
  // 防误触已由三处保证：长按阈值 2 s、上电后必须先见到一次"松开"、每次长按只触发一次。
  // 需要关闭时串口发 bootrot 0。
  uint8_t bootRotEnable = 1;
  // 气泡/姿态方向最终微调：0..3 = 每步 90°，用于最后"眼校"对齐（持久化）
  uint8_t bfix = 0;
  // 输出 X（左右）镜像：只影响投影结果（水平仪气泡 / show 数值），不影响姿态仪画面
  uint8_t outXFlip = 0;
  // 姿态仪画面 180° 翻转（默认 0：本板不需要）
  uint8_t horizonFlip = 0;
  // 姿态仪俯仰方向翻转（抬头时地平线该往下移）—— 本板实测需要 1
  uint8_t horizonPitchFlip = 1;
  // 姿态仪滚转方向翻转（左右倾斜时地平线的转向）—— 本板实测需要 1
  uint8_t horizonRollFlip = 1;
  // 左右滑动判定阈值（像素）：越大越不灵敏；手势需 |dx| ≥ 该值且 |dx| > |dy|×1.5
  uint16_t swipeMinPx = 70;

  // —— 版本标记，用于识别 NVS 里的旧数据 ——
  uint32_t magic = 0;
};

class Store {
 public:
  // v4：校准零位改为"物理板体坐标系"存储（对屏幕方向免疫），旧数据作废需重新校准
  static constexpr uint32_t kMagic = 0xA77B0014;

  bool begin();
  Settings& settings() { return _s; }

  // 保存/读取/恢复默认
  bool save();
  bool load();
  void toDefaults();

  // 只存校准（校准后立刻落盘，避免掉电丢失）
  bool saveCalib(float gx, float gy, float gz);

 private:
  Settings _s;
};

}  // namespace app
