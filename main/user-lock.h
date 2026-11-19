// ============================================
// user-lock.h
// ============================================
// Optional PIN (4-8 digits) that protects the Settings part of the web page:
// tank setup, time, Wi-Fi networks. Without a PIN, settings are open.
//   - Saved in flash (namespace "lock").
//   - After 5 wrong tries, all tries are refused for 60 seconds.
//   - Forgot it? Hold the BOOT button 3 s: clears the PIN and the Wi-Fi list.
// Note: the page uses plain HTTP, so this keeps out curious people on the
// same network, not someone who records the network traffic.
#pragma once
#include <Arduino.h>

void initLock();
bool hasPin();                          // true if a PIN is set

// "" if the PIN is right (or none is set), otherwise an error message
String checkPin(const String &pin);

// Set a new PIN ("" = remove it). Returns an error message, or "" on success.
String setPin(const String &newPin);
void clearPin();                        // used by the BOOT-button reset
