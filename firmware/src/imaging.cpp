#include "imaging.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <initializer_list>
using std::min;

#include <JPEGDEC.h>

#include "red_jpeg.h"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
static void *bigAlloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
#else
static void *bigAlloc(size_t n) { return malloc(n); }
#endif

namespace img {

// Gemeten paneelkleuren (zelfde als server/epaper.py), index = kleurcode van het scherm.
static const uint8_t PANEL[7][3] = {
    {24, 25, 65},     // zwart (blauwig)
    {235, 235, 235},  // wit
    {44, 92, 61},     // groen
    {19, 24, 92},     // blauw
    {125, 25, 29},    // rood
    {220, 186, 27},   // geel
    {202, 131, 51},   // oranje
};

// ------------------------------------------------------------------ JPEG

static JPEGDEC *decoder() {
  static JPEGDEC *d = nullptr;     // ~20 KB; eenmalig, liefst niet op de stack
  if (!d) d = new JPEGDEC();
  return d;
}

bool jpegSize(const uint8_t *data, size_t len, int &w, int &h) {
  JPEGDEC *j = decoder();
  if (!j->openRAM((uint8_t *)data, (int)len, nullptr)) return false;
  w = j->getWidth();
  h = j->getHeight();
  bool ok = j->getJPEGType() != JPEG_MODE_PROGRESSIVE;
  j->close();
  return ok;
}

// Afhankelijk van de interne route (C, SSE, ESP32-S3-SIMD) levert de decoder RGB8888 als
// R,G,B,A of als B,G,R,A. Per schaal wordt dat eenmalig bepaald met een rode test-JPEG.
static int8_t bgrForScale[4] = {-1, -1, -1, -1};   // index: 0=1/1, 1=1/2, 2=1/4, 3=1/8
static bool curBgr = false;
static int curRotation = 0;
static int srcW = 0, srcH = 0;   // afmetingen vóór draaien (na schalen)

static int probeCb(JPEGDRAW *d) {
  uint32_t p = ((const uint32_t *)d->pPixels)[0];
  *(bool *)d->pUser = ((p >> 16) & 0xFF) > (p & 0xFF);   // blauwe plek groter: B,G,R
  return 0;                                                // na het eerste blok stoppen
}

static int scaleIndex(int scale) { return scale == 8 ? 3 : scale == 4 ? 2 : scale == 2 ? 1 : 0; }
static int scaleOpt(int scale) {
  return scale == 8 ? JPEG_SCALE_EIGHTH : scale == 4 ? JPEG_SCALE_QUARTER
       : scale == 2 ? JPEG_SCALE_HALF : 0;
}

static bool isBgr(int scale) {
  int8_t &v = bgrForScale[scaleIndex(scale)];
  if (v < 0) {
    bool bgr = false;
    JPEGDEC *j = decoder();
    if (j->openRAM((uint8_t *)RED_JPEG, sizeof(RED_JPEG), probeCb)) {
      j->setPixelType(RGB8888);
      j->setUserPointer(&bgr);
      j->decode(0, 0, scaleOpt(scale));
      j->close();
    }
    v = bgr;
  }
  return v;
}

static inline void putPixel(Image *im, uint32_t p, int tx, int ty, int ri, int bi) {
  uint8_t *o = im->rgb + ((size_t)ty * im->w + tx) * 3;
  o[ri] = p & 0xFF;
  o[1] = (p >> 8) & 0xFF;
  o[bi] = (p >> 16) & 0xFF;
}

static int drawCb(JPEGDRAW *d) {
  Image *im = (Image *)d->pUser;
  const int ri = curBgr ? 2 : 0, bi = curBgr ? 0 : 2;
  const uint32_t *px = (const uint32_t *)d->pPixels;
  const int hMax = min(d->iHeight, srcH - d->y), wMax = min(d->iWidthUsed, srcW - d->x);
  if (curRotation == 0) {
    for (int y = 0; y < hMax; y++)
      for (int x = 0; x < wMax; x++)
        putPixel(im, px[y * d->iWidth + x], d->x + x, d->y + y, ri, bi);
  } else {
    // Gedraaid (zoals Pillow's rotate, tegen de klok in). Kolom voor kolom door het blok,
    // zodat de schrijfacties in het doelbeeld naast elkaar liggen (PSRAM-cache).
    for (int x = 0; x < wMax; x++) {
      int sx = d->x + x;
      for (int y = 0; y < hMax; y++) {
        int sy = d->y + y;
        int tx = curRotation == 90 ? sy : srcH - 1 - sy;
        int ty = curRotation == 90 ? srcW - 1 - sx : sx;
        putPixel(im, px[y * d->iWidth + x], tx, ty, ri, bi);
      }
    }
  }
  return 1;
}

bool decodeJpeg(const uint8_t *data, size_t len, int rotation, Image &out) {
  int w, h;
  if (!jpegSize(data, len, w, h)) return false;
  const bool swap = rotation == 90 || rotation == 270;
  const int minW = swap ? H : W, minH = swap ? W : H;
  int scale = 1;
  for (int s : {8, 4, 2}) {
    if (w / s >= minW && h / s >= minH) {
      scale = s;
      break;
    }
  }
  curBgr = isBgr(scale);
  JPEGDEC *j = decoder();
  if (!j->openRAM((uint8_t *)data, (int)len, drawCb)) return false;
  srcW = (w + scale - 1) / scale;
  srcH = (h + scale - 1) / scale;
  curRotation = rotation;
  out.w = swap ? srcH : srcW;
  out.h = swap ? srcW : srcH;
  out.rgb = (uint8_t *)bigAlloc((size_t)out.w * out.h * 3);
  if (!out.rgb) {
    j->close();
    return false;
  }
  memset(out.rgb, 0, (size_t)out.w * out.h * 3);
  j->setPixelType(RGB8888);
  j->setUserPointer(&out);
  bool ok = j->decode(0, 0, scaleOpt(scale)) == 1;
  j->close();
  if (!ok) freeImage(out);
  return ok;
}

void freeImage(Image &im) {
  free(im.rgb);
  im.rgb = nullptr;
  im.w = im.h = 0;
}

// ------------------------------------------------------------------ verwerking

static inline uint8_t clip(float v) {
  return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t)(v + 0.5f);
}

