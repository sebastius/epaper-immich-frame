// Instellingen uit config.txt.
#pragma once
#include <Arduino.h>

struct Settings {
  String wifiSsid, wifiPass;

  String immichUrl, immichKey, immichAlbum = "Fotolijst";

  uint32_t refreshSeconds = 3600;
  String orientation = "auto";   // landscape | portrait | auto
  int rotation = 90;              // 90 | 270
  float saturation = 1.5f, contrast = 1.2f, brightness = 1.0f, warmth = 0.0f;
};

// Leest KEY=waarde-regels; onbekende sleutels en commentaar (#) worden genegeerd.
Settings parseSettings(const String &text);

// Inhoud van een nieuwe config.txt (met uitleg, lege geheimen).
const char *defaultConfigText();
