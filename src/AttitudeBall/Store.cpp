// Store.cpp
#include "Store.h"

#include <Preferences.h>

namespace app {

static Preferences s_prefs;
static const char* kNs = "attball";

bool Store::begin() {
  // 只读方式打开，读不到就用默认值
  if (!s_prefs.begin(kNs, true)) {
    toDefaults();
    return false;
  }

  const uint32_t magic = s_prefs.getUInt("magic", 0);
  if (magic != kMagic) {
    // 版本不匹配：必须把旧键值【整体清掉】再写新 magic。
    // 只改 magic 的话，下次启动 magic 就匹配了，会把旧版本残留的
    // calibOk / sRot 等全部读回来（曾经因此反复清不掉旧校准）。
    s_prefs.end();
    toDefaults();
    s_prefs.begin(kNs, false);
    s_prefs.clear();
    s_prefs.putUInt("magic", kMagic);
    s_prefs.end();
    return false;
  }

  _s.gx = s_prefs.getFloat("gx", 0.0f);
  _s.gy = s_prefs.getFloat("gy", 0.0f);
  _s.gz = s_prefs.getFloat("gz", 1.0f);
  _s.calibValid = s_prefs.getBool("calibOk", false);
  _s.hCalValid = s_prefs.getUChar("hcal", 0) & 1;
  _s.hgx = s_prefs.getFloat("hgx", 0.0f);
  _s.hgy = s_prefs.getFloat("hgy", 1.0f);
  _s.hgz = s_prefs.getFloat("hgz", 0.0f);

  _s.mode = s_prefs.getUChar("mode", (uint8_t)Mode::Horizon);
  _s.brightness = s_prefs.getUChar("bright", 70);
  _s.idleDimSec = s_prefs.getUShort("dimSec", 10);
  _s.idleOffSec = s_prefs.getUShort("offSec", 30);
  _s.dimBrightness = s_prefs.getUChar("dimB", 12);
  _s.levelGain = s_prefs.getFloat("lGain", 1.0f);
  _s.ballGain = s_prefs.getFloat("bGain", 1.0f);
  _s.filterTauMs = s_prefs.getUShort("tau", 300);
  _s.fullScaleDeg = s_prefs.getUChar("fsdeg", 30);
  _s.imuRot = s_prefs.getUChar("imuRot", 4);
  _s.imuInvZ = s_prefs.getUChar("imuInvZ", 1);
  _s.ballFlip = s_prefs.getUChar("bFlip", 0) & 3;
  _s.screenRot = s_prefs.getUChar("sRot", 0) & 3;
  _s.sdir = s_prefs.getUChar("sdir", 0) & 1;
  _s.bootRotEnable = s_prefs.getUChar("bootrot", 0) & 1;
  _s.bfix = s_prefs.getUChar("bfix", 0) & 3;
  _s.outXFlip = s_prefs.getUChar("xflip", 0) & 1;
  _s.horizonFlip = s_prefs.getUChar("hflip", 0) & 1;
  _s.horizonPitchFlip = s_prefs.getUChar("hpf", 1) & 1;
  _s.horizonRollFlip = s_prefs.getUChar("hrf", 1) & 1;
  _s.swipeMinPx = s_prefs.getUShort("swpx", 70);
  _s.yflip = s_prefs.getUChar("yflip", 0) & 1;
  _s.magic = kMagic;

  // 合法性兜底
  if (_s.brightness > 100) _s.brightness = 70;
  if (_s.dimBrightness > 100) _s.dimBrightness = 12;
  if (_s.mode >= (uint8_t)Mode::Count) _s.mode = (uint8_t)Mode::Horizon;
  if (_s.imuRot > 4) _s.imuRot = 4;

  s_prefs.end();
  return true;
}

void Store::toDefaults() {
  _s = Settings{};
  _s.magic = kMagic;
}

bool Store::save() {
  if (!s_prefs.begin(kNs, false)) return false;
  s_prefs.putUInt("magic", kMagic);
  s_prefs.putFloat("gx", _s.gx);
  s_prefs.putFloat("gy", _s.gy);
  s_prefs.putFloat("gz", _s.gz);
  s_prefs.putBool("calibOk", _s.calibValid);
  s_prefs.putUChar("hcal", _s.hCalValid);
  s_prefs.putFloat("hgx", _s.hgx);
  s_prefs.putFloat("hgy", _s.hgy);
  s_prefs.putFloat("hgz", _s.hgz);
  s_prefs.putUChar("mode", _s.mode);
  s_prefs.putUChar("bright", _s.brightness);
  s_prefs.putUShort("dimSec", _s.idleDimSec);
  s_prefs.putUShort("offSec", _s.idleOffSec);
  s_prefs.putUChar("dimB", _s.dimBrightness);
  s_prefs.putFloat("lGain", _s.levelGain);
  s_prefs.putFloat("bGain", _s.ballGain);
  s_prefs.putUShort("tau", _s.filterTauMs);
  s_prefs.putUChar("fsdeg", _s.fullScaleDeg);
  s_prefs.putUChar("imuRot", _s.imuRot);
  s_prefs.putUChar("imuInvZ", _s.imuInvZ);
  s_prefs.putUChar("bFlip", _s.ballFlip);
  s_prefs.putUChar("sRot", _s.screenRot);
  s_prefs.putUChar("sdir", _s.sdir);
  s_prefs.putUChar("bootrot", _s.bootRotEnable);
  s_prefs.putUChar("bfix", _s.bfix);
  s_prefs.putUChar("xflip", _s.outXFlip);
  s_prefs.putUChar("hflip", _s.horizonFlip);
  s_prefs.putUChar("hpf", _s.horizonPitchFlip);
  s_prefs.putUChar("hrf", _s.horizonRollFlip);
  s_prefs.putUShort("swpx", _s.swipeMinPx);
  s_prefs.putUChar("yflip", _s.yflip);
  s_prefs.end();
  return true;
}

bool Store::load() { return begin(); }

bool Store::saveCalib(float gx, float gy, float gz) {
  _s.gx = gx;
  _s.gy = gy;
  _s.gz = gz;
  _s.calibValid = true;
  if (!s_prefs.begin(kNs, false)) return false;
  s_prefs.putUInt("magic", kMagic);
  s_prefs.putFloat("gx", gx);
  s_prefs.putFloat("gy", gy);
  s_prefs.putFloat("gz", gz);
  s_prefs.putBool("calibOk", true);
  s_prefs.end();
  return true;
}

}  // namespace app
