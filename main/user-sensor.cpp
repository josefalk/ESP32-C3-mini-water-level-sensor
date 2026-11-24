// ============================================
// user-sensor.cpp
// ============================================
#include "user-sensor.h"
#include <Preferences.h>

namespace {
Preferences sensorPrefs;
A02YYUW *driver = nullptr;
int rxPin = DEFAULT_SENSOR_RX;
int txPin = DEFAULT_SENSOR_TX;

// Taken: 3 button, 5/6 OLED, 8 LED, 9 BOOT, 18/19 USB, 11-17 flash.
// GPIO 2 is left out too: it is a boot strapping pin.
const int CHOICES[] = {0, 1, 4, 7, 10, 20, 21};
const int CHOICE_COUNT = sizeof(CHOICES) / sizeof(CHOICES[0]);

bool isChoice(int pin) {
  for (int i = 0; i < CHOICE_COUNT; i++)
    if (CHOICES[i] == pin) return true;
  return false;
}
}

void initSensorWiring(A02YYUW &sensor) {
  driver = &sensor;
  sensorPrefs.begin("sensor", false);
  if (sensorPrefs.isKey("rx")) {
    rxPin = sensorPrefs.getInt("rx", DEFAULT_SENSOR_RX);
    txPin = sensorPrefs.getInt("tx", DEFAULT_SENSOR_TX);
  } else if (sensorPrefs.getBool("swap", false)) {
    // Migrate the earlier "swap RX/TX" checkbox
    rxPin = DEFAULT_SENSOR_TX;
    txPin = DEFAULT_SENSOR_RX;
  }
  if (!isChoice(rxPin) || !isChoice(txPin) || rxPin == txPin) {   // damaged data: use defaults
    rxPin = DEFAULT_SENSOR_RX;
    txPin = DEFAULT_SENSOR_TX;
  }
  sensor.setPins(rxPin, txPin);
  Serial.printf("Sensor: RX=GPIO%d TX=GPIO%d\n", rxPin, txPin);
}

int getSensorRxPin() { return rxPin; }
int getSensorTxPin() { return txPin; }

int getSensorPinChoices(const int *&list) {
  list = CHOICES;
  return CHOICE_COUNT;
}

String setSensorPins(int rx, int tx) {
  if (!isChoice(rx) || !isChoice(tx)) return "that pin can't be used";
  if (rx == tx) return "RX and TX need different pins";
  if (rx == rxPin && tx == txPin) return "";     // nothing changed: no flash write
  rxPin = rx;
  txPin = tx;
  sensorPrefs.putInt("rx", rxPin);
  sensorPrefs.putInt("tx", txPin);
  if (driver) driver->setPins(rxPin, txPin);
  return "";
}
