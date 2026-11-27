// Foto's ophalen: rechtstreeks uit Immich (zelf omzetten) of als kant-en-klaar frame
// van de fotolijst-server.
#pragma once
#include <Arduino.h>

#include "settings.h"

namespace photos {

struct Result {
  bool ok = false;
  uint32_t sleepSeconds = 300;   // tot de volgende poging
  String message;                // voor het log
};

// Vult `frame` (img::FRAME_BYTES) met een nieuwe foto.
Result fetchFrame(const Settings &s, uint8_t *frame);

}  // namespace photos
