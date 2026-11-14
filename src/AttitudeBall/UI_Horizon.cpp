// UI_Horizon.cpp —— 人工地平仪（姿态仪）实现
//
// 核心思路（关键，别再改错）：
//   天地是【绝对参考】，必须用"重力方向"直接判定，不能用校准后的相对投影。
//   设 u = 内容坐标系下的重力方向（= 滤波后的加速度，单位向量），f = 焦距(像素)。
//   屏幕上某像素对应的视线为 r = (dx, dy, f)，dy 取设备坐标（向上为正）。
//   该视线在地平线之上 ⇔ dot(r, u) > 0：
//        d = dx·ux + dy·uy + f·uz > 0  →  天空
//   俯仰刻度线 m° 的位置就是让 dot(r,u)/|r| = sin(m) 的那条线：
//        dx·ux + dy·uy = f·(sin m − uz)
//   这样"地平线"和"刻度梯"用同一个公式，必然自洽，且不存在万向节锁。
#include "UI_Horizon.h"

#include <math.h>
#include <string.h>

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "Config.h"

namespace ui {

namespace {

constexpr int W = 360, H = 360;
constexpr float CX = W / 2.0f, CY = H / 2.0f;
constexpr float VIS_R = 176.0f;  // 可视圆半径（圆形屏幕）
constexpr float FOCAL = 300.0f;  // 焦距：决定"每度多少像素"（≈5.2 px/°）
constexpr float LADDER_L = 72.0f;

constexpr float kRad = 3.14159265f / 180.0f;

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// 圆形裁剪：屏幕上可见区域是内切圆。任何绘制都不得越界，
// 否则刻度数字/线段被边缘切掉后会留下"一圈小白点"之类的碎片。
constexpr float CLIP_R = 174.0f;
constexpr float CLIP_R2 = CLIP_R * CLIP_R;
inline void putPx(uint16_t* fb, int x, int y, uint16_t c) {
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
  const float dx = (float)x - CX, dy = (float)y - CY;
  if (dx * dx + dy * dy > CLIP_R2) return;
  fb[y * W + x] = c;
}

void drawLine(uint16_t* fb, int x0, int y0, int x1, int y1, uint16_t c, int thick = 1) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    if (thick <= 1) {
      putPx(fb, x0, y0, c);
    } else {
      for (int oy = -thick / 2; oy <= thick / 2; oy++)
        for (int ox = -thick / 2; ox <= thick / 2; ox++) putPx(fb, x0 + ox, y0 + oy, c);
    }
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void fillCircle(uint16_t* fb, int cx, int cy, int r, uint16_t c) {
  for (int y = -r; y <= r; y++) {
    const int w = (int)sqrtf((float)(r * r - y * y));
    for (int x = -w; x <= w; x++) putPx(fb, cx + x, cy + y, c);
  }
}

void fillRect(uint16_t* fb, int x0, int y0, int x1, int y1, uint16_t c) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) putPx(fb, x, y, c);
}

// 4×4 Bayer 有序抖动矩阵（0..15）。用于消除 RGB565 渐变中的色带。
// 索引方式：kBay[[y&3]*4 + (x&3)]
const uint8_t kBay[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

// 3×5 点阵数字
const uint8_t kGlyph[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, {0b010, 0b110, 0b010, 0b010, 0b111},
    {0b111, 0b001, 0b111, 0b100, 0b111}, {0b111, 0b001, 0b111, 0b001, 0b111},
    {0b101, 0b101, 0b111, 0b001, 0b001}, {0b111, 0b100, 0b111, 0b001, 0b111},
    {0b111, 0b100, 0b111, 0b101, 0b111}, {0b111, 0b001, 0b010, 0b010, 0b010},
    {0b111, 0b101, 0b111, 0b101, 0b111}, {0b111, 0b101, 0b111, 0b001, 0b111},
};

// 在"a=向上、b=向右"的局部坐标系里画数字；ox/oy 为左上角（a 向上为正）
void drawDigit(uint16_t* fb, float aX, float aY, float bX, float bY, float cxp, float cyp, float ox,
               float oy, int digit, float scale, uint16_t col) {
  if (digit < 0 || digit > 9) return;
  for (int row = 0; row < 5; row++) {
    for (int c = 0; c < 3; c++) {
      if (!(kGlyph[digit][row] & (0b100 >> c))) continue;
      const float lb = ox + c * scale;
      const float la = oy - row * scale;
      const int ix = (int)lroundf(cxp + bX * lb + aX * la);
      const int iy = (int)lroundf(cyp + bY * lb + aY * la);
      const int s = (int)ceilf(scale);
      for (int dy = 0; dy < s; dy++)
        for (int dx = 0; dx < s; dx++) putPx(fb, ix + dx, iy + dy, col);
    }
  }
}

}  // namespace

