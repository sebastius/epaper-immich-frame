// Optioneel. Kopieer naar secrets.h en vul in; secrets.h wordt niet gedeeld/gecommit.
// Deze waarden worden alleen gebruikt om bij de allereerste start config.txt op het
// USB-schijfje van de Pico te vullen. Daarna pas je de instellingen daar aan.
#pragma once
#define WIFI_SSID "mijn-wifi"
#define WIFI_PASS "mijn-wachtwoord"
// Adres van de fotolijst-server op je thuisnetwerk (http, geen https).
// Met FRAME_TOKEN op de server: voeg ?token=... toe.
#define FRAME_URL "http://192.168.1.10:8080/frame?token=..."
