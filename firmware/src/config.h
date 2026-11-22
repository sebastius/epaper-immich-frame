// Instellingen uit config.txt op het USB-schijfje van de Pico.
#pragma once
#include <Arduino.h>

struct Config {
  String wifiSsid;
  String wifiPass;
  String frameUrl;
};

extern Config config;

// Start het flash-bestandssysteem, maakt zo nodig een config.txt met standaardwaarden
// en leest die in. Daarna wordt het schijfje via USB aan de computer aangeboden.
void configBegin();

// true als de computer het schijfje heeft uitgeworpen sinds de vorige aanroep;
// config is dan opnieuw ingelezen.
bool configReloaded();
