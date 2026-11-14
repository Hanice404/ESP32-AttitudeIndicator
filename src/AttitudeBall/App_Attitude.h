// App_Attitude.h —— 姿态解算与校准（纯算法，不依赖任何硬件驱动）
//
// 设计要点：
// 1. 校准不是简单减偏差，而是记录"零位时的重力向量 g0"，然后构造一组
//    以 g0 为 Z 轴的正交基 (e1, e2, up)。把实时重力向量投影到这组基上，
//    就得到"相对零位"的倾斜量。这样无论你把板子装成什么角度当零点，都成立。
// 2. 只用加速度计做倾斜（水平仪/姿态球足够），陀螺仪仅用于显示角速度与峰值。
// 3. 一阶 IIR 低通滤波，时间常数可调（默认 tau = 80 ms）。

#pragma once

#include <Arduino.h>

namespace att {

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

// 一次 IMU 读数（单位：加速度 g，角速度 deg/s）
struct ImuRaw {
  float ax = 0, ay = 0, az = 0;  // g
  float gx = 0, gy = 0, gz = 0;  // deg/s
};

// 解算结果
struct State {
  // —— 滤波后的原始量 ——
  ImuRaw filtered;

  // —— 校准坐标系下的重力分量（g 为单位）——
  // ax_c / ay_c 是"水平方向"，反映倾斜方向；az_c 接近 1 表示接近水平
  float axc = 0, ayc = 0, azc = 1;

  // —— 角度（度）——
  float tilt = 0;      // 合倾斜角，0 = 完全水平
  float tiltDir = 0;   // 倾斜方向（0~360），气泡该往哪边跑
  float roll = 0;      // 绕 X 轴（左右倾斜）
  float pitch = 0;     // 绕 Y 轴（前后倾斜）

  // 偏航（绕重力轴的自转）：加速度计测不到，由陀螺仪分量积分得到，
  // 并带缓慢回正（避免积分漂移累积）。仅用于姿态球视觉。
  float yaw = 0;

  // —— 角速度与峰值记录 ——
  float rate = 0;      // 合角速度 deg/s
  float peakTilt = 0;  // 自上次复位以来的最大倾斜角
  float peakRate = 0;  // 自上次复位以来的最大角速度
};

class Attitude {
 public:
  // 滤波时间常数（毫秒）。越大越平滑但越滞后；水平仪推荐 60~120。
  void setFilterTau(uint16_t tauMs) { _tauMs = tauMs; }

  // 输出方向微调：把投影后的 (axc,ayc) 再旋转 k×90°（0..3）。
  // ⚠️ 必须在"投影之后"旋转，不能在输入上旋转——
  //    若同时旋转输入和校准零点，两者同转，相对偏差不变，等于没改（踩过这个坑）。
  void setOutputRot(uint8_t k) { _outRot = k & 3; }
  uint8_t outputRot() const { return _outRot; }

  // 输出 X（左右）镜像：只影响投影结果 axc/ayc（水平仪气泡、show 数值），
  // 不影响 filtered（姿态仪用的是原始重力向量）—— 所以画面不会变。
  void setOutputXFlip(bool f) { _outXFlip = f; }
  bool outputXFlip() const { return _outXFlip; }

  // 偏航回正时间常数（ms）。0 = 不积分（yaw 恒为 0）
  void setYawTau(uint16_t ms) { _yawTauMs = ms; }
  void resetYaw() { _yaw = 0; }

  // 是否已校准
  bool calibrated() const { return _hasCalib; }

  // 重置滤波器与状态（上电或切换模式时调用）
  void reset();

  // 主更新：传入原始 IMU 读数与 dt（秒），返回最新状态
  const State& update(const ImuRaw& raw, float dtSec);

  const State& state() const { return _st; }

  // —— 校准 ——
  // 以"当前滤波后的重力向量"为零位
  void calibrateNow();
  void clearCalibration();

  // 校准数据读写（由 Store 层负责持久化，这里只交出裸数据）
  bool getCalibGravity(Vec3& g0) const;
  void setCalibGravity(const Vec3& g0);

  // 峰值复位
  void resetPeaks();

  // 零位重力向量（未校准时为 +Z）
  const Vec3& zeroGravity() const { return _g0; }

 private:
  void recomputeBasis();

  State _st;
  Vec3 _g0{0, 0, 1};      // 零位重力向量（板体坐标系）
  Vec3 _up{0, 0, 1};      // 零位坐标系的 Z
  Vec3 _e1{1, 0, 0};      // 零位坐标系的 X
  Vec3 _e2{0, 1, 0};      // 零位坐标系的 Y

  bool _hasCalib = false;
  bool _first = true;
  uint16_t _tauMs = 80;
  uint8_t _outRot = 0;  // 输出旋转步数（0..3，每步 90°）
  bool _outXFlip = false;  // 输出 X 镜像
  float _yaw = 0;       // 偏航角（度）
  uint16_t _yawTauMs = 8000;  // 偏航回正时间常数：越大回正越慢、越"记得"转动
};

// 常用的向量小工具（其它模块也会用到）
inline Vec3 v3(float x, float y, float z) { return Vec3{x, y, z}; }
float v3_len(const Vec3& v);
Vec3 v3_norm(const Vec3& v);
float v3_dot(const Vec3& a, const Vec3& b);
Vec3 v3_cross(const Vec3& a, const Vec3& b);

}  // namespace att
