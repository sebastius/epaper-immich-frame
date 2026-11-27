// Host-test van de beeldverwerking: decodeert een JPEG en schrijft een preview (PPM)
// en het ruwe frame. Bouwen: zie test/host/build.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "imaging.h"

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "gebruik: %s in.jpg uit.ppm uit.bin [rotatie]\n", argv[0]); return 2; }
  int rotation = argc > 4 ? atoi(argv[4]) : 0;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 1;
  std::vector<uint8_t> data;
  uint8_t tmp[65536]; size_t n;
  while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) data.insert(data.end(), tmp, tmp + n);
  fclose(f);
  int w, h;
  if (!img::jpegSize(data.data(), data.size(), w, h)) { fprintf(stderr, "geen bruikbare JPEG\n"); return 1; }
  img::Image im;
  if (!img::decodeJpeg(data.data(), data.size(), rotation, im)) {
    fprintf(stderr, "decoderen mislukt\n"); return 1;
  }
  printf("jpeg %dx%d -> gedecodeerd %dx%d\n", w, h, im.w, im.h);
  float cx = 0.5f, cy = 0.35f;
  if (rotation == 90) { cx = 0.35f; cy = 0.5f; } else if (rotation == 270) { cx = 0.65f; cy = 0.5f; }
  std::vector<uint8_t> frame(img::FRAME_BYTES), prev(img::W * img::H * 3);
  img::Params p;
  if (!img::render(im, cx, cy, p, frame.data(), prev.data())) return 1;
  f = fopen(argv[2], "wb"); fprintf(f, "P6\n%d %d\n255\n", img::W, img::H); fwrite(prev.data(), 1, prev.size(), f); fclose(f);
  f = fopen(argv[3], "wb"); fwrite(frame.data(), 1, frame.size(), f); fclose(f);
  img::freeImage(im);
  return 0;
}
