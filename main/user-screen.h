// ============================================
// user-screen.h  (U8g2 version, 0.42" 72x40 OLED)
// ============================================
#ifndef USER_SCREEN_H
#define USER_SCREEN_H

#include <Arduino.h>
#include <U8g2lib.h>

void initScreen();
void toggleScreen();
void setScreenState(bool state);
bool getScreenState();
void showText(const String &text);
void handleScreenButton();
void showWaterLevel(float distance, float fullDistance, float emptyDistance);

#endif