// ============================================
// user-history.cpp
// ============================================
#include "user-history.h"
#include <Preferences.h>
#include <time.h>

namespace {

struct Sample {
  uint32_t t;     // unix time (start of the bucket)
  uint16_t p10;   // percent * 10
  uint16_t pad;
};

// Fixed-size ring buffer that averages readings into time buckets
template <int N>
struct Series {
  Sample buf[N];
  int head = 0, count = 0;
  uint32_t period;              // bucket length in seconds
  uint32_t bucket = 0;          // current bucket index (time / period)
  float sum = 0; int n = 0;     // running average for the current bucket

  explicit Series(uint32_t p) : period(p) {}

  void push(uint32_t t, float pct) {
    buf[head] = { t, (uint16_t)lroundf(pct * 10), 0 };
    head = (head + 1) % N;
    if (count < N) count++;
  }

  // Returns true when a finished bucket was pushed
  bool add(uint32_t now, float pct) {
    uint32_t b = now / period;
    bool pushed = false;
    if (n > 0 && b != bucket) {
      push(bucket * period, sum / n);
      sum = 0; n = 0;
      pushed = true;
    }
    bucket = b;
    sum += pct; n++;
    return pushed;
  }

  const Sample &at(int i) const {            // i = 0 is the oldest point
    return buf[(head - count + i + N) % N];
  }

  void toJson(String &out) const {
    out += '[';
    for (int i = 0; i < count; i++) {
      if (i) out += ',';
      const Sample &s = at(i);
      out += '['; out += s.t; out += ','; out += String(s.p10 / 10.0, 1); out += ']';
    }
    out += ']';
  }
};

Series<288> day(300);    // 24 h @ 5 min
Series<168> week(3600);  // 7 days @ 1 h
Preferences histPrefs;

bool clockValid(uint32_t now) { return now > 1700000000UL; }  // NTP has synced

void saveWeek() {
  static Sample ordered[168];
  for (int i = 0; i < week.count; i++) ordered[i] = week.at(i);
  histPrefs.putBytes("week", ordered, week.count * sizeof(Sample));
}

} // namespace

void initHistory() {
  histPrefs.begin("hist", false);
  static Sample ordered[168];
  size_t len = histPrefs.getBytesLength("week");
  if (len > 0 && len <= sizeof(ordered) && len % sizeof(Sample) == 0) {
    histPrefs.getBytes("week", ordered, len);
    for (size_t i = 0; i < len / sizeof(Sample); i++)
      week.push(ordered[i].t, ordered[i].p10 / 10.0);
    Serial.printf("History: loaded %d hourly points\n", week.count);
  }
}

void addHistorySample(float percent) {
  uint32_t now = (uint32_t)time(nullptr);
  if (!clockValid(now)) return;
  day.add(now, percent);
  if (week.add(now, percent)) saveWeek();   // flash write once per hour
}

String getHistoryJson() {
  String out;
  out.reserve(8000);
  out += "{\"now\":"; out += (uint32_t)time(nullptr);
  out += ",\"day\":";  day.toJson(out);
  out += ",\"week\":"; week.toJson(out);
  out += '}';
  return out;
}
