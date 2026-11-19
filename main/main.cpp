// ============================================
//  main.cpp
// ============================================
// ESP32-C3 + 0.42" 72x40 OLED (U8g2) water level monitor
#include <Arduino.h>
#include "user-led.h"      // LED control (manual/auto modes)
#include "user-screen.h"   // OLED display + button handling
#include "user-wifi.h"     // Wi-Fi, web server, mDNS, OTA, calibration
#include "user-history.h"  // 24 h / 7 day level history for the web graph
#include "user-tank.h"     // tank sizes -> litres and percent
#include "user-clock.h"    // real time + time zone
#include "user-lock.h"     // optional PIN for the settings
#include "A02YYUW.h"       // Ultrasonic distance sensor driver

HardwareSerial mySerial(1);           // UART1 for the sensor
A02YYUW sensor(mySerial, 4, 7);       // ESP RX=GPIO4 (<- sensor TX), ESP TX=GPIO7 (-> sensor RX)

// ---- Smoothing: median of the last N valid readings ----
// A median ignores single wild readings (ripples, stray echoes) that would
// drag a plain average around.
const int SMOOTH_N = 7;
float readings[SMOOTH_N];
int readingCount = 0, readingIdx = 0;

float smoothedDistance(float raw) {
  readings[readingIdx] = raw;
  readingIdx = (readingIdx + 1) % SMOOTH_N;
  if (readingCount < SMOOTH_N) readingCount++;

  float sorted[SMOOTH_N];
  memcpy(sorted, readings, readingCount * sizeof(float));
  // insertion sort (tiny array)
  for (int i = 1; i < readingCount; i++) {
    float v = sorted[i]; int j = i - 1;
    while (j >= 0 && sorted[j] > v) { sorted[j + 1] = sorted[j]; j--; }
    sorted[j + 1] = v;
  }
  return sorted[readingCount / 2];
}

// Only report "no reading" after this many failures in a row
const int MAX_FAILS = 6;
int failStreak = 0;

void setup() {
  Serial.begin(115200);
  sensor.begin(9600);

  initLED();
  initScreen();
  initTank();
  initClock();
  initLock();
  initHistory();
  initWiFi();               // Wi-Fi + web server + mDNS + OTA

  showText("System Ready!");
}

void loop() {
  handleWiFi();             // web requests, OTA, reconnects
  handleScreenButton();

  float raw = sensor.getDistance();

  if (raw > 0) {
    failStreak = 0;
    float distance = smoothedDistance(raw);

    float percent = levelPercent(distance);   // by volume when tank sizes are set
    float litres = waterLitres(distance);     // -1 if tank sizes not set

    updateSensorData(distance, percent);
    addHistorySample(percent);
    showWaterLevel(distance, percent, litres);

    if (isLedAutoMode()) {
      if (percent < 20)       ledOn(255, 0, 0);
      else if (percent < 70)  ledOn(255, 255, 0);
      else                    ledOn(0, 255, 0);
    }
  } else if (++failStreak >= MAX_FAILS) {
    readingCount = 0;       // start smoothing fresh once the sensor returns
    showText("No reading");
    if (isLedAutoMode()) ledOn(255, 0, 0);
    setSensorInvalid();
  }

  delay(300);
}
