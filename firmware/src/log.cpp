#include "log.h"

#include <stdarg.h>

USBCDC USBSerial;

void logf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.print(buf);
  if (USBSerial) USBSerial.print(buf);
}
