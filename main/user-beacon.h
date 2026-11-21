// ============================================
// user-beacon.h
// ============================================
// Tells the tank display (max7219-tank-level) where we are and how many
// litres there are: a UDP broadcast "TANK1;<litres>" to port 4210 every 5 s.
// The display learns our IP from the sender address, so it keeps working
// when DHCP changes the address.
//
// The mDNS name stays "watertank" (see user-wifi.cpp): a second
// MDNS.begin() would rename the device and break OTA uploads.
#pragma once
#include <Arduino.h>

// Call often from loop() with the current litres; sends at most every 5 s.
// Does nothing when not on a Wi-Fi network or when litres < 0 (unknown).
void beaconLoop(float litres);