bool HorizonView::create(lv_obj_t* parent) {
  _fb = (uint16_t*)heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM);
  if (!_fb) _fb = (uint16_t*)malloc((size_t)W * H * 2);
  if (!_fb) {
    DBG("[horizon] framebuffer alloc failed\n");
    return false;
  }
  memset(_fb, 0, (size_t)W * H * 2);

  _root = lv_obj_create(parent);
  lv_obj_remove_style_all(_root);
  lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(_root, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

  _canvas = lv_canvas_create(_root);
  lv_canvas_set_buffer(_canvas, _fb, W, H, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(_canvas, LV_ALIGN_CENTER, 0, 0);
  return true;
}

void HorizonView::destroy() {
  if (_root) {
    lv_obj_del(_root);
    _root = nullptr;
    _canvas = nullptr;
  }
  if (_fb) {
    free(_fb);
    _fb = nullptr;
  }
}

void HorizonView::setVisible(bool v) {
  if (!_root) return;
  if (v) {
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_root);
    if (_canvas) lv_obj_invalidate(_canvas);
  } else {
    lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
  }
}

// s.filtered 是【内容坐标系】下的重力方向（已含屏幕方向补偿），单位向量
void HorizonView::render(const att::State& st) {
  if (!_fb) return;

  // ---- 重力方向（单位向量）----
  // 上电初期滤波器还没收敛（filtered≈0）：此时不能直接归一化（会得到 NaN），
  // 否则刻度梯因 hypot 过小被跳过 → 出现一帧"没有刻度"的画面。
  // 兜底：有地平线基准就用基准（等价"水平"），否则用正上方。
  float gx0 = st.filtered.ax, gy0 = st.filtered.ay, gz0 = st.filtered.az;
  const float glen = sqrtf(gx0 * gx0 + gy0 * gy0 + gz0 * gz0);
  if (glen < 0.2f) {
    if (_refValid) {
      gx0 = _rx;
      gy0 = _ry;
      gz0 = _rz;
    } else {
      gx0 = 0.0f;
      gy0 = 1.0f;
      gz0 = 0.0f;
    }
  } else {
    gx0 /= glen;
    gy0 /= glen;
    gz0 /= glen;
  }
  att::Vec3 u = att::v3(gx0, gy0, gz0);

  // ---- 地平线基准（短按 BOOT 校准）：把当前重力旋转到"基准=正上方"----
  // 于是按短按时的那一姿态看，地平线正好居中水平（等价真实姿态仪的 cage 功能）。
  if (_refValid) {
    const float rl = sqrtf(_rx * _rx + _ry * _ry + _rz * _rz);
    if (rl > 1e-4f) {
      const float rx = _rx / rl, ry = _ry / rl, rz = _rz / rl;
      // 轴 = ref × (0,1,0) = (-rz, 0, rx)，角 = acos(ry)
      float kx = -rz, ky = 0.0f, kz = rx;
      const float kl = sqrtf(kx * kx + kz * kz);
      float ang = acosf(ry < -1.0f ? -1.0f : (ry > 1.0f ? 1.0f : ry));
      if (kl > 1e-4f) {
        kx /= kl;
        kz /= kl;
      } else {
        kx = 1.0f;
        kz = 0.0f;  // ref 几乎竖直且朝下：绕 X 轴转 180°
      }
      if (ang > 1e-4f) {
        const float ca2 = cosf(ang), sa2 = sinf(ang), ta2 = 1.0f - ca2;
        const float kd = kx * u.x + ky * u.y + kz * u.z;
        const float cx2 = ky * u.z - kz * u.y;
        const float cy2 = kz * u.x - kx * u.z;
        const float cz2 = kx * u.y - ky * u.x;
        u = att::v3(u.x * ca2 + cx2 * sa2 + kx * kd * ta2, u.y * ca2 + cy2 * sa2 + ky * kd * ta2,
                    u.z * ca2 + cz2 * sa2 + kz * kd * ta2);
      }
    }
  }

  float ux = u.x, uy = u.y;
  float uz = u.z;
  if (_flipXY) {  // 天地/左右整体翻转 180°（本板实测需要）
    ux = -ux;
    uy = -uy;
  }
  if (_flipPitch) uz = -uz;  // 只翻俯仰方向（抬头时地平线该往下）
  if (_flipRoll) ux = -ux;   // 只翻滚转方向（左右倾斜时地平线的转向）

  // 屏幕面内的"天空方向"（设备坐标，y 向上）
  const float nlen = sqrtf(ux * ux + uy * uy);
  float nx = 0.0f, ny = 1.0f;  // 若正对天/地（nlen≈0）则退化，取向上
  if (nlen > 1e-4f) {
    nx = ux / nlen;
    ny = uy / nlen;
  }

  const uint16_t cHorizon = rgb565(0xFF, 0xFF, 0xFF);

  // ---- 逐像素填天地 ----
  // 渐变做法：
  //   1) 用"视线仰角"而不是平面距离来取色 —— 天顶/天底才到最深，过渡更自然；
  //      为省算力用 u·(1-0.28u²) 近似 sin(atan(u))，避免逐像素开方。
  //   2) smoothstep 缓动，地平线附近变化平缓、远处收得快。
  //   3) 4×4 Bayer 抖动：RGB565 只有 5-6-5 位，不抖动会出现明显色带。
  const float kEdgeU = (float)VIS_R / FOCAL;
  const float kEdgeS = kEdgeU * (1.0f - 0.28f * kEdgeU * kEdgeU);  // 边缘处的仰角归一化因子
  const float kInvEdgeS = (kEdgeS > 1e-3f) ? (1.0f / kEdgeS) : 1.0f;

  // 配色：地平线附近偏亮偏淡，越远离地平线越深（天：浅蓝→深蓝；地：暖棕→暗褐）
  const float skyR0 = 0xA6, skyG0 = 0xD0, skyB0 = 0xEC;  // 天：地平线
  const float skyR1 = 0x11, skyG1 = 0x4C, skyB1 = 0x96;  // 天：天顶
  const float gndR0 = 0xA2, gndG0 = 0x7A, gndB0 = 0x4C;  // 地：地平线
  const float gndR1 = 0x36, gndG1 = 0x24, gndB1 = 0x13;  // 地：天底

  for (int y = 0; y < H; y++) {
    const int dyDev = (int)CY - y;  // 设备坐标 y 向上
    if (fabsf((float)dyDev) > VIS_R) continue;
    const int halfW = (int)sqrtf(VIS_R * VIS_R - (float)dyDev * dyDev);
    uint16_t* row = _fb + y * W;
    const int bayerRow = (y & 3) << 2;
    for (int x = (int)CX - halfW; x <= (int)CX + halfW; x++) {
      const int dxDev = x - (int)CX;
      const float d = dxDev * ux + dyDev * uy + FOCAL * uz;
      const float uu = d / FOCAL;
      const float s = uu * (1.0f - 0.28f * uu * uu);  // ≈ sin(仰角)

      float t = fabsf(s) * kInvEdgeS;
      if (t > 1.0f) t = 1.0f;
      const float e = t * t * (3.0f - 2.0f * t);  // smoothstep

      float r, g, b;
      if (s >= 0) {  // 天空
        r = skyR0 + (skyR1 - skyR0) * e;
        g = skyG0 + (skyG1 - skyG0) * e;
        b = skyB0 + (skyB1 - skyB0) * e;
      } else {  // 地面
        r = gndR0 + (gndR1 - gndR0) * e;
        g = gndG0 + (gndG1 - gndG0) * e;
        b = gndB0 + (gndB1 - gndB0) * e;
      }

      // 4×4 Bayer 抖动（±4/255，正好覆盖一个 R/B 量化步长）
      const int dith = kBay[bayerRow | (x & 3)] - 8;  // -8..7
      const float dq = dith * 0.5f;
      int ri = (int)(r + dq);
      int gi = (int)(g + dq);
      int bi = (int)(b + dq);
      if (ri < 0) ri = 0; if (ri > 255) ri = 255;
      if (gi < 0) gi = 0; if (gi > 255) gi = 255;
      if (bi < 0) bi = 0; if (bi > 255) bi = 255;
      row[x] = rgb565((uint8_t)ri, (uint8_t)gi, (uint8_t)bi);
    }
  }

  // ---- 地平线：d≈0 的那一圈，直接复用上面的判据 ----
  for (int y = 0; y < H; y++) {
    const int dyDev = (int)CY - y;
    if (fabsf((float)dyDev) > VIS_R) continue;
    const int halfW = (int)sqrtf(VIS_R * VIS_R - (float)dyDev * dyDev);
    for (int x = (int)CX - halfW; x <= (int)CX + halfW; x++) {
      const int dxDev = x - (int)CX;
      const float d = dxDev * ux + dyDev * uy + FOCAL * uz;
      if (fabsf(d) < 5.0f) putPx(_fb, x, y, cHorizon);
    }
  }

  // ---- 俯仰刻度梯：dx·ux + dy·uy = f·(sin m − uz) ----
  const float hypot = nlen;
  if (hypot > 0.08f) {
    const float aX = nx, aY = ny;   // 天空方向（局部 a 轴）
    const float bX = -ny, bY = nx;  // 右方向（局部 b 轴）
    for (int m = -30; m <= 30; m += 10) {
      if (m == 0) continue;
      const float c = FOCAL * (sinf(m * kRad) - uz);
      // 线：ux·dx + uy·dy = c，法向 (ux,uy)，偏移量 c/hypot
      const float off = c / hypot;                     // 沿 n̂ 的距离（像素）
      if (fabsf(off) > VIS_R - 34.0f) continue;        // 超出可视区就不画（含数字）
      const float cx0 = CX + nx * off, cy0 = CY - ny * off;
      const float len = (m % 20 == 0) ? LADDER_L : (LADDER_L * 0.55f);
      const float gap = (m % 20 == 0) ? 26.0f : 18.0f;
      // 屏幕上的切向
      const float tx = -ny, ty = -nx;  // 注意：屏幕 y 向下
      drawLine(_fb, (int)lroundf(cx0 - tx * len), (int)lroundf(cy0 - ty * len),
               (int)lroundf(cx0 - tx * gap), (int)lroundf(cy0 - ty * gap), cHorizon, 1);
      drawLine(_fb, (int)lroundf(cx0 + tx * gap), (int)lroundf(cy0 + ty * gap),
               (int)lroundf(cx0 + tx * len), (int)lroundf(cy0 + ty * len), cHorizon, 1);

      // 数字（两端，随刻度线朝向旋转；屏幕坐标 a=向上 → (aX, -aY)）
      const int v = (m < 0) ? -m : m;
      const float sc = 2.0f;
      const float sA = nx, sB = -ny;
      for (int side = -1; side <= 1; side += 2) {
        const float bx = cx0 + tx * side * (len + 6);
        const float by = cy0 + ty * side * (len + 6);
        const float ox = (side < 0) ? -(v >= 10 ? 8 * sc : 5 * sc) : 2 * sc;
        if (v >= 10) {
          drawDigit(_fb, sA, sB, -sB, sA, bx, by, ox, 2 * sc, v / 10, sc, cHorizon);
          drawDigit(_fb, sA, sB, -sB, sA, bx, by, ox + 4 * sc, 2 * sc, v % 10, sc, cHorizon);
        } else {
          drawDigit(_fb, sA, sB, -sB, sA, bx, by, ox + 2 * sc, 2 * sc, v, sc, cHorizon);
        }
      }
    }
  }

  // ---- 顶部滚转刻度弧（固定在壳体上，作为度量基准）----
  for (int deg = -60; deg <= 60; deg += 10) {
    const float a = (-90.0f + deg) * kRad;
    const float ca = cosf(a), sa = sinf(a);
    const bool major = (deg % 30 == 0);
    const float r0 = VIS_R - (major ? 22 : 14), r1 = VIS_R - 4;
    drawLine(_fb, (int)lroundf(CX + ca * r0), (int)lroundf(CY + sa * r0),
             (int)lroundf(CX + ca * r1), (int)lroundf(CY + sa * r1), cHorizon, major ? 2 : 1);
  }

  // ---- 滚转指针（红色实心三角）：随姿态沿刻度弧滑动 ----
  // 根部贴齐刻度的内边缘，尖端指向圆心；0° 滚转时停在正上方。
  {
    const uint16_t cRed = rgb565(0xE8, 0x3C, 0x3C);
    const float ang = atan2f(-uy, ux);  // 屏幕坐标（y 向下）
    const float ca = cosf(ang), sa = sinf(ang);
    const float tx = -sa, ty = ca;      // 切向
    const float rTip = VIS_R - 22.0f;   // 尖端：贴齐刻度内边缘（朝外/朝上）
    const float len = 14.0f;            // 三角长度（根部在内侧）
    for (float rr = rTip; rr >= rTip - len; rr -= 1.0f) {
      const float t = (rTip - rr) / len;           // 0 = 尖端，1 = 根部
      const int half = (int)lroundf(6.0f * t + 0.4f);
      const float bx = CX + ca * rr, by = CY + sa * rr;
      for (int k = -half; k <= half; k++) {
        putPx(_fb, (int)lroundf(bx + tx * k), (int)lroundf(by + ty * k), cRed);
      }
    }
  }

  // ---- 中心飞机符号（固定，黄色）----
  const uint16_t cAir = rgb565(0xF5, 0xC5, 0x00);
  fillCircle(_fb, (int)CX, (int)CY, 5, cAir);
  fillRect(_fb, (int)CX - 58, (int)CY - 3, (int)CX - 16, (int)CY + 2, cAir);
  fillRect(_fb, (int)CX + 16, (int)CY - 3, (int)CX + 58, (int)CY + 2, cAir);
  fillRect(_fb, (int)CX - 60, (int)CY - 16, (int)CX - 54, (int)CY + 2, cAir);
  fillRect(_fb, (int)CX + 54, (int)CY - 16, (int)CX + 60, (int)CY + 2, cAir);
  fillRect(_fb, (int)CX - 3, (int)CY, (int)CX + 2, (int)CY + 70, cAir);
  for (int i = 0; i < 22; i++) {
    const int half = 12 + i / 2;
    fillRect(_fb, (int)CX - half, (int)CY + 74 + i, (int)CX + half, (int)CY + 74 + i, cAir);
  }

  // ---- 亮度叠加条（调光时临时显示）----
  if (_ovPct >= 0 && (int32_t)(_ovUntil - millis()) > 0) {
    const int barW = 150, barH = 12;
    const int x0 = (int)CX - barW / 2;
    const int y0 = (int)CY + 118;
    const uint16_t cEdge = rgb565(0xFF, 0xFF, 0xFF);
    const uint16_t cFill = rgb565(0xF5, 0xC5, 0x00);
    for (int x = x0; x <= x0 + barW; x++) {
      putPx(_fb, x, y0, cEdge);
      putPx(_fb, x, y0 + barH, cEdge);
    }
    for (int y = y0; y <= y0 + barH; y++) {
      putPx(_fb, x0, y, cEdge);
      putPx(_fb, x0 + barW, y, cEdge);
    }
    const int fill = (barW - 4) * (_ovPct > 100 ? 100 : _ovPct) / 100;
    if (fill > 0) fillRect(_fb, x0 + 2, y0 + 2, x0 + 1 + fill, y0 + barH - 2, cFill);
  }
}

