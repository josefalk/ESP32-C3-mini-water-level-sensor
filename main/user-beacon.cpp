// ============================================
// user-beacon.cpp
// ============================================
#include "user-beacon.h"
#include "user-wifi.h"
#include <WiFi.h>
#include <WiFiUdp.h>

static const uint16_t BEACON_PORT = 4210;
static const uint32_t BEACON_INTERVAL_MS = 5000;

static WiFiUDP udp;
static uint32_t lastSent = 0;
static bool sentOnce = false;

void beaconLoop(float litres) {
  if (litres < 0 || !isWiFiConnected()) return;
  uint32_t now = millis();
  if (sentOnce && now - lastSent < BEACON_INTERVAL_MS) return;
  lastSent = now;
  sentOnce = true;
  udp.beginPacket(WiFi.broadcastIP(), BEACON_PORT);
  udp.printf("TANK1;%.1f", litres);
  udp.endPacket();
}
