// Fotolijst-firmware voor ESP32-S3 + Waveshare 5.65" 7-kleuren e-paper.
//
// - Haalt periodiek een foto op: rechtstreeks uit Immich (en zet die zelf om) of als
//   kant-en-klaar frame van de fotolijst-server. Zie config.txt.
// - De native USB-poort biedt een schijfje met config.txt aan; wijzigingen worden na een
//   paar seconden overgenomen.
// - Seriële commando's (COM-poort of native USB, 115200 baud):
//     NOW                      meteen een nieuwe foto ophalen
//     IMG0 + 134400 bytes      ruw frame tonen (tools/convert.py, tools/calibrate.py)
//     IMG1/IMG2 + opties + frame   idem met paneelopties (TSE, PLL, rondes[, CDI])
//   Na een frame via serieel wordt het automatisch ophalen 10 minuten uitgesteld.
#include <Arduino.h>
#include <USB.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "configdisk.h"
#include "epd5in65f.h"
#include "imaging.h"
#include "log.h"
#include "photos.h"
#include "settings.h"

static uint8_t *frame = nullptr;
static Settings settings;
static uint32_t nextFetch = 0;          // millis() van de volgende ophaalbeurt
static bool fetchNow = true;

// ------------------------------------------------------------ scherm

static void fromFrame(int y, uint8_t *row) {
  memcpy(row, frame + (size_t)y * (epd::WIDTH / 2), epd::WIDTH / 2);
}

static void showFrame(const epd::Options &opts = epd::Options(), uint8_t passes = 1) {
  if (!epd::init(opts)) {
    logf("ERR scherm init\n");
    return;
  }
  uint32_t t = millis();
  bool ok = epd::show(fromFrame);
  for (uint8_t i = 1; ok && i < passes; i++) ok = epd::refreshAgain();
  epd::sleep();
  logf("%s %lu s\n", ok ? "DONE" : "ERR refresh", (unsigned long)((millis() - t) / 1000));
}

// ------------------------------------------------------------ WiFi

static bool connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (settings.wifiSsid.isEmpty()) {
    logf("ERR geen WIFI_SSID in config.txt\n");
    return false;
  }
  logf("WiFi verbinden met '%s'...\n", settings.wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t > 30000) {
      logf("ERR WiFi: geen verbinding (status %d)\n", WiFi.status());
      WiFi.disconnect();
      return false;
    }
    delay(250);
  }
  logf("WiFi ok, ip %s\n", WiFi.localIP().toString().c_str());
  return true;
}

static uint32_t update() {
  if (!connectWifi()) return 300;
  uint32_t t = millis();
  photos::Result r = photos::fetchFrame(settings, frame);
  logf("%s: %s (%lu ms)\n", r.ok ? "opgehaald" : "ERR", r.message.c_str(),
       (unsigned long)(millis() - t));
  if (r.ok) showFrame();
  logf("volgende foto over %lu s\n", (unsigned long)r.sleepSeconds);
  return r.sleepSeconds;
}

static void applySettings(const String &text) {
  settings = parseSettings(text);
  String mode = settings.immichMode() ? "Immich (" + settings.immichAlbum + ")"
              : settings.serverMode() ? "server" : "niet ingesteld";
  logf("instellingen: WiFi '%s', bron %s, elke %lu s, stand %s/%d\n", settings.wifiSsid.c_str(),
       mode.c_str(), (unsigned long)settings.refreshSeconds, settings.orientation.c_str(),
       settings.rotation);
}

// ------------------------------------------------------------ serieel

static Stream *cmdPort = nullptr;   // poort waarop het huidige commando binnenkomt

static int readByte(uint32_t timeoutMs) {
  uint32_t t = millis();
  while (!cmdPort->available()) {
    if (millis() - t > timeoutMs) return -1;
    delay(1);
  }
  return cmdPort->read();
}

static bool readFrame() {
  size_t got = 0;
  uint32_t last = millis();
  while (got < img::FRAME_BYTES) {
    int n = cmdPort->available();
    if (n > 0) {
      got += cmdPort->readBytes(frame + got, min((size_t)n, img::FRAME_BYTES - got));
      last = millis();
    } else if (millis() - last > 3000) {
      logf("ERR timeout na %u bytes\n", (unsigned)got);
      return false;
    }
  }
  logf("OK ontvangen\n");
  return true;
}

static void handleCommand(Stream &port) {
  cmdPort = &port;
  String head;
  for (int i = 0; i < 4; i++) {
    int c = readByte(500);
    if (c < 0) return;
    head += (char)c;
    if (i == 2 && head == "NOW") {
      fetchNow = true;
      return;
    }
  }
  if (!head.startsWith("IMG")) return;
  epd::Options opts;
  uint8_t passes = 1;
  if (head[3] == '1' || head[3] == '2') {
    int a = readByte(1000), b = readByte(1000), c = readByte(1000);
    int d = head[3] == '2' ? readByte(1000) : opts.cdi;
    if (a < 0 || b < 0 || c < 0 || d < 0) return;
    opts.tse = a;
    opts.pll = b;
    passes = constrain(c, 1, 3);
    opts.cdi = d;
  } else if (head[3] != '0') {
    return;
  }
  if (!readFrame()) return;
  showFrame(opts, passes);
  uint32_t pause = millis() + 600000UL;   // testbeeld 10 minuten laten staan
  if ((int32_t)(pause - nextFetch) > 0) nextFetch = pause;
  fetchNow = false;
}

// ------------------------------------------------------------ setup/loop

void setup() {
  Serial.begin(115200);
  Serial.setRxBufferSize(8192);
  delay(300);
  logf("\nfotolijst ESP32-S3, PSRAM %lu KB vrij\n", (unsigned long)(ESP.getFreePsram() >> 10));

  frame = (uint8_t *)heap_caps_malloc(img::FRAME_BYTES, MALLOC_CAP_SPIRAM);
  applySettings(configdisk::begin());

  USBSerial.begin();
  configdisk::startUsb();
  USB.productName("Fotolijst");
  USB.begin();

  epd::begin();
}

void loop() {
  if (Serial.available()) handleCommand(Serial);
  if (USBSerial.available()) handleCommand(USBSerial);

  String text;
  if (configdisk::poll(text)) {
    applySettings(text);
    WiFi.disconnect();    // mogelijk andere WiFi-gegevens
    fetchNow = true;
  }

  if (fetchNow || (int32_t)(millis() - nextFetch) >= 0) {
    fetchNow = false;
    nextFetch = millis() + update() * 1000UL;
  }
  delay(10);
}