static inline int nearest(float r, float g, float b) {
  int best = 0;
  float bestD = 1e30f;
  for (int k = 0; k < 7; k++) {
    float dr = r - PANEL[k][0], dg = g - PANEL[k][1], db = b - PANEL[k][2];
    float d = dr * dr + dg * dg + db * db;
    if (d < bestD) { bestD = d; best = k; }
  }
  return best;
}

bool render(const Image &src, float centerX, float centerY, const Params &p, uint8_t *frame,
            uint8_t *preview) {
  if (!src.rgb) return false;

  // Bijsnijden zoals ImageOps.fit: grootste uitsnede met de verhouding van het scherm.
  double target = (double)W / H, have = (double)src.w / src.h;
  double cw = src.w, ch = src.h;
  if (have > target) cw = src.h * target;
  else ch = src.w / target;
  const double cx0 = (src.w - cw) * centerX, cy0 = (src.h - ch) * centerY;
  const double fx = cw / W, fy = ch / H;

  // Kolomgrenzen van de middeling zijn voor elke rij gelijk.
  static int16_t xs0[W], xs1[W];
  for (int x = 0; x < W; x++) {
    int x0 = (int)(cx0 + x * fx), x1 = (int)(cx0 + (x + 1) * fx);
    if (x1 <= x0) x1 = x0 + 1;
    if (x1 > src.w) x1 = src.w;
    xs0[x] = x0;
    xs1[x] = x1;
  }

  // Stap 1: verkleinen door middeling + verzadiging (Pillow ImageEnhance.Color), als bytes.
  uint8_t *buf = (uint8_t *)bigAlloc((size_t)W * H * 3);
  if (!buf) return false;
  uint64_t lsum = 0;   // som van L*1000
  for (int y = 0; y < H; y++) {
    int y0 = (int)(cy0 + y * fy), y1 = (int)(cy0 + (y + 1) * fy);
    if (y1 <= y0) y1 = y0 + 1;
    if (y1 > src.h) y1 = src.h;
    uint8_t *out = buf + (size_t)y * W * 3;
    for (int x = 0; x < W; x++) {
      uint32_t r = 0, g = 0, b = 0;
      for (int yy = y0; yy < y1; yy++) {
        const uint8_t *q = src.rgb + ((size_t)yy * src.w + xs0[x]) * 3;
        for (int xx = xs0[x]; xx < xs1[x]; xx++, q += 3) {
          r += q[0]; g += q[1]; b += q[2];
        }
      }
      // Geen float-delingen: die gaan op de ESP32 in software en zijn traag.
      float inv = 1.0f / (float)((y1 - y0) * (xs1[x] - xs0[x]));
      float fr = clip(r * inv), fg = clip(g * inv), fb = clip(b * inv);
      float l = fr * 0.299f + fg * 0.587f + fb * 0.114f;
      uint8_t *o = out + x * 3;
      o[0] = clip(l + p.saturation * (fr - l));
      o[1] = clip(l + p.saturation * (fg - l));
      o[2] = clip(l + p.saturation * (fb - l));
      lsum += (uint32_t)(o[0] * 299 + o[1] * 587 + o[2] * 114);
    }
  }
  const float mean = (float)(int)(lsum / 1000.0 / ((double)W * H) + 0.5);

  // Stap 2: per rij contrast, helderheid en warmte, daarna Floyd-Steinberg tegen de
  // paneelkleuren. De fout van de huidige en volgende rij staat in kleine buffers.
  static float errA[(W + 2) * 3], errB[(W + 2) * 3];
  float *errCur = errA, *errNext = errB;
  memset(errA, 0, sizeof errA);
  memset(errB, 0, sizeof errB);
  memset(frame, 0, FRAME_BYTES);
  for (int y = 0; y < H; y++) {
    const uint8_t *row = buf + (size_t)y * W * 3;
    for (int x = 0; x < W; x++) {
      float c[3];
      for (int k = 0; k < 3; k++) {
        float v = clip(mean + p.contrast * ((float)row[x * 3 + k] - mean));
        c[k] = clip(v * p.brightness);
      }
      if (p.warmth != 0) {
        c[0] = c[0] * (1 + p.warmth) > 255 ? 255 : (float)(int)(c[0] * (1 + p.warmth));
        c[2] = (float)(int)(c[2] * (1 - p.warmth / 2));
      }
      float *e = errCur + (x + 1) * 3;
      float r = clip(c[0] + e[0]), g = clip(c[1] + e[1]), b = clip(c[2] + e[2]);
      int best = nearest(r, g, b);
      float er = r - PANEL[best][0], eg = g - PANEL[best][1], eb = b - PANEL[best][2];
      float *right = e + 3, *dl = errNext + x * 3, *dn = dl + 3, *dr = dn + 3;
      constexpr float F7 = 7 / 16.0f, F3 = 3 / 16.0f, F5 = 5 / 16.0f, F1 = 1 / 16.0f;
      right[0] += er * F7; right[1] += eg * F7; right[2] += eb * F7;
      dl[0] += er * F3;    dl[1] += eg * F3;    dl[2] += eb * F3;
      dn[0] += er * F5;    dn[1] += eg * F5;    dn[2] += eb * F5;
      dr[0] += er * F1;    dr[1] += eg * F1;    dr[2] += eb * F1;

      size_t i = (size_t)y * W + x;
      frame[i / 2] |= (i & 1) ? best : (best << 4);
      if (preview) memcpy(preview + i * 3, PANEL[best], 3);
    }
    float *t = errCur;
    errCur = errNext;
    errNext = t;
    memset(errNext, 0, sizeof errA);
  }
  free(buf);
  return true;
}

}  // namespace img
