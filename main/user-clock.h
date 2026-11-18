// ============================================
// user-clock.h
// ============================================
// Real time + the local time zone.
//   - The clock is set from the internet (NTP) once the device is online,
//     or by hand from the web page (e.g. in setup mode without internet).
//   - The time zone is an offset from GMT (e.g. GMT+3) plus an optional
//     +1 hour for summer time. Saved in flash (namespace "clock").
// Timestamps (history) are always stored in UTC; only displays use the zone.
#pragma once
#include <Arduino.h>

void initClock();
void startClockSync();                  // start NTP (call once the device is online)

bool isClockValid();                    // true once the time has been set
int getClockOffset();                   // minutes from GMT, without summer time (e.g. 180 = GMT+3)
bool getClockSummerTime();
int getClockTotalOffset();              // offset + 60 if summer time is on
String getClockLocalText();             // e.g. "Wed 18 Nov 23:11", or "--" if the clock isn't set

// Returns an error message, or "" on success (and saves to flash)
String setClockZone(int offsetMinutes, bool summerTime);
void setClockManual(uint32_t unixTime); // set the clock by hand (UTC seconds)