void HorizonView::showOverlay(int pct, uint32_t durationMs) {  _ovPct = pct;
  _ovUntil = millis() + durationMs;
  _forceRender = true;
}

void HorizonView::dumpToSerial() {
  if (!_fb) return;
  Serial.printf("[shot] %d %d\n", W, H);
  Serial.flush();
  const size_t total = (size_t)W * H * 2;
  const uint8_t* p = (const uint8_t*)_fb;
  for (size_t off = 0; off < total; off += 4096) {
    const size_t n = (total - off < 4096) ? (total - off) : 4096;
    Serial.write(p + off, n);
    Serial.flush();
    delay(2);
  }
  Serial.printf("\n[shot] done\n");
  Serial.flush();
}

void HorizonView::update(const att::State& s, const app::RuntimeStatus& st) {
  (void)st;
  const uint32_t now = millis();
  const bool ovActive = (_ovPct >= 0) && ((int32_t)(_ovUntil - now) > 0);
  if (_ovPct >= 0 && !ovActive) {  // 叠加层到期：重绘一次把它清掉
    _ovPct = -1;
    _forceRender = true;
  }

  // 用 0.1° 量化做刷新门限（远处 1° 一下也够细）
  const int ax10 = (int)lroundf(s.filtered.ax * 200.0f);
  const int ay10 = (int)lroundf(s.filtered.ay * 200.0f);
  if (!ovActive && !_forceRender && ax10 == _lastPitch10 && ay10 == _lastRoll10) return;
  _lastPitch10 = ax10;
  _lastRoll10 = ay10;
  _forceRender = false;

  render(s);
  if (_canvas) lv_obj_invalidate(_canvas);
}

}  // namespace ui
