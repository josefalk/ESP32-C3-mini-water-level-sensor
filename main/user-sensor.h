// ============================================
// user-sensor.h
// ============================================
// Sensor wiring option: choose the two GPIO pins of the sensor UART, or swap
// them if the sensor's TX/RX wires were connected the wrong way round.
//   - Saved in flash (namespace "sensor"), applied right away.
//   - Not cleared by the BOOT-button reset.
#pragma once
#include <Arduino.h>
#include "A02YYUW.h"

const int DEFAULT_SENSOR_RX = 4;          // ESP input  (<- sensor TX wire)
const int DEFAULT_SENSOR_TX = 7;          // ESP output (-> sensor RX wire)

void initSensorWiring(A02YYUW &sensor);   // load the saved pins and apply them
int getSensorRxPin();
int getSensorTxPin();

// Pins free to use (not the screen button, OLED, LED, BOOT button, USB or flash)
int getSensorPinChoices(const int *&list);   // returns how many

// Validate, save and apply. Returns an error message, or "" on success.
String setSensorPins(int rx, int tx);
