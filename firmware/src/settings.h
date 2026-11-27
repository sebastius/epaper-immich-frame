// Instellingen uit config.txt.
#pragma once
#include <Arduino.h>

struct Settings {
  String wifiSsid, wifiPass;

  // Modus "immich": rechtstreeks naar Immich (heeft voorrang als IMMICH_URL gezet is).
  String immichUrl, immichKey, immichAlbum = "Fotolijst";
  // Modus "server": kant-en-klare frames van de fotolijst-server.
  String frameUrl;

  uint32_t refreshSeconds = 3600;
  String orientation = "auto";   // landscape | portrait | auto
  int rotation = 90;              // 90 | 270
  float saturation = 1.5f, contrast = 1.2f, brightness = 1.0f, warmth = 0.0f;

  bool immichMode() const { return immichUrl.length() > 0; }
  bool serverMode() const { return !immichMode() && frameUrl.length() > 0; }
};

// Leest KEY=waarde-regels; onbekende sleutels en commentaar (#) worden genegeerd.
Settings parseSettings(const String &text);

// Inhoud van een nieuwe config.txt (met uitleg, lege geheimen).
const char *defaultConfigText();
