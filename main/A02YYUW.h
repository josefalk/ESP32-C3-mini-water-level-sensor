#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

#define A02YYUW_DEBUG  // TEMP: prints raw UART bytes/frames to Serial for troubleshooting

class A02YYUW {
public:
  A02YYUW(HardwareSerial &serial, int rxPin, int txPin);
  void begin(unsigned long baud);
  float getDistance();

private:
  HardwareSerial &serial;
  int rxPin, txPin;
};
