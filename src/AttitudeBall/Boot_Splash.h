// Boot_Splash.h —— 开机 Logo 与渐隐
//
// 在 setup() 里的调用顺序：
//   splash::begin()            解码内置 Logo（RLE → PSRAM）
//   splash::showLogo()         整屏推 Logo（此时背光还灭着，看不到任何闪烁）
//   applyBrightness(...)       点亮背光
//   delay(停留时间)
//   splash::crossfade(ui, 800) 用 0.8 s 交叉渐隐进入姿态仪首帧
//
// 为什么用交叉渐隐而不是单纯调背光：这样 Logo 淡出的同时仪表画面浮现，
// 视觉上是一个连贯过渡，而不是"灭一下再亮一下"。
#pragma once

#include <stdint.h>

namespace splash {

// 把 Logo 解码到 PSRAM。成功返回 true。
bool begin();

// 整屏推送 Logo
void showLogo();

// 从 Logo 交叉渐隐到 ui（ui 必须是 360×360 的 RGB565 缓冲）。
// ui 传空则渐隐到全黑。totalMs 为总时长。
void crossfade(const uint16_t* ui, uint32_t totalMs);

}  // namespace splash
