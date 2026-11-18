// ============================================
// user-networks.h
// ============================================
// The list of saved Wi-Fi networks, in priority order:
// index 0 is the main network, index 1 the first backup, and so on.
// Stored in flash (namespace "wifi"), so it survives restarts.
// How the device moves between them is in user-wifi.cpp.
#pragma once
#include <Arduino.h>

const int MAX_NETWORKS = 5;

void initNetworks();                    // loads the list from flash
int getNetworkCount();
String getNetworkSsid(int i);           // "" if i is out of range
String getNetworkPassword(int i);
int findNetwork(const String &ssid);    // position in the list, or -1 if not saved

// These return an error message, or "" on success (and save to flash)
String saveNetwork(const String &ssid, const String &password);  // add at the end, or change the password
String deleteNetwork(int i);
String moveNetwork(int i, int dir);     // dir -1 = up (more preferred), +1 = down

void clearNetworks();                   // forget all networks (Wi-Fi reset button)
