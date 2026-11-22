#include "epd5in65f.h"

namespace epd {

static SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

static void cmd(uint8_t c) {
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  SPI1.beginTransaction(spiSettings);
  SPI1.transfer(c);
  SPI1.endTransaction();
  digitalWrite(PIN_CS, HIGH);
}

static void data(uint8_t d) {
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  SPI1.beginTransaction(spiSettings);
  SPI1.transfer(d);
  SPI1.endTransaction();
  digitalWrite(PIN_CS, HIGH);
}

static void cmdData(uint8_t c, std::initializer_list<uint8_t> d) {
  cmd(c);
  for (uint8_t b : d) data(b);
}

// BUSY: laag = bezig, hoog = klaar.
static bool waitLevel(int level, uint32_t timeoutMs, const char *what) {
  uint32_t t0 = millis();
  while (digitalRead(PIN_BUSY) != level) {
    if (millis() - t0 > timeoutMs) {
      Serial.printf("  busy-timeout bij %s (na %lu ms)\n", what, (unsigned long)(millis() - t0));
      return false;
    }
    delay(1);
  }
  Serial.printf("  %s: %lu ms\n", what, (unsigned long)(millis() - t0));
  return true;
}

void begin() {
  pinMode(PIN_RST, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_BUSY, INPUT);
  digitalWrite(PIN_CS, HIGH);
  SPI1.setSCK(PIN_CLK);
  SPI1.setTX(PIN_DIN);
  SPI1.begin();
}

static void reset() {
  digitalWrite(PIN_RST, HIGH); delay(200);
  digitalWrite(PIN_RST, LOW);  delay(2);
  digitalWrite(PIN_RST, HIGH); delay(200);
}

bool init(const Options &opt) {
  reset();
  if (!waitLevel(HIGH, 5000, "na reset")) return false;
  cmdData(0x00, {0xEF, 0x08});
  cmdData(0x01, {0x37, 0x00, 0x23, 0x23});
  cmdData(0x03, {0x00});
  cmdData(0x06, {0xC7, 0xC7, 0x1D});
  cmdData(0x30, {opt.pll});
  cmdData(0x41, {opt.tse});
  cmdData(0x50, {opt.cdi});
  cmdData(0x60, {0x22});
  cmdData(0x61, {0x02, 0x58, 0x01, 0xC0});
  cmdData(0xE3, {0xAA});
  delay(100);
  cmdData(0x50, {opt.cdi});
  return true;
}

bool show(RowFn fill) {
  static uint8_t row[WIDTH / 2];
  cmdData(0x61, {0x02, 0x58, 0x01, 0xC0});
  cmd(0x10);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  SPI1.beginTransaction(spiSettings);
  for (int y = 0; y < HEIGHT; y++) {
    fill(y, row);
    SPI1.transfer(row, sizeof(row));
  }
  SPI1.endTransaction();
  digitalWrite(PIN_CS, HIGH);
  return refreshAgain();
}

bool refreshAgain() {
  cmd(0x04);
  if (!waitLevel(HIGH, 10000, "power on")) return false;
  cmd(0x12);
  if (!waitLevel(HIGH, 120000, "refresh")) return false;
  cmd(0x02);
  waitLevel(LOW, 5000, "power off");
  delay(200);
  return true;
}

void sleep() {
  delay(100);
  cmdData(0x07, {0xA5});
  delay(100);
  digitalWrite(PIN_RST, LOW);
}

}  // namespace epd
