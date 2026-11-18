// ============================================
// user-networks.cpp
// ============================================
#include "user-networks.h"
#include <Preferences.h>

// Default network for a brand-new board (or after the Wi-Fi reset button).
// It lives in secrets.h, which is NOT uploaded to GitHub. To set it, copy
// secrets.example.h to secrets.h and fill in your network.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWORD ""
#endif

namespace {

// Flash keys: "n" = how many, "s0".."s4" = names, "p0".."p4" = passwords
Preferences netPrefs;
String ssids[MAX_NETWORKS], passwords[MAX_NETWORKS];
int count = 0;

String key(char c, int i) { return String(c) + i; }

void removeKey(const String &k) {
  if (netPrefs.isKey(k.c_str())) netPrefs.remove(k.c_str());
}

void save() {
  netPrefs.putUChar("n", count);
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (i < count) {
      netPrefs.putString(key('s', i).c_str(), ssids[i]);
      netPrefs.putString(key('p', i).c_str(), passwords[i]);
    } else {
      removeKey(key('s', i));
      removeKey(key('p', i));
    }
  }
}

bool validIndex(int i) { return i >= 0 && i < count; }

void add(const String &ssid, const String &password) {
  if (count >= MAX_NETWORKS || ssid.length() == 0 || findNetwork(ssid) >= 0) return;
  ssids[count] = ssid;
  passwords[count] = password;
  count++;
}

} // namespace

void initNetworks() {
  netPrefs.begin("wifi", false);

  if (netPrefs.isKey("n")) {
    count = min((int)netPrefs.getUChar("n", 0), MAX_NETWORKS);
    for (int i = 0; i < count; i++) {
      ssids[i] = netPrefs.getString(key('s', i).c_str(), "");
      passwords[i] = netPrefs.getString(key('p', i).c_str(), "");
    }
  } else {
    // First boot with this version (or after a Wi-Fi reset): take over the
    // single network saved by older firmware, then add the secrets.h default.
    if (netPrefs.isKey("ssid")) {
      add(netPrefs.getString("ssid", ""), netPrefs.getString("password", ""));
      removeKey("ssid");
      removeKey("password");
    }
    add(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD);
    save();
  }

  Serial.printf("Wi-Fi: %d saved network(s)\n", count);
  for (int i = 0; i < count; i++) Serial.printf("  %d. %s\n", i + 1, ssids[i].c_str());
}

int getNetworkCount() { return count; }
String getNetworkSsid(int i)     { return validIndex(i) ? ssids[i] : String(); }
String getNetworkPassword(int i) { return validIndex(i) ? passwords[i] : String(); }

int findNetwork(const String &ssid) {
  for (int i = 0; i < count; i++)
    if (ssids[i] == ssid) return i;
  return -1;
}

String saveNetwork(const String &ssid, const String &password) {
  if (ssid.length() == 0)  return "Enter the network name";
  if (ssid.length() > 32)  return "Network name is too long (max 32 characters)";
  if (password.length() > 0 && password.length() < 8)
    return "Wi-Fi passwords are at least 8 characters (leave it empty for an open network)";
  if (password.length() > 63) return "Password is too long (max 63 characters)";

  int i = findNetwork(ssid);
  if (i < 0) {
    if (count >= MAX_NETWORKS)
      return "The list is full (max " + String(MAX_NETWORKS) + "), delete a network first";
    i = count++;
    ssids[i] = ssid;
  }
  passwords[i] = password;
  save();
  Serial.printf("Wi-Fi: saved '%s' as #%d\n", ssid.c_str(), i + 1);
  return "";
}

String deleteNetwork(int i) {
  if (!validIndex(i)) return "No such network";
  Serial.printf("Wi-Fi: deleted '%s'\n", ssids[i].c_str());
  for (int j = i; j < count - 1; j++) {
    ssids[j] = ssids[j + 1];
    passwords[j] = passwords[j + 1];
  }
  count--;
  save();
  return "";
}

String moveNetwork(int i, int dir) {
  int j = i + dir;
  if (!validIndex(i) || !validIndex(j)) return "Can't move it further";
  String s = ssids[i];     ssids[i] = ssids[j];         ssids[j] = s;
  String p = passwords[i]; passwords[i] = passwords[j]; passwords[j] = p;
  save();
  return "";
}

void clearNetworks() {
  netPrefs.clear();
  count = 0;
}
