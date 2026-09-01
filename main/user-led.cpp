// ============================================
//  user-led.cpp
//  Single plain (non-RGB) onboard LED on GPIO8.
// ============================================
#include "user-led.h"

#define LED_PIN 8           // Onboard LED GPIO

// Some boards wire the onboard LED active-low (LOW = on).
// If ledOn() leaves it dark and ledOff() lights it up, flip this to true.
#define LED_ACTIVE_LOW false

bool ledAutoMode = false;

void initLED() {
  pinMode(LED_PIN, OUTPUT);
  ledOff();   // Apply initial "off" state
}

// r/g/b are kept for compatibility with existing call sites (main.ino's
// auto-color logic), but since this is a single plain LED, any non-zero
// value just turns it on -- there's no actual color control.
void ledOn(uint8_t r, uint8_t g, uint8_t b) {
  digitalWrite(LED_PIN, LED_ACTIVE_LOW ? HIGH : LOW);
}

void ledOff() {
  digitalWrite(LED_PIN, LED_ACTIVE_LOW ? LOW : HIGH);
}

void setLedAutoMode(bool state) {
    ledAutoMode = state;
}

bool getLedAutoMode() {
    return ledAutoMode;
}