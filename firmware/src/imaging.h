// Beeldverwerking: JPEG -> 600x448, 7 paneelkleuren, 4 bit per pixel.
// Platformonafhankelijk (ook op de pc te bouwen voor tests); doet hetzelfde als
// tools/epaper.py.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace img {

constexpr int W = 600;
constexpr int H = 448;
constexpr size_t FRAME_BYTES = W * H / 2;

struct Image {
  int w = 0, h = 0;
  uint8_t *rgb = nullptr;   // w*h*3 bytes
};

struct Params {
  float saturation = 1.5f;
  float contrast = 1.2f;
  float brightness = 1.0f;
  float warmth = 0.0f;
};

// Leest alleen de JPEG-header; false bij een onbruikbaar bestand (geen JPEG, progressief).
bool jpegSize(const uint8_t *data, size_t len, int &w, int &h);

// Decodeert een JPEG en draait hem meteen (0, 90 of 270 graden tegen de klok in, zoals
// Pillow's rotate). De decoder verkleint zo veel mogelijk (1/2, 1/4, 1/8) zolang het
// gedraaide resultaat het scherm (600x448) nog vult. `out.rgb` wordt gealloceerd (PSRAM
// op de ESP32); vrijgeven met freeImage().
bool decodeJpeg(const uint8_t *data, size_t len, int rotation, Image &out);
void freeImage(Image &im);

// Snijdt bij en verkleint tot 600x448, past kleurbewerking toe, dithert en schrijft
// FRAME_BYTES naar `frame`. centerX/centerY: waar bijgesneden wordt (0..1).
// `preview` (optioneel, W*H*3 bytes) krijgt het geditherde beeld in paneelkleuren.
bool render(const Image &src, float centerX, float centerY, const Params &p, uint8_t *frame,
            uint8_t *preview = nullptr);

}  // namespace img
