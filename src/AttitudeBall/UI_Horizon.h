// UI_Horizon.h —— 人工地平仪（姿态仪 / Artificial Horizon）
//
// 设计参考：航空姿态仪
//   · 蓝天/棕地 + 地平线（随滚转倾斜、随俯仰平移）
//   · 俯仰刻度梯：±10/20/30° 横线 + 数字，随地平线一起旋转
//   · 顶部滚转刻度弧 + 固定红色三角指示
//   · 中心固定的飞机符号（黄色）
//
// 实现方式与姿态球一致：PSRAM 里自备 360×360 RGB565 帧缓冲，自己光栅化，
// 再通过 lv_canvas 挂到 LVGL。逐像素填充仅画圆内，圆外留黑（屏幕本来就是圆的）。
#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "App_Attitude.h"
#include "App_Status.h"

namespace ui {

class HorizonView {
 public:
  bool create(lv_obj_t* parent);
  void update(const att::State& s, const app::RuntimeStatus& st);
  void destroy();
  void setVisible(bool v);
  void setGain(float g) { _gain = g; }
  float gain() const { return _gain; }
  // 供开机渐隐使用：当前帧缓冲（360×360 RGB565，PSRAM）
  const uint16_t* pixels() const { return _fb; }

  // 临时叠加层：显示亮度条，durationMs 后自动消失
  void showOverlay(int pct, uint32_t durationMs = 450);

  // 调试用：把当前帧缓冲（360×360×2 字节，RGB565 小端）从串口原样输出，
  // 供上位机还原成 PNG 截图（发布文档用）。格式：[shot] 360 360\n + 259200 字节
  void dumpToSerial();
  // 天地/左右整体翻转 180°（本板实测需要开启：否则天在下、滚转方向也反）
  void setFlipXY(bool f) { _flipXY = f; }
  bool flipXY() const { return _flipXY; }
  // 只翻转俯仰方向（抬头时地平线该往下移）。只取反 uz，不动左右与天地布置。
  void setFlipPitch(bool f) { _flipPitch = f; }
  bool flipPitch() const { return _flipPitch; }
  // 只翻转滚转方向（左右倾斜时地平线的转向）。只取反 ux。
  void setFlipRoll(bool f) { _flipRoll = f; }
  bool flipRoll() const { return _flipRoll; }

  // 地平线基准（短按 BOOT 校准）：传入"内容坐标系"下该姿态的重力方向。
  // 渲染时先把当前重力旋转到该基准为"正上方"，于是该姿态下地平线正好居中水平
  // —— 等同于真实姿态仪的 cage / 快速校正。
  void setReference(bool valid, float rx, float ry, float rz) {
    _refValid = valid;
    _rx = rx;
    _ry = ry;
    _rz = rz;
  }

 private:
  void render(const att::State& s);

  lv_obj_t* _root = nullptr;
  lv_obj_t* _canvas = nullptr;
  uint16_t* _fb = nullptr;

  int _lastPitch10 = INT32_MIN, _lastRoll10 = INT32_MIN;
  float _gain = 1.0f;
  bool _flipXY = false;  // 天地/左右整体翻转（默认关闭；实测本板不需要）
  bool _flipPitch = false;  // 只翻俯仰方向
  bool _flipRoll = false;   // 只翻滚转方向
  bool _refValid = false;   // 地平线基准是否有效
  float _rx = 0.0f, _ry = 1.0f, _rz = 0.0f;  // 基准重力方向（内容坐标系）
  int _ovPct = -1;          // 叠加层：亮度百分比（-1 = 不显示）
  uint32_t _ovUntil = 0;    // 叠加层消失时刻
  bool _forceRender = false;
};

}  // namespace ui
