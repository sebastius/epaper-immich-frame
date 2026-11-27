#include "settings.h"

Settings parseSettings(const String &text) {
  Settings s;
  int pos = 0;
  while (pos < (int)text.length()) {
    int nl = text.indexOf('\n', pos);
    if (nl < 0) nl = text.length();
    String line = text.substring(pos, nl);
    pos = nl + 1;
    line.replace("\xEF\xBB\xBF", "");   // BOM van sommige editors
    line.trim();                          // ook \r van Windows-regeleinden
    if (line.length() == 0 || line[0] == '#') continue;
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String k = line.substring(0, eq), v = line.substring(eq + 1);
    k.trim();
    v.trim();
    if (v.length() >= 2 && (v[0] == '"' || v[0] == '\'') && v[v.length() - 1] == v[0])
      v = v.substring(1, v.length() - 1);
    k.toUpperCase();

    if (k == "WIFI_SSID") s.wifiSsid = v;
    else if (k == "WIFI_PASS") s.wifiPass = v;
    else if (k == "IMMICH_URL") { s.immichUrl = v; while (s.immichUrl.endsWith("/")) s.immichUrl.remove(s.immichUrl.length() - 1); }
    else if (k == "IMMICH_API_KEY") s.immichKey = v;
    else if (k == "IMMICH_ALBUM") { if (v.length()) s.immichAlbum = v; }
    else if (k == "REFRESH_SECONDS") { long n = v.toInt(); if (n >= 60) s.refreshSeconds = n; }
    else if (k == "ORIENTATION") { v.toLowerCase(); if (v == "landscape" || v == "portrait" || v == "auto") s.orientation = v; }
    else if (k == "ROTATION") { int n = v.toInt(); if (n == 90 || n == 270) s.rotation = n; }
    else if (k == "SATURATION") s.saturation = v.toFloat();
    else if (k == "CONTRAST") s.contrast = v.toFloat();
    else if (k == "BRIGHTNESS") s.brightness = v.toFloat();
    else if (k == "WARMTH") s.warmth = v.toFloat();
  }
  return s;
}

const char *defaultConfigText() {
  return
      "# Fotolijst-instellingen. Pas aan en sla op; na een paar seconden neemt de\r\n"
      "# fotolijst ze over en haalt hij meteen een nieuwe foto op.\r\n"
      "\r\n"
      "# WiFi (alleen 2,4 GHz)\r\n"
      "WIFI_SSID=\r\n"
      "WIFI_PASS=\r\n"
      "\r\n"
      "# Immich. API-sleutel met alleen de rechten\r\n"
      "# album.read, asset.read en asset.view. IMMICH_URL bv. https://photos.example.net\r\n"
      "IMMICH_URL=\r\n"
      "IMMICH_API_KEY=\r\n"
      "IMMICH_ALBUM=Fotolijst\r\n"
      "\r\n"
      "# Seconden tussen twee foto's (3600 = elk uur, 86400 = dagelijks)\r\n"
      "REFRESH_SECONDS=3600\r\n"
      "\r\n"
      "# landscape = alleen liggende foto's, portrait = alleen staande (gedraaid,\r\n"
      "# voor een staand lijstje), auto = alles (staande gedraaid)\r\n"
      "ORIENTATION=auto\r\n"
      "# 90 of 270: draairichting; staan foto's op hun kop, kies dan de andere\r\n"
      "ROTATION=90\r\n"
      "\r\n"
      "# Beeld (1.0 = ongewijzigd); WARMTH 0..0.15 trekt naar rood\r\n"
      "SATURATION=1.5\r\n"
      "CONTRAST=1.2\r\n"
      "BRIGHTNESS=1.0\r\n"
      "WARMTH=0.0\r\n";
}
