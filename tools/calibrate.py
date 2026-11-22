#!/usr/bin/env python3
"""Kalibratie van de paneelkleuren.

  calibrate.py show --send /dev/ttyACM0     toon 8 kleurvlakken op het scherm
  calibrate.py measure foto.jpg x0 y0 x1 y1 meet de vlakken in een foto van het scherm;
                                            (x0,y0)-(x1,y1) = hoeken van het beeldvlak in de foto

Indeling (4 kolommen x 2 rijen):
  zwart  wit    groen   blauw
  rood   geel   oranje  schaakbord (zwart/wit, grijsreferentie)
"""
import sys

from convert import H, W, send

NAMES = ["zwart", "wit", "groen", "blauw", "rood", "geel", "oranje", "schaakbord"]
COLS, ROWS = 4, 2
MARGIN = 12  # witte rand tussen de vlakken


def build(label=None):
    px = bytearray([1]) * (W * H)  # wit
    cw, ch = W // COLS, H // ROWS
    for idx in range(8):
        cx, cy = (idx % COLS) * cw, (idx // COLS) * ch
        for y in range(cy + MARGIN, cy + ch - MARGIN):
            for x in range(cx + MARGIN, cx + cw - MARGIN):
                px[y * W + x] = ((x + y) & 1) if idx == 7 else idx
    if label:
        # zwarte letter onderin het witte vlak, buiten het meetgebied (middelste 50%)
        from PIL import Image, ImageDraw, ImageFont

        txt = Image.new("1", (W, H), 0)
        ImageDraw.Draw(txt).text((cw + 20, ch - 58), label, fill=1,
                                 font=ImageFont.load_default(size=40))
        for i, on in enumerate(txt.getdata()):
            if on:
                px[i] = 0
    out = bytearray(W * H // 2)
    for i in range(0, len(px), 2):
        out[i // 2] = (px[i] << 4) | px[i + 1]
    return bytes(out)


def measure(path, x0, y0, x1, y1):
    from PIL import Image, ImageOps

    img = ImageOps.exif_transpose(Image.open(path)).convert("RGB")
    sx, sy = (x1 - x0) / W, (y1 - y0) / H
    cw, ch = W // COLS, H // ROWS
    res = {}
    for idx, name in enumerate(NAMES):
        cx, cy = (idx % COLS) * cw, (idx // COLS) * ch
        # middelste 50% van het vlak, om randen en schaduw te vermijden
        bx0, by0 = cx + cw * 0.25, cy + ch * 0.25
        bx1, by1 = cx + cw * 0.75, cy + ch * 0.75
        box = (int(x0 + bx0 * sx), int(y0 + by0 * sy), int(x0 + bx1 * sx), int(y0 + by1 * sy))
        crop = img.crop(box).resize((1, 1), Image.Resampling.BOX)
        res[name] = crop.getpixel((0, 0))
        print(f"{name:11s} {res[name]}  (vlak {box})")
    return res


if __name__ == "__main__":
    if sys.argv[1] == "show":
        ok = send(sys.argv[sys.argv.index("--send") + 1], build())
        sys.exit(0 if ok else 1)
    elif sys.argv[1] == "measure":
        measure(sys.argv[2], *map(int, sys.argv[3:7]))
