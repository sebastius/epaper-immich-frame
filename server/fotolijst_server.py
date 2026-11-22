#!/usr/bin/env python3
"""Fotolijst-server: kiest een foto uit een Immich-album (of een map), zet die om
voor het 7-kleuren e-paper en levert hem aan de Pico W.

Endpoints:
  GET /frame        nieuwe foto als ruwe framebytes (134400 bytes)
                    header X-Sleep-Seconds: hoe lang de Pico daarna moet slapen
  GET /preview.png  hoe de laatst geleverde foto er (ongeveer) uitziet op het paneel
  GET /health       "ok"

Configuratie via omgevingsvariabelen, of een bestand .env naast dit script
(regels KEY=waarde; echte omgevingsvariabelen gaan voor):
  IMMICH_URL        bv. https://photos.example.net   (leeg = PHOTO_DIR gebruiken)
  IMMICH_API_KEY    API-sleutel met rechten album.read, asset.read, asset.view
  IMMICH_ALBUM      naam of id van het album (standaard "Fotolijst")
  PHOTO_DIR         map met jpg/png-foto's, voor testen zonder Immich
  REFRESH_SECONDS   tijd tussen twee foto's (standaard 3600 = elk uur)
  FRAME_TOKEN       optioneel: /frame vereist dan ?token=<waarde>
  PORT              standaard 8080
  ORIENTATION       landscape = lijstje hangt liggend, alleen liggende foto's
                    portrait  = lijstje hangt staand, alleen staande foto's (gedraaid)
                    auto      = alle foto's, staande een kwartslag gedraaid (standaard)
  ROTATION          draairichting voor staande foto's: 90 of 270 (standaard 90);
                    staan ze op hun kop, kies dan de andere
  SATURATION        verzadiging (standaard 1.5; 1.0 = origineel)
  CONTRAST          contrast (standaard 1.2)
  BRIGHTNESS        helderheid (standaard 1.0)
  WARMTH            trekt naar rood, 0..0.15 (standaard 0.0)
"""
import io
import json
import logging
import os
import random
import threading
import urllib.error
import urllib.parse
import urllib.request
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from PIL import Image, ImageOps

import epaper

log = logging.getLogger("fotolijst")


def load_dotenv(path):
    if not os.path.exists(path):
        return
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, val = line.split("=", 1)
            os.environ.setdefault(key.strip(), val.strip().strip('"').strip("'"))


load_dotenv(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".env"))

CFG = {
    "immich_url": os.environ.get("IMMICH_URL", "").rstrip("/"),
    "api_key": os.environ.get("IMMICH_API_KEY", ""),
    "album": os.environ.get("IMMICH_ALBUM", "Fotolijst"),
    "photo_dir": os.environ.get("PHOTO_DIR", ""),
    "refresh": int(os.environ.get("REFRESH_SECONDS", "3600")),
    "token": os.environ.get("FRAME_TOKEN", ""),
    "port": int(os.environ.get("PORT", "8080")),
    "saturation": float(os.environ.get("SATURATION", "1.5")),
    "contrast": float(os.environ.get("CONTRAST", "1.2")),
    "warmth": float(os.environ.get("WARMTH", "0.0")),
    "brightness": float(os.environ.get("BRIGHTNESS", "1.0")),
    "orientation": os.environ.get("ORIENTATION", "auto").lower(),
    "rotation": int(os.environ.get("ROTATION", "90")),
}
if CFG["orientation"] not in ("landscape", "portrait", "auto"):
    raise SystemExit("ORIENTATION moet landscape, portrait of auto zijn")
if CFG["rotation"] not in (90, 270):
    raise SystemExit("ROTATION moet 90 of 270 zijn")

recent = deque(maxlen=30)   # recent getoonde foto's, om herhaling te vermijden
is_portrait = {}            # naam -> True/False, zodat we niet steeds opnieuw downloaden
MAX_TRIES = 20              # zoveel foto's proberen om er een met de juiste stand te vinden
last_preview = None
lock = threading.Lock()


# ---------------------------------------------------------------- bronnen

class Immich:
    def __init__(self, url, key, album):
        self.url, self.key, self.album = url, key, album
        self.album_id = None

    def _req(self, method, path, body=None, raw=False):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.url + path, data=data, method=method)
        req.add_header("x-api-key", self.key)
        req.add_header("Accept", "application/json" if not raw else "*/*")
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.read() if raw else json.load(r)
        except urllib.error.URLError as e:
            if isinstance(e, urllib.error.HTTPError):
                raise
            host = urllib.parse.urlparse(self.url).hostname
            raise RuntimeError(f"kan Immich niet bereiken op host '{host}': {e.reason}") from e

    def _resolve_album(self):
        if self.album_id:
            return self.album_id
        albums = self._req("GET", "/api/albums") + self._req("GET", "/api/albums?shared=true")
        for a in albums:
            if self.album in (a["id"], a["albumName"]):
                self.album_id = a["id"]
                log.info("album '%s' gevonden (%s foto's)", a["albumName"], a.get("assetCount", "?"))
                return self.album_id
        raise RuntimeError(f"album '{self.album}' niet gevonden")

    def asset_ids(self):
        album_id = self._resolve_album()
        try:
            # Nieuwere Immich-versies: zoeken binnen een album, alleen foto's.
            ids, page = [], 1
            while page:
                res = self._req("POST", "/api/search/metadata",
                                {"albumIds": [album_id], "type": "IMAGE", "size": 1000, "page": page})
                assets = res["assets"]
                ids += [a["id"] for a in assets["items"]]
                page = assets.get("nextPage")
                page = int(page) if page else None
            return ids
        except urllib.error.HTTPError as e:
            log.info("search/metadata gaf %s, val terug op album-endpoint", e.code)
            album = self._req("GET", f"/api/albums/{album_id}")
            return [a["id"] for a in album.get("assets", []) if a.get("type") == "IMAGE"]

    def candidates(self):
        return self.asset_ids()

    def load(self, asset):
        data = self._req("GET", f"/api/assets/{asset}/thumbnail?size=preview", raw=True)
        return io.BytesIO(data)


