// WifiOta.h —— 非阻塞 WiFi 连接 + ArduinoOTA 无线升级
//
// 设计原则：WiFi 绝不允许阻塞主功能。
// begin() 只发起连接，loop() 里推进状态机；连不上就安静地重试，姿态功能照常运行。
#pragma once

#include <Arduino.h>

namespace net {

enum class WifiState : uint8_t {
  Idle = 0,       // 未启用或已关闭
  Connecting,
  Online,
  Failed,         // 本次尝试失败，等待退避后重试
  OtaUpdating,    // 正在通过 OTA 更新
};

class WifiOta {
 public:
  void begin();
  void loop();

  bool online() const { return _state == WifiState::Online; }
  bool otaActive() const { return _state == WifiState::OtaUpdating; }
  WifiState state() const { return _state; }
  int rssi() const;
  String ip() const;
  const char* stateText() const;

 private:
  void startConnect();
  void startOta();

  WifiState _state = WifiState::Idle;
  bool _otaStarted = false;
  uint32_t _tConnectStart = 0;
  uint32_t _tBackoff = 0;
  uint32_t _retryDelayMs = 5000;
};

}  // namespace net
