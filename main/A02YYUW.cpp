// ============================================
//  A02YYUW.cpp
// ============================================

#include "A02YYUW.h"

// Constructor: store reference to HardwareSerial and pin assignments
A02YYUW::A02YYUW(HardwareSerial &serial, int rxPin, int txPin)
  : serial(serial), rxPin(rxPin), txPin(txPin) {}

void A02YYUW::begin(unsigned long baud) {
  // Initialize UART with given baud rate and 8N1 format
  serial.begin(baud, SERIAL_8N1, rxPin, txPin);
}

float A02YYUW::getDistance() {
  // The A02YYUW streams frames continuously on its own; this trigger byte
  // is harmless but not required. Rather than taking one fixed-delay
  // snapshot of the buffer (which tends to land mid-frame against a
  // free-running sensor), actively sync on the 0xFF header and wait for
  // the rest of the frame to arrive.
  serial.write(0x55);

  unsigned long start = millis();
  while (millis() - start < 60) {
    if (serial.available() == 0) {
      delay(2);
      continue;
    }

    if (serial.peek() != 0xFF) {
      serial.read();  // not a header byte -- discard and resync
      continue;
    }

    if (serial.available() < 4) {
      delay(2);  // header seen, rest of frame still arriving
      continue;
    }

    uint8_t data[4];
    for (int i = 0; i < 4; i++)
      data[i] = serial.read();

#ifdef A02YYUW_DEBUG
    Serial.printf("[A02YYUW] frame: %02X %02X %02X %02X\n",
                  data[0], data[1], data[2], data[3]);
#endif

    // Validate checksum: (byte0 + byte1 + byte2) & 0xFF == byte3
    int sum = (data[0] + data[1] + data[2]) & 0xFF;
    if (sum == data[3]) {
      // Combine high + low byte into distance (mm)
      int dist = (data[1] << 8) + data[2];
      return dist / 10.0;  // Convert mm → cm
    }

#ifdef A02YYUW_DEBUG
    Serial.printf("[A02YYUW] checksum mismatch: got %02X expected %02X\n", data[3], sum);
#endif
    // Bad frame -- keep looping in case another one arrives before timeout
  }

  // No valid reading
  return -1;
}
