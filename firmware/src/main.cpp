// Fotolijst-firmware voor Pico W + Waveshare 5.65" ACeP.
//
// 1. Haalt periodiek een kant-en-klaar beeld op bij de fotolijst-server (WiFi, HTTP)
//    en wacht daarna zo lang als de server aangeeft (header X-Sleep-Seconds).
// 2. Accepteert daarnaast beelden via USB-serieel (voor testen met tools/convert.py).
// 3. Biedt een USB-schijfje aan met config.txt (WiFi en server-adres).
//
// Serieel protocol: host stuurt "IMG0" + WIDTH*HEIGHT/2 bytes (4 bit per pixel, hoge nibble eerst),
// of "IMG1" + 3 optiebytes (TSE-register, PLL, aantal rondes) + frame,
// of "IMG2" + 4 optiebytes (TSE, PLL, rondes, CDI/randkleur) + frame.
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "epd5in65f.h"

#include "config.h"

static constexpr size_t FRAME_BYTES = epd::WIDTH * epd::HEIGHT / 2;
static uint8_t frame[FRAME_BYTES];

static void fromFrame(int y, uint8_t *row) {
  memcpy(row, frame + (size_t)y * (epd::WIDTH / 2), epd::WIDTH / 2);
}

static epd::Options opts;
static uint8_t passes = 1;

static void showFrame() {
  Serial.printf("init... (tse 0x%02X, pll 0x%02X, cdi 0x%02X, rondes %u)\n", opts.tse, opts.pll,
                opts.cdi, passes);
  if (!epd::init(opts)) { Serial.println("ERR init"); return; }
  uint32_t t = millis();
  bool ok = epd::show(fromFrame);
  for (uint8_t i = 1; ok && i < passes; i++) {
    Serial.printf("ronde %u...\n", i + 1);
    ok = epd::refreshAgain();
  }
  epd::sleep();
  Serial.printf("%s %lu s\n", ok ? "DONE" : "ERR refresh", (unsigned long)((millis() - t) / 1000));
}

static int readByte(uint32_t timeoutMs) {
  uint32_t t = millis();
  while (!Serial.available()) {
    if (millis() - t > timeoutMs) return -1;
    delay(1);
  }
  return Serial.read();
}

// Wacht op de header "IMG0"/"IMG1" en leest daarna (opties en) het frame.
static bool receiveFrame() {
  static const char magic[] = "IMG";
  int matched = 0;
  while (matched < 3) {
    int c = readByte(1000);
    if (c < 0) return false;
    matched = (c == magic[matched]) ? matched + 1 : (c == magic[0] ? 1 : 0);
  }
  int ver = readByte(1000);
  opts = epd::Options();
  passes = 1;
  if (ver == '1' || ver == '2') {
    int a = readByte(1000), b = readByte(1000), c = readByte(1000);
    int d = ver == '2' ? readByte(1000) : opts.cdi;
    if (a < 0 || b < 0 || c < 0 || d < 0) { Serial.println("ERR opties"); return false; }
    opts.tse = (uint8_t)a;
    opts.pll = (uint8_t)b;
    passes = c < 1 ? 1 : (c > 3 ? 3 : c);
    opts.cdi = (uint8_t)d;
  } else if (ver != '0') {
    Serial.println("ERR onbekende header");
    return false;
  }
  size_t got = 0;
  uint32_t last = millis();
  while (got < FRAME_BYTES) {
    int n = Serial.available();
    if (n > 0) {
      got += Serial.readBytes(frame + got, min((size_t)n, FRAME_BYTES - got));
      last = millis();
    } else if (millis() - last > 3000) {
      Serial.printf("ERR timeout na %u bytes\n", (unsigned)got);
      return false;
    }
  }
  Serial.println("OK ontvangen");
  return true;
}

// ---------------------------------------------------------------- WiFi

static constexpr uint32_t RETRY_SECONDS = 300;   // bij een fout: over 5 minuten opnieuw
static uint32_t nextFetch = 0;                   // millis() van de volgende ophaalbeurt

static bool connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (config.wifiSsid.length() == 0) {
    Serial.println("ERR geen WIFI_SSID in config.txt");
    return false;
  }
  Serial.printf("WiFi verbinden met '%s'...\n", config.wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSsid.c_str(), config.wifiPass.c_str());
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t > 30000) {
      Serial.println("ERR WiFi: geen verbinding");
      return false;
    }
    delay(250);
  }
  Serial.printf("WiFi ok, ip %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// Haalt een frame op; geeft het aantal seconden tot de volgende poging terug.
static uint32_t fetchAndShow() {
  if (config.frameUrl.length() == 0) {
    Serial.println("ERR geen FRAME_URL in config.txt");
    return RETRY_SECONDS;
  }
  if (!connectWifi()) return RETRY_SECONDS;

  uint32_t sleepS = RETRY_SECONDS;
  bool got = false;
  {
    HTTPClient http;
    http.setTimeout(30000);
    const char *hdrs[] = {"X-Sleep-Seconds"};
    http.collectHeaders(hdrs, 1);
    http.begin(config.frameUrl);
    int code = http.GET();
    if (http.hasHeader("X-Sleep-Seconds")) {
      long v = http.header("X-Sleep-Seconds").toInt();
      if (v >= 60) sleepS = v;
    }
    int len = http.getSize();
    if (code != 200) {
      Serial.printf("ERR server: HTTP %d\n", code);
    } else if (len != (int)FRAME_BYTES) {
      Serial.printf("ERR server: %d bytes in plaats van %u\n", len, (unsigned)FRAME_BYTES);
    } else {
      WiFiClient *s = http.getStreamPtr();
      size_t n = 0;
      uint32_t last = millis();
      while (n < FRAME_BYTES && millis() - last < 15000) {
        int a = s->available();
        if (a > 0) {
          n += s->readBytes(frame + n, min((size_t)a, FRAME_BYTES - n));
          last = millis();
        } else {
          delay(2);
        }
      }
      got = (n == FRAME_BYTES);
      if (!got) Serial.printf("ERR download afgebroken na %u bytes\n", (unsigned)n);
    }
    http.end();
  }
  WiFi.disconnect();

  if (got) {
    Serial.println("frame opgehaald");
    opts = epd::Options();
    passes = 1;
    showFrame();
  }
  if (!got) sleepS = RETRY_SECONDS;
  Serial.printf("volgende foto over %lu s\n", (unsigned long)sleepS);
  return sleepS;
}

void setup() {
  Serial.begin(115200);
  epd::begin();
  delay(2000);  // even tijd om een seriële monitor te openen
  configBegin();
}

void loop() {
  if (configReloaded()) {
    WiFi.disconnect();          // nieuwe WiFi-gegevens: opnieuw verbinden
    nextFetch = millis();       // en meteen een foto ophalen
  }
  if (Serial.available() && receiveFrame()) {
    showFrame();
    // testbeeld via USB: automatisch ophalen 10 minuten uitstellen
    uint32_t pause = millis() + 600000UL;
    if ((int32_t)(pause - nextFetch) > 0) nextFetch = pause;
  }
  if ((int32_t)(millis() - nextFetch) >= 0) {
    nextFetch = millis() + fetchAndShow() * 1000UL;
  }
  delay(10);
}
