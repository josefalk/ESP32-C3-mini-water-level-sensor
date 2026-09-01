// ============================================
// user-screen.cpp  (U8g2 version, ESP32-C3 + 0.42" 72x40 OLED)
// Manages the OLED display and screen toggle button
//
// Layout (3 rows):
//   Row 1: IP address / AP status
//   Row 2: reading value, or "No reading"
//   Row 3: water level bar
// ============================================

#include "user-screen.h"
#include "user-wifi.h"    // Needed for WiFi status display

#define SCREEN_WIDTH 72
#define SCREEN_HEIGHT 40
#define I2C_SDA 5          // OLED SDA pin
#define I2C_SCL 6          // OLED SCL pin
#define BUTTON_PIN 3        // GPIO for screen ON/OFF button
                             // NOTE: GPIO18/19 are the native USB D-/D+ pins
                             // on most ESP32-C3 boards -- using them as GPIO
                             // breaks USB enumeration ("device not recognized").

// U8g2 driver instance for the 0.42" 72x40 SSD1306 (hardware I2C)
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);

bool screenAvailable = false;  // True only if OLED init succeeded
bool screenOn = true;          // Current display power state
bool lastButtonState = HIGH;   // Used for button edge detection

// Row 1: IP address / AP status. Drawn first by every screen state.
static void drawIPRow() {
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.setCursor(0, 6);
  if (isWiFiConnected()) {
    u8g2.print(getWiFiIP());
  } else {
    u8g2.print("AP:");
    u8g2.print(getWiFiIP());
  }
}

// Row 3: level bar outline, optionally filled to `percent`.
static void drawBarRow(int percent, bool filled) {
  int barY = 20;
  int barH = 10;
  u8g2.drawFrame(0, barY, SCREEN_WIDTH, barH);
  if (filled) {
    int fillWidth = map(percent, 0, 100, 0, SCREEN_WIDTH);
    if (fillWidth > 2) {
      u8g2.drawBox(1, barY + 1, fillWidth - 2, barH - 2);
    }
  }
}

void initScreen() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Button uses internal pull-up

  u8g2.setBusClock(400000);  // 400kHz I2C

  // u8g2.begin() returns false if the display failed to initialize,
  // so the rest of the program can still run without a screen attached.
  if (u8g2.begin()) {
    screenAvailable = true;
    u8g2.setContrast(255);

    // Start blank -- the first showText()/showWaterLevel() call from
    // main.ino draws the real 3-row layout, no separate splash needed.
    u8g2.clearBuffer();
    u8g2.sendBuffer();
    screenOn = true;

    Serial.println("OLED connected successfully!");
  } else {
    // Allow system to run even with no screen attached
    Serial.println("No OLED detected — continuing without display.");
    screenAvailable = false;
  }
}

// Toggle ON/OFF state of OLED (button or web)
void toggleScreen() {
  if (!screenAvailable) return;

  screenOn = !screenOn;
  u8g2.setPowerSave(screenOn ? 0 : 1);  // 0 = on, 1 = sleep

  Serial.println(screenOn ? "Screen turned ON" : "Screen turned OFF");
}

// Row 1 (IP) + row 2 (given text, e.g. "No reading") + row 3 (empty bar)
void showText(const String &text) {
  if (!screenAvailable || !screenOn) return;

  u8g2.clearBuffer();
  drawIPRow();

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(0, 16);
  u8g2.print(text);

  drawBarRow(0, false);

  u8g2.sendBuffer();
}

// Handle physical button that toggles display power
void handleScreenButton() {
  static unsigned long lastPressTime = 0;
  bool currentState = digitalRead(BUTTON_PIN);

  // Detect falling edge (HIGH -> LOW)
  if (lastButtonState == HIGH && currentState == LOW) {
    unsigned long now = millis();

    // Debounce + ignore rapid presses
    if (now - lastPressTime > 500) {
      toggleScreen();
      lastPressTime = now;
    }
  }

  lastButtonState = currentState;
}

// Row 1 (IP) + row 2 (distance/percent) + row 3 (filled level bar)
void showWaterLevel(float distance, float fullDistance, float emptyDistance) {
  if (!screenAvailable || !screenOn) return;

  // Convert raw distance to percentage (clamped)
  float percent = (emptyDistance - distance) /
                  (emptyDistance - fullDistance) * 100;
  percent = constrain(percent, 0, 100);

  u8g2.clearBuffer();
  drawIPRow();

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(0, 16);
  u8g2.print(distance, 1);
  u8g2.print("cm ");
  u8g2.print((int)percent);
  u8g2.print("%");

  drawBarRow((int)percent, true);

  u8g2.sendBuffer();
}

// Set screen ON/OFF from web interface
void setScreenState(bool state) {
  if (!screenAvailable) return;
  if (screenOn == state) return;  // Avoid redundant I2C commands

  screenOn = state;
  u8g2.setPowerSave(screenOn ? 0 : 1);

  Serial.println(screenOn ? "Screen turned ON (via web)" : "Screen turned OFF (via web)");
}

// Query current screen state (OFF also if no OLED attached)
bool getScreenState() {
  return screenOn && screenAvailable;
}