// ============================================
//  A02YYUW.cpp
// ============================================

#include "A02YYUW.h"

A02YYUW::A02YYUW(HardwareSerial &serial, int rxPin, int txPin)
  : serial(serial), rxPin(rxPin), txPin(txPin) {}

void A02YYUW::begin(unsigned long baud) {
  baudRate = baud;
  started = true;
  serial.begin(baud, SERIAL_8N1, rxPin, txPin);
}

void A02YYUW::setPins(int rx, int tx) {
  if (rx == rxPin && tx == txPin) return;
  rxPin = rx;
  txPin = tx;
  if (started) {
    serial.end();
    begin(baudRate);
  }
}

float A02YYUW::getDistance() {
  // Streaming sensors send a frame every ~100 ms; wait a bit over two periods.
  float d = readFrame(false, 250);
  // No frame? The sensor may need a trigger byte -- ask once.
  if (d < 0) d = readFrame(true, 150);
  return d;
}

float A02YYUW::readFrame(bool trigger, unsigned long timeoutMs) {
  while (serial.available()) serial.read();   // drop stale backlog
  if (trigger) serial.write(0x55);

  uint8_t data[4];
  int idx = 0;
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (!serial.available()) { delay(1); continue; }
    uint8_t b = serial.read();

    if (idx == 0 && b != 0xFF) continue;      // wait for header
    data[idx++] = b;
    if (idx < 4) continue;
    idx = 0;

#ifdef A02YYUW_DEBUG
    Serial.printf("[A02YYUW] frame: %02X %02X %02X %02X\n", data[0], data[1], data[2], data[3]);
#endif

    uint8_t sum = (data[0] + data[1] + data[2]) & 0xFF;
    if (sum != data[3]) continue;             // corrupted frame, wait for next

    int mm = (data[1] << 8) | data[2];
    if (mm == 0) continue;                    // no echo / out of range
    return mm / 10.0;
  }
  return -1;
}
