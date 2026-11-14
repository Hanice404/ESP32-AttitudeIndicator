// App_Status.h —— 供 UI 层绘制的运行时状态（与具体驱动解耦）
#pragma once

#include <stdint.h>

namespace app {

struct RuntimeStatus {
  int batteryPct = -1;    // 0~100，-1 = 未接电池/读数无效
  bool charging = false;
  int wifiRssi = 0;       // 0 = 未连接
  const char* wifiText = "off";
  bool dimmed = false;    // 当前是否处于降亮度状态
  bool screenOff = false; // 当前是否熄屏
  bool otaActive = false;
  uint32_t uptimeSec = 0;
};

}  // namespace app
