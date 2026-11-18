#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

// Uncomment to print every raw sensor frame to Serial (troubleshooting only)
// #define A02YYUW_DEBUG

// Driver for A02YYUW-style UART ultrasonic sensors.
// Frame format: FF  H  L  SUM   (distance = H*256+L mm, SUM = low byte of FF+H+L)
// Works with sensors that stream frames on their own, and with ones that only
// answer after a 0x55 trigger byte (it falls back to triggering automatically).
class A02YYUW {
public:
  // rxPin = ESP input  (connect to sensor TX / output wire)
  // txPin = ESP output (connect to sensor RX / input wire)
  A02YYUW(HardwareSerial &serial, int rxPin, int txPin);
  void begin(unsigned long baud = 9600);
  float getDistance();          // cm, or -1 if no valid reading

private:
  HardwareSerial &serial;
  int rxPin, txPin;
  float readFrame(bool trigger, unsigned long timeoutMs);
};
