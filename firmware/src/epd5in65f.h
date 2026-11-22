// Driver voor Waveshare 5.65" 7-kleuren ACeP (600x448), Arduino-pico core.
// Commando-reeks gelijk aan de officiële Waveshare-driver (EPD_5in65f.c).
#pragma once
#include <Arduino.h>
#include <SPI.h>

namespace epd {

constexpr int WIDTH = 600;
constexpr int HEIGHT = 448;

enum Color : uint8_t { BLACK = 0, WHITE, GREEN, BLUE, RED, YELLOW, ORANGE, CLEAN };

constexpr int PIN_DC = 8;
constexpr int PIN_CS = 9;
constexpr int PIN_CLK = 10;
constexpr int PIN_DIN = 11;
constexpr int PIN_RST = 12;
constexpr int PIN_BUSY = 13;

// Rij-callback: vul `row` (WIDTH/2 bytes, 2 pixels per byte, hoge nibble eerst) voor rij y.
using RowFn = void (*)(int y, uint8_t *row);

void begin();
struct Options {
  uint8_t tse = 0x00;      // TSE (0x41): bit7 = externe sensor, bits 3..0 = offset (-8..+7)
  uint8_t pll = 0x3C;      // frame rate (0x30): 0x3C = 50 Hz
  uint8_t cdi = 0x37;      // VCOM/data interval (0x50): bits 7..5 = randkleur
};

bool init(const Options &opt = Options());  // reset + registers; false bij busy-timeout
bool show(RowFn fill);       // stuurt beeld en ververst; false bij busy-timeout
bool refreshAgain();         // ververst nogmaals met het beeld dat al in het geheugen staat
void sleep();

}  // namespace epd
