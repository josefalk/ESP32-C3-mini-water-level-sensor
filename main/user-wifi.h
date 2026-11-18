// ============================================
// user-wifi.h
// ============================================
#ifndef USER_WIFI_H
#define USER_WIFI_H

#include <Arduino.h>

void initWiFi();
void handleWiFi();
bool isWiFiConnected();
bool isWiFiAPMode();                  // setup Access Point is on (no saved network worked)
String getWiFiIP();
void updateSensorData(float distance, float percent);
void setSensorInvalid();              // sensor stopped answering
bool isLedAutoMode();

#endif
