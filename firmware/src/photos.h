// Foto's ophalen uit Immich en omzetten naar een frame voor het scherm.
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
