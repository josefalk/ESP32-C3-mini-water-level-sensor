// ============================================
// user-lock.cpp
// ============================================
#include "user-lock.h"
#include <Preferences.h>

namespace {

Preferences lockPrefs;
String pinCode = "";                    // "" = no PIN set

const int MAX_FAILS = 5;                // wrong tries in a row...
const unsigned long BLOCK_TIME = 60000; // ...then refuse all tries for 60 s
int fails = 0;
unsigned long blockedSince = 0;

bool isPinFormat(const String &p) {
  if (p.length() < 4 || p.length() > 8) return false;
  for (unsigned i = 0; i < p.length(); i++)
    if (!isDigit(p[i])) return false;
  return true;
}

} // namespace

void initLock() {
  lockPrefs.begin("lock", false);
  pinCode = lockPrefs.getString("pin", "");
  Serial.println(pinCode.length() ? "Settings: PIN protected" : "Settings: no PIN");
}

bool hasPin() {
  return pinCode.length() > 0;
}

String checkPin(const String &pin) {
  if (!hasPin()) return "";

  if (fails >= MAX_FAILS) {
    unsigned long waited = millis() - blockedSince;
    if (waited < BLOCK_TIME)
      return "Too many wrong PINs. Try again in " + String((BLOCK_TIME - waited) / 1000 + 1) + " s";
    fails = 0;                          // waited long enough
  }

  if (pin == pinCode) {
    fails = 0;
    return "";
  }
  if (++fails >= MAX_FAILS) blockedSince = millis();
  return "Wrong PIN";
}

String setPin(const String &newPin) {
  if (newPin.length() && !isPinFormat(newPin)) return "The PIN must be 4 to 8 digits";
  pinCode = newPin;
  if (pinCode.length()) lockPrefs.putString("pin", pinCode);
  else lockPrefs.remove("pin");
  Serial.println(pinCode.length() ? "Settings: PIN set" : "Settings: PIN removed");
  return "";
}

void clearPin() {
  setPin("");
}
