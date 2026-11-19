// ============================================
// user-history.h
// ============================================
// Water-level history for the web graph:
//   - last 24 h, one point per 5 minutes  (kept in RAM)
//   - last 7 days, one point per hour     (saved to flash, survives restarts)
//   - last 30 days, one point per 4 hours (flash)
//   - last year, one point per day        (flash)
// Points are timestamped with real time from NTP, so nothing is recorded
// until the clock has been synced (i.e. the device is on Wi-Fi with internet).
#pragma once
#include <Arduino.h>

void initHistory();
void addHistorySample(float percent);   // call with every valid (smoothed) reading
// {"now":t,"day":[[t,p],...],"week":[...]}; range = "day"/"week"/"month"/"year"
// returns only that one ("" = day + week)
String getHistoryJson(const String &range = "");
