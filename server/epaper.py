"""Omzetting van foto's naar het 7-kleuren ACeP-formaat (600x448, 4 bit/pixel)."""
from PIL import Image, ImageEnhance, ImageOps

W, H = 600, 448
FRAME_BYTES = W * H // 2

# Gemeten kleuren van het paneel (kalibratiefoto met tools/calibrate.py, 2026-11-22).
# Witgebalanceerd op het paneel-wit en lineair uitgerekt tussen zwart en wit.
# Dithering tegen de echte paneelkleuren geeft een getrouwer resultaat.
# Volgorde = kleurcode van het scherm: zwart, wit, groen, blauw, rood, geel, oranje.
PANEL = [
    (24, 25, 65),      # zwart: duidelijk blauwpaars
    (235, 235, 235),
    (44, 92, 61),
    (19, 24, 92),
    (125, 25, 29),
    (220, 186, 27),
    (202, 131, 51),
]

_palette = Image.new("P", (1, 1))
_flat = [v for c in PANEL for v in c]
_palette.putpalette(_flat + _flat[:3] * (256 - len(PANEL)))


def convert(src, saturation=1.5, contrast=1.2, brightness=1.0, warmth=0.0,
            centering=(0.5, 0.35)):
    """src: pad, bestandsobject of PIL-beeld. Geeft een P-beeld met kleurcodes 0..6.

    centering: waar bijgesneden wordt (0..1 per as); standaard iets boven het midden,
    zodat hoofden in beeld blijven.
    """
    img = src if isinstance(src, Image.Image) else Image.open(src)
    img = ImageOps.exif_transpose(img).convert("RGB")
    img = ImageOps.fit(img, (W, H), Image.Resampling.LANCZOS, centering=centering)
    img = ImageEnhance.Color(img).enhance(saturation)
    img = ImageEnhance.Contrast(img).enhance(contrast)
    img = ImageEnhance.Brightness(img).enhance(brightness)
    if warmth:
        r, g, b = img.split()
        r = r.point(lambda v: min(255, int(v * (1 + warmth))))
        b = b.point(lambda v: int(v * (1 - warmth / 2)))
        img = Image.merge("RGB", (r, g, b))
    return img.quantize(palette=_palette, dither=Image.Dither.FLOYDSTEINBERG)


def pack(q):
    """P-beeld -> ruwe framebytes (2 pixels per byte, hoge nibble eerst)."""
    px = q.tobytes()
    return bytes((a << 4) | b for a, b in zip(px[0::2], px[1::2]))


def preview(q):
    """P-beeld -> RGB-beeld zoals het (ongeveer) op het paneel oogt."""
    return q.convert("RGB")
