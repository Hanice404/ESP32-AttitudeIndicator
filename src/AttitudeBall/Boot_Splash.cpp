// Boot_Splash.cpp —— 开机 Logo 与渐隐实现
#include "Boot_Splash.h"

#include <string.h>

#include <esp_heap_caps.h>

#include "Boot_Logo.h"
#include "Config.h"
#include "Display_ST77916.h"

namespace splash {

namespace {

constexpr int W = 360;
constexpr int H = 360;
constexpr int BAND_H = 36;  // 每条带的像素行数：36×360×2 = 25.9 KB（放内部 RAM）

uint16_t* s_logo = nullptr;  // PSRAM：解码后的 Logo
uint16_t* s_band = nullptr;  // 内部 RAM：发送带缓冲

// RGB565 线性混合：w=0 全 a，w=256 全 b
inline uint16_t blend565(uint16_t a, uint16_t b, int w) {
  const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int iw = 256 - w;
  const int r = (ar * iw + br * w) >> 8;
  const int g = (ag * iw + bg * w) >> 8;
  const int bl = (ab * iw + bb * w) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// 把 s_band 里的若干行推到屏幕
void pushBand(int y0, int rows) {
  LCD_addWindow(0, (uint16_t)y0, (uint16_t)(W - 1), (uint16_t)(y0 + rows - 1), s_band);
  LCD_WaitFlush();
}

}  // namespace

bool begin() {
  if (s_logo) return true;

  s_band = (uint16_t*)heap_caps_malloc((size_t)W * BAND_H * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!s_band) {
    DBG("[splash] band buffer alloc failed\n");
    return false;
  }

  s_logo = (uint16_t*)heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM);
  if (!s_logo) s_logo = (uint16_t*)malloc((size_t)W * H * 2);
  if (!s_logo) {
    DBG("[splash] logo buffer alloc failed\n");
    free(s_band);
    s_band = nullptr;
    return false;
  }

  // RLE 解码：(次数, 颜色) 成对存放
  uint32_t total = 0;
  for (uint32_t i = 0; i + 1 < bootlogo::kRleLen; i += 2) {
    uint32_t n = bootlogo::kRle[i];
    const uint16_t c = (uint16_t)(bootlogo::kRle[i + 1] & 0xFFFF);
    if (total + n > (uint32_t)(W * H)) n = (uint32_t)(W * H) - total;
    for (uint32_t k = 0; k < n; k++) s_logo[total + k] = c;
    total += n;
    if (total >= (uint32_t)(W * H)) break;
  }
  DBG("[splash] logo decoded (%lu px, rle %lu)\n", (unsigned long)total,
      (unsigned long)bootlogo::kRleLen);
  return total >= (uint32_t)(W * H);
}

void showLogo() {
  if (!s_logo || !s_band) return;
  for (int y = 0; y < H; y += BAND_H) {
    const int rows = (y + BAND_H <= H) ? BAND_H : (H - y);
    memcpy(s_band, s_logo + (size_t)y * W, (size_t)rows * W * 2);
    pushBand(y, rows);
  }
}

void crossfade(const uint16_t* ui, uint32_t totalMs) {
  if (!s_logo || !s_band) return;
  if (totalMs < 16) totalMs = 16;

  const uint32_t t0 = millis();
  for (;;) {
    uint32_t el = millis() - t0;
    int w = (int)((uint64_t)el * 256ULL / totalMs);
    if (w > 256) w = 256;

    for (int y = 0; y < H; y += BAND_H) {
      const int rows = (y + BAND_H <= H) ? BAND_H : (H - y);
      const uint16_t* src = s_logo + (size_t)y * W;
      uint16_t* dst = s_band;
      if (ui) {
        const uint16_t* u = ui + (size_t)y * W;
        const int n = rows * W;
        for (int i = 0; i < n; i++) dst[i] = blend565(src[i], u[i], w);
      } else {
        memcpy(dst, src, (size_t)rows * W * 2);
      }
      pushBand(y, rows);
    }

    if (w >= 256) break;
  }
  DBG("[splash] fade done in %lu ms\n", (unsigned long)(millis() - t0));
}

}  // namespace splash

