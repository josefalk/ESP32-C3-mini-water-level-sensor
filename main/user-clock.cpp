// ============================================
// user-clock.cpp
// ============================================
#include "user-clock.h"
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

namespace {

Preferences clockPrefs;
int offsetMin = 0;          // minutes from GMT
bool summerTime = false;    // +1 hour

// The C library wants the zone as a POSIX TZ string, where the sign is
// reversed: GMT+3 is written "<+03>-3", GMT-5:30 is "<-0530>5:30".
String twoDigits(int n) { return n < 10 ? "0" + String(n) : String(n); }

String tzString() {
  int total = getClockTotalOffset();
  int h = abs(total) / 60, m = abs(total) % 60;
  String name = String(total < 0 ? "-" : "+") + twoDigits(h);
  String rule = String(total > 0 ? "-" : "") + h;
  if (m) {
    name += twoDigits(m);
    rule += ":" + twoDigits(m);
  }
  return "<" + name + ">" + rule;
}

void applyZone() {
  setenv("TZ", tzString().c_str(), 1);
  tzset();
}

} // namespace

void initClock() {
  clockPrefs.begin("clock", false);
  offsetMin = clockPrefs.getShort("offset", 0);
  summerTime = clockPrefs.getBool("summer", false);
  applyZone();
  Serial.printf("Clock: time zone GMT%+.2g%s\n", offsetMin / 60.0, summerTime ? " + summer time" : "");
}

void startClockSync() {
  configTzTime(tzString().c_str(), "pool.ntp.org", "time.google.com");
}

bool isClockValid() {
  return time(nullptr) > 1700000000;   // any real date after Nov 2023
}

int getClockOffset()        { return offsetMin; }
bool getClockSummerTime()   { return summerTime; }
int getClockTotalOffset()   { return offsetMin + (summerTime ? 60 : 0); }

String getClockLocalText() {
  if (!isClockValid()) return "--";
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char buf[24];
  strftime(buf, sizeof(buf), "%a %d %b %H:%M", &t);
  return buf;
}

String setClockZone(int offsetMinutes, bool summer) {
  // Real time zones go from GMT-12 to GMT+14, in steps of 15 minutes
  if (offsetMinutes < -12 * 60 || offsetMinutes > 14 * 60 || offsetMinutes % 15 != 0)
    return "Time zone must be between GMT-12 and GMT+14";
  offsetMin = offsetMinutes;
  summerTime = summer;
  clockPrefs.putShort("offset", offsetMin);
  clockPrefs.putBool("summer", summerTime);
  applyZone();
  Serial.printf("Clock: time zone saved, local time now %s\n", getClockLocalText().c_str());
  return "";
}

void setClockManual(uint32_t unixTime) {
  struct timeval tv = { (time_t)unixTime, 0 };
  settimeofday(&tv, nullptr);
  Serial.printf("Clock: set by hand, local time now %s\n", getClockLocalText().c_str());
}
