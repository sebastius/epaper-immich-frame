// Logging naar de COM-poort (UART0) en, als die verbonden is, de native USB-poort.
#pragma once
#include <Arduino.h>
#include <USB.h>

extern USBCDC USBSerial;

void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
