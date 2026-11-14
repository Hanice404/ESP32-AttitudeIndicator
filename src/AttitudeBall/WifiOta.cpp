// WifiOta.cpp
#include "WifiOta.h"

#include "Config.h"

#if ENABLE_WIFI_OTA
#include <ArduinoOTA.h>
#include <WiFi.h>
#endif

namespace net {

#if ENABLE_WIFI_OTA

void WifiOta::begin() {
  // 没填 SSID 就当作未启用，避免一直重试刷日志
  if (strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0) {
    DBG("[wifi] SSID not configured, skip WiFi/OTA\n");
    _state = WifiState::Idle;
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  // 姿态球对实时性敏感，WiFi 省电模式会引入抖动
  WiFi.setSleep(false);
  startConnect();
}

void WifiOta::startConnect() {
  DBG("[wifi] connecting to \"%s\" ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  _state = WifiState::Connecting;
  _tConnectStart = millis();
}

void WifiOta::startOta() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([this]() {
    _state = WifiState::OtaUpdating;
    DBG("[ota] start (%s)\n",
        ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
  });

  ArduinoOTA.onEnd([this]() {
    DBG("[ota] done, rebooting\n");
    _state = WifiState::Online;
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // 只在整 10% 时打印，避免刷屏影响主循环
    static unsigned int lastPct = 100;
    const unsigned int pct = total ? (progress * 100u / total) : 0;
    if (pct / 10 != lastPct / 10) {
      lastPct = pct;
      DBG("[ota] %u%%\n", pct);
    }
  });

  ArduinoOTA.onError([](ota_error_t err) {
    DBG("[ota] error[%u]\n", (unsigned)err);
  });

  ArduinoOTA.begin();
  _otaStarted = true;
  DBG("[wifi] online, ip=%s, OTA host=%s.local\n", WiFi.localIP().toString().c_str(),
      OTA_HOSTNAME);
}

void WifiOta::loop() {
  switch (_state) {
    case WifiState::Idle:
      return;

    case WifiState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        _state = WifiState::Online;
        _retryDelayMs = 5000;
        startOta();
      } else if (millis() - _tConnectStart > WIFI_CONNECT_TIMEOUT_MS) {
        DBG("[wifi] timeout, retry in %u ms\n", (unsigned)_retryDelayMs);
        WiFi.disconnect(true, false);
        _state = WifiState::Failed;
        _tBackoff = millis();
      }
      break;

    case WifiState::Online:
      // 掉线检测
      if (WiFi.status() != WL_CONNECTED) {
        DBG("[wifi] lost connection\n");
        _otaStarted = false;
        _state = WifiState::Failed;
        _tBackoff = millis();
        break;
      }
      if (_otaStarted) ArduinoOTA.handle();  // 只在在线且已注册时处理
      break;

    case WifiState::OtaUpdating:
      // 更新期间不要处理其它网络逻辑
      if (_otaStarted) ArduinoOTA.handle();
      break;

    case WifiState::Failed:
      if (millis() - _tBackoff > _retryDelayMs) {
        // 指数退避，最长 60 s
        _retryDelayMs = min<uint32_t>(_retryDelayMs * 2, 60000u);
        startConnect();
      }
      break;
  }
}

int WifiOta::rssi() const {
  if (_state != WifiState::Online && _state != WifiState::OtaUpdating) return 0;
  return WiFi.RSSI();
}

String WifiOta::ip() const {
  if (_state != WifiState::Online && _state != WifiState::OtaUpdating) return String("-");
  return WiFi.localIP().toString();
}

const char* WifiOta::stateText() const {
  switch (_state) {
    case WifiState::Idle: return "off";
    case WifiState::Connecting: return "connecting";
    case WifiState::Online: return "online";
    case WifiState::Failed: return "retry";
    case WifiState::OtaUpdating: return "OTA";
  }
  return "?";
}

#else  // ENABLE_WIFI_OTA == 0：全部编成空实现

void WifiOta::begin() {}
void WifiOta::loop() {}
int WifiOta::rssi() const { return 0; }
String WifiOta::ip() const { return String("-"); }
const char* WifiOta::stateText() const { return "off"; }

#endif

}  // namespace net
