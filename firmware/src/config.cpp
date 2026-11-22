#include "config.h"

#include <FatFS.h>
#include <FatFSUSB.h>
#include <tusb.h>

// Standaardwaarden voor een nieuwe config.txt komen uit secrets.h (als die er is).
#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASS ""
#define FRAME_URL ""
#endif

Config config;

static const char *CONFIG_PATH = "/config.txt";
static volatile bool hostHasDrive = false;   // computer heeft het schijfje in gebruik
static volatile bool ejected = false;        // computer heeft uitgeworpen
static volatile bool busy = false;           // wij lezen/schrijven zelf

static void writeDefault() {
  File f = FatFS.open(CONFIG_PATH, "w");
  if (!f) {
    Serial.println("ERR kan config.txt niet aanmaken");
    return;
  }
  f.print(
      "# Fotolijst-instellingen. Pas aan, sla op en werp het schijfje uit;\r\n"
      "# de fotolijst leest ze dan opnieuw in en haalt meteen een foto op.\r\n"
      "#\r\n"
      "# WiFi: alleen 2,4 GHz-netwerken werken.\r\n");
  f.printf("WIFI_SSID=%s\r\nWIFI_PASS=%s\r\n", WIFI_SSID, WIFI_PASS);
  f.print(
      "#\r\n"
      "# Adres van de fotolijst-server, met ?token=... als de server een FRAME_TOKEN heeft.\r\n");
  f.printf("FRAME_URL=%s\r\n", FRAME_URL);
  f.close();
  Serial.println("config.txt aangemaakt met standaardwaarden");
}

static void load() {
  busy = true;
  Config c;
  File f = FatFS.open(CONFIG_PATH, "r");
  if (!f) {
    writeDefault();
    f = FatFS.open(CONFIG_PATH, "r");
  }
  while (f && f.available()) {
    String line = f.readStringUntil('\n');
    line.replace("\xEF\xBB\xBF", "");   // BOM van sommige Windows-editors
    line.trim();                          // ook \r van Windows-regeleinden
    if (line.length() == 0 || line[0] == '#') continue;
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    key.trim();
    val.trim();
    if (key == "WIFI_SSID") c.wifiSsid = val;
    else if (key == "WIFI_PASS") c.wifiPass = val;
    else if (key == "FRAME_URL") c.frameUrl = val;
  }
  if (f) f.close();
  config = c;
  busy = false;
  Serial.printf("config: WiFi '%s', server %s\n", config.wifiSsid.c_str(),
                config.frameUrl.length() ? "ingesteld" : "ONTBREEKT");
}

static void onPlug(uint32_t) {
  hostHasDrive = true;
  FatFS.end();
}

static void onUnplug(uint32_t) {
  hostHasDrive = false;
  ejected = true;
}

static bool driveReady(uint32_t) { return !busy; }

void configBegin() {
  if (!FatFS.begin()) {
    Serial.println("ERR bestandssysteem niet beschikbaar");
  } else {
    load();
  }
  FatFSUSB.onPlug(onPlug);
  FatFSUSB.onUnplug(onUnplug);
  FatFSUSB.driveReady(driveReady);
  FatFSUSB.begin();
}

bool configReloaded() {
  // "Uitwerpen" in een bestandsbeheerder koppelt soms het hele USB-apparaat los
  // in plaats van een nette eject te sturen; dat telt ook als uitwerpen.
  if (hostHasDrive && (!tud_mounted() || tud_suspended())) {
    hostHasDrive = false;
    ejected = true;
  }
  if (!ejected || hostHasDrive) return false;
  ejected = false;
  Serial.println("schijfje uitgeworpen, config opnieuw inlezen");
  FatFS.end();
  if (FatFS.begin()) load();
  return true;
}
