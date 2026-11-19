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

Series<288> day(300);       // 24 h @ 5 min      (RAM only)
Series<168> week(3600);     // 7 days @ 1 h      (flash, saved every hour)
Series<180> month(14400);   // 30 days @ 4 h     (flash, saved every 4 hours)
Series<365> year(86400);    // 365 days @ 1 day  (flash, saved once a day)
Preferences histPrefs;

bool clockValid(uint32_t now) { return now > 1700000000UL; }  // NTP has synced

Sample ordered[365];        // work space for saving/loading (oldest first)

template <int N>
void saveSeries(const char *key, const Series<N> &s) {
  for (int i = 0; i < s.count; i++) ordered[i] = s.at(i);
  histPrefs.putBytes(key, ordered, s.count * sizeof(Sample));
}

// Returns false if nothing was saved under this key yet
template <int N>
bool loadSeries(const char *key, Series<N> &s) {
  if (!histPrefs.isKey(key)) return false;
  size_t len = histPrefs.getBytesLength(key);
  if (len == 0 || len > N * sizeof(Sample) || len % sizeof(Sample)) return false;
  histPrefs.getBytes(key, ordered, len);
  for (size_t i = 0; i < len / sizeof(Sample); i++)
    s.push(ordered[i].t, ordered[i].p10 / 10.0);
  return true;
}

// First start with this version: build the 30 day / 1 year history from
// the hourly points that already exist, so they don't start empty.
template <int N>
void fillFromWeek(Series<N> &s) {
  for (int i = 0; i < week.count; i++) s.add(week.at(i).t, week.at(i).p10 / 10.0);
}

} // namespace

void initHistory() {
  histPrefs.begin("hist", false);
  loadSeries("week", week);
  if (!loadSeries("month", month)) fillFromWeek(month);
  if (!loadSeries("year", year))   fillFromWeek(year);
  Serial.printf("History: %d hourly, %d 4-hour, %d daily points\n", week.count, month.count, year.count);
}

void addHistorySample(float percent) {
  uint32_t now = (uint32_t)time(nullptr);
  if (!clockValid(now)) return;
  day.add(now, percent);
  // Flash is written only when a time slot is finished
  if (week.add(now, percent))  saveSeries("week", week);     // once per hour
  if (month.add(now, percent)) saveSeries("month", month);   // every 4 hours
  if (year.add(now, percent))  saveSeries("year", year);     // once a day
}

// range = "day", "week", "month" or "year": only that one.
// Empty: day + week (what older versions of the page expect).
String getHistoryJson(const String &range) {
  String out;
  out.reserve(range.length() ? 7000 : 8000);
  out += "{\"now\":"; out += (uint32_t)time(nullptr);
  if (range == "" || range == "day")  { out += ",\"day\":";  day.toJson(out); }
  if (range == "" || range == "week") { out += ",\"week\":"; week.toJson(out); }
  if (range == "month") { out += ",\"month\":"; month.toJson(out); }
  if (range == "year")  { out += ",\"year\":";  year.toJson(out); }
  out += '}';
  return out;
}