class Folder:
    EXT = (".jpg", ".jpeg", ".png", ".webp")

    def __init__(self, path):
        self.path = path

    def candidates(self):
        return sorted(f for f in os.listdir(self.path) if f.lower().endswith(self.EXT))

    def load(self, name):
        return os.path.join(self.path, name)


def make_source():
    if CFG["immich_url"]:
        if not CFG["immich_url"].startswith(("http://", "https://")):
            raise SystemExit(f"IMMICH_URL moet met https:// of http:// beginnen (nu: {CFG['immich_url']!r})")
        if not CFG["api_key"]:
            raise SystemExit("IMMICH_API_KEY ontbreekt")
        return Immich(CFG["immich_url"], CFG["api_key"], CFG["album"])
    if CFG["photo_dir"]:
        return Folder(CFG["photo_dir"])
    raise SystemExit("stel IMMICH_URL (+ IMMICH_API_KEY) of PHOTO_DIR in")


SOURCE = None


def choose_photo():
    """Kies een foto die past bij ORIENTATION; geeft (naam, beeld, staand) terug."""
    names = SOURCE.candidates()
    if not names:
        raise RuntimeError("geen foto's gevonden (album leeg?)")
    fresh = [n for n in names if n not in recent]
    random.shuffle(fresh)
    # recent getoonde foto's achteraan (oudste eerst): liever een herhaling met de
    # juiste stand dan een foto met de verkeerde stand
    last_shown = {n: i for i, n in enumerate(recent)}   # hoger = later getoond
    available = set(names)
    order = fresh + sorted((n for n in last_shown if n in available), key=last_shown.get)
    if CFG["orientation"] != "auto":
        want = CFG["orientation"] == "portrait"
        # foto's waarvan we de stand al kennen en die niet passen, achteraan
        order.sort(key=lambda n: is_portrait.get(n, want) != want)
    fallback = None
    for name in order[:MAX_TRIES]:  # bekende stand kost geen download meer
        img = ImageOps.exif_transpose(Image.open(SOURCE.load(name)))
        portrait = img.height > img.width
        is_portrait[name] = portrait
        if CFG["orientation"] == "auto" or portrait == (CFG["orientation"] == "portrait"):
            return name, img, portrait
        fallback = fallback or (name, img, portrait)
    log.warning("geen %se foto gevonden in %d pogingen, toon toch een andere",
                "staand" if CFG["orientation"] == "portrait" else "liggend", MAX_TRIES)
    return fallback


def next_frame():
    global last_preview
    with lock:
        name, img, portrait = choose_photo()
        rotate = CFG["orientation"] == "portrait" or (CFG["orientation"] == "auto" and portrait)
        centering = (0.5, 0.35)       # bovenkant van de foto iets meer in beeld
        if rotate:
            img = img.rotate(CFG["rotation"], expand=True)
            # na draaien zit de bovenkant links (90) of rechts (270)
            centering = (0.35, 0.5) if CFG["rotation"] == 90 else (0.65, 0.5)
        q = epaper.convert(img, saturation=CFG["saturation"], contrast=CFG["contrast"],
                           brightness=CFG["brightness"], warmth=CFG["warmth"],
                           centering=centering)
        recent.append(name)
        buf = io.BytesIO()
        epaper.preview(q).save(buf, "PNG")
        last_preview = buf.getvalue()
        log.info("foto geleverd: %s (%s%s)", name, "staand" if portrait else "liggend",
                 ", gedraaid" if rotate else "")
        return name, epaper.pack(q)


# ---------------------------------------------------------------- http

class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype, headers=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        if url.path == "/health":
            return self._send(200, b"ok\n", "text/plain")
        if url.path == "/preview.png":
            if not last_preview:
                return self._send(404, b"nog geen foto geleverd\n", "text/plain")
            return self._send(200, last_preview, "image/png")
        if url.path == "/frame":
            token = urllib.parse.parse_qs(url.query).get("token", [""])[0]
            if CFG["token"] and token != CFG["token"]:
                return self._send(403, b"verkeerd token\n", "text/plain")
            try:
                name, frame = next_frame()
            except Exception as e:  # bron onbereikbaar, album leeg, ...
                log.error("geen foto: %s", e)
                return self._send(503, f"geen foto: {e}\n".encode(), "text/plain",
                                  {"X-Sleep-Seconds": "300"})
            return self._send(200, frame, "application/octet-stream",
                              {"X-Sleep-Seconds": str(CFG["refresh"])})
        self._send(404, b"niet gevonden\n", "text/plain")

    def log_message(self, fmt, *args):
        log.debug("%s " + fmt, self.address_string(), *args)


def main():
    global SOURCE
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    SOURCE = make_source()
    log.info("bron: %s, ververs elke %d s, poort %d, stand %s (draaiing %d)",
             "Immich" if isinstance(SOURCE, Immich) else CFG["photo_dir"],
             CFG["refresh"], CFG["port"], CFG["orientation"], CFG["rotation"])
    ThreadingHTTPServer(("", CFG["port"]), Handler).serve_forever()


if __name__ == "__main__":
    main()
