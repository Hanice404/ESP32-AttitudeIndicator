// App_Attitude.cpp
#include "App_Attitude.h"

#include <math.h>

namespace att {

static constexpr float kRad2Deg = 57.29577951308232f;
static constexpr float kEps = 1e-6f;

float v3_len(const Vec3& v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

Vec3 v3_norm(const Vec3& v) {
  const float l = v3_len(v);
  if (l < kEps) return Vec3{0, 0, 1};
  return Vec3{v.x / l, v.y / l, v.z / l};
}

float v3_dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 v3_cross(const Vec3& a, const Vec3& b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

void Attitude::reset() {
  _st = State{};
  _st.azc = 1.0f;
  _first = true;
}

void Attitude::resetPeaks() {
  _st.peakTilt = 0;
  _st.peakRate = 0;
}

void Attitude::clearCalibration() {
  _hasCalib = false;
  _g0 = Vec3{0, 0, 1};
  _up = Vec3{0, 0, 1};
  _e1 = Vec3{1, 0, 0};
  _e2 = Vec3{0, 1, 0};
  resetPeaks();
}

void Attitude::setCalibGravity(const Vec3& g0) {
  _g0 = v3_norm(g0);
  _hasCalib = true;
  recomputeBasis();
}

bool Attitude::getCalibGravity(Vec3& g0) const {
  if (!_hasCalib) return false;
  g0 = _g0;
  return true;
}

void Attitude::calibrateNow() {
  // 用滤波后的重力向量作为零位；若滤波器尚未收敛则退回原始 Z 轴
  Vec3 g = v3_norm(Vec3{_st.filtered.ax, _st.filtered.ay, _st.filtered.az});
  if (v3_len(g) < 0.5f) g = Vec3{0, 0, 1};
  setCalibGravity(g);
  resetPeaks();
}

// 构造以零位重力为 Z 轴的正交基
void Attitude::recomputeBasis() {
  _up = v3_norm(_g0);

  // 选一个与 up 不平行的参考轴，避免叉乘退化
  Vec3 ref = (fabsf(_up.x) < 0.9f) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};

  _e1 = v3_norm(v3_cross(ref, _up));
  _e2 = v3_cross(_up, _e1);  // up 与 e1 已正交，故 e2 自动单位化
}

const State& Attitude::update(const ImuRaw& raw, float dtSec) {
  if (!(dtSec > 0.0f)) dtSec = 0.01f;
  if (dtSec > 0.5f) dtSec = 0.5f;  // 防止偶发长间隔把滤波器打崩

  // ---- 一阶 IIR 低通：alpha = 1 - exp(-dt/tau) ----
  const float tau = (float)_tauMs / 1000.0f;
  const float alpha = (_tauMs == 0) ? 1.0f : (1.0f - expf(-dtSec / tau));

  if (_first) {
    _st.filtered = raw;
    _first = false;
  } else {
    _st.filtered.ax += alpha * (raw.ax - _st.filtered.ax);
    _st.filtered.ay += alpha * (raw.ay - _st.filtered.ay);
    _st.filtered.az += alpha * (raw.az - _st.filtered.az);
    _st.filtered.gx += alpha * (raw.gx - _st.filtered.gx);
    _st.filtered.gy += alpha * (raw.gy - _st.filtered.gy);
    _st.filtered.gz += alpha * (raw.gz - _st.filtered.gz);
  }

  // ---- 投影到零位坐标系 ----
  Vec3 g = v3_norm(Vec3{_st.filtered.ax, _st.filtered.ay, _st.filtered.az});
  _st.axc = v3_dot(g, _e1);
  _st.ayc = v3_dot(g, _e2);
  _st.azc = v3_dot(g, _up);

  // ---- 输出方向微调：在【投影之后】旋转，否则会与校准零点同步旋转而互相抵消 ----
  if (_outRot) {
    const float cx = _st.axc, cy = _st.ayc;
    switch (_outRot) {
      case 1: _st.axc = -cy; _st.ayc = cx; break;   // +90°
      case 2: _st.axc = -cx; _st.ayc = -cy; break;  // 180°
      case 3: _st.axc = cy;  _st.ayc = -cx; break;  // +270°
    }
  }
  // 输出 X 镜像（只影响投影结果，不影响 filtered，所以姿态仪画面不受影响）
  if (_outXFlip) _st.axc = -_st.axc;

  // ---- 合倾斜角与方向 ----
  const float horiz = sqrtf(_st.axc * _st.axc + _st.ayc * _st.ayc);
  _st.tilt = atan2f(horiz, _st.azc) * kRad2Deg;
  if (horiz > kEps) {
    float d = atan2f(_st.ayc, _st.axc) * kRad2Deg;
    if (d < 0) d += 360.0f;
    _st.tiltDir = d;
  }

  // ---- 姿态球用的 pitch / roll ----
  // 约定（已按本板实测轴向修正后的坐标系）：
  //   roll  > 0 = 屏幕右侧抬高
  //   pitch > 0 = 屏幕上侧（远端）抬高
  _st.roll = atan2f(_st.axc, _st.azc) * kRad2Deg;
  _st.pitch = atan2f(_st.ayc, _st.azc) * kRad2Deg;

  // ---- 偏航积分（绕重力轴的自转，加速度计测不到）----
  // 取陀螺在重力方向上的分量 g·û 作为"绕垂直轴角速度"，积分后缓慢回正。
  // 完全是视觉增强：静止时缓慢归零，避免漂移累积；转动时球体会跟着转。
  if (_yawTauMs > 0) {
    const float gdotu = _st.filtered.gx * g.x + _st.filtered.gy * g.y + _st.filtered.gz * g.z;
    _yaw += gdotu * dtSec;
    _yaw *= expf(-dtSec / ((float)_yawTauMs / 1000.0f));
    if (_yaw > 180.0f) _yaw -= 360.0f;
    if (_yaw < -180.0f) _yaw += 360.0f;
  }
  _st.yaw = _yaw;

  // ---- 角速度与峰值 ----
  _st.rate = sqrtf(_st.filtered.gx * _st.filtered.gx + _st.filtered.gy * _st.filtered.gy +
                   _st.filtered.gz * _st.filtered.gz);
  if (_st.tilt > _st.peakTilt) _st.peakTilt = _st.tilt;
  if (_st.rate > _st.peakRate) _st.peakRate = _st.rate;

  return _st;
}

}  // namespace att
