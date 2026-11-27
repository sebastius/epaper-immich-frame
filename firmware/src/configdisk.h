// USB-schijfje met config.txt.
//
// Het schijfje (512 KB, FAT) staat in het PSRAM en wordt via de native USB-poort als
// massaopslag aangeboden. Schrijft de computer erop, dan wordt het na 2 seconden rust
// opgeslagen in flash (LittleFS) en config.txt opnieuw ingelezen. Uitwerpen is dus niet
// nodig; gewoon opslaan volstaat.
#pragma once
#include <Arduino.h>

namespace configdisk {

// Laadt het schijfje uit flash (of maakt een nieuw met een standaard config.txt) en
// geeft de inhoud van config.txt terug. Aanroepen vóór USB.begin().
String begin();

// Start de USB-massaopslag. Aanroepen vóór USB.begin().
void startUsb();

// Regelmatig aanroepen. Geeft true (en de nieuwe tekst) als config.txt veranderd is.
bool poll(String &configText);

}  // namespace configdisk
