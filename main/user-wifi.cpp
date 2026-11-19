// ============================================
// user-wifi.cpp
// ============================================
#include "user-wifi.h"
#include "user-led.h"
#include "user-screen.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <nvs.h>
#include "user-history.h"
#include "user-tank.h"
#include "user-networks.h"
#include "user-clock.h"
#include "user-lock.h"

// Network name of the device: the web page is at http://watertank.local
// and OTA uploads go to watertank.local (see platformio.ini).
const char* HOSTNAME = "watertank";
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "watertank-ota"   // set in platformio.ini build_flags
#endif

// Setup Access Point, started when none of the saved networks work
const char* ap_ssid = "ESP32-Config";
const char* ap_password = "";

// Create web server on port 80
WebServer server(80);

// Variables
int ledState = LOW;
// Wi-Fi reset = the board's own BOOT button (GPIO 9), no extra wiring needed.
// Hold it 3 s while the board is running. (Holding BOOT while pressing RESET
// or plugging in USB starts firmware-download mode instead -- that's normal.)
const int RESET_BUTTON_PIN = 9;
unsigned long buttonPressStart = 0;
bool buttonPressed = false;
bool isAPMode = false;    // true while the setup Access Point is on

// ---------------- Wi-Fi connection manager ----------------
// The saved networks (user-networks.h) are tried in list order: the first
// one that is in range and accepts the password wins. If none work, the
// setup Access Point starts and the list is tried again every minute.
// While connected to a lower network, the device checks every
// PRIORITY_CHECK_INTERVAL whether a higher one (e.g. the main network) is
// in range again, and switches to it.
// Everything runs step by step from loop(), so the sensor, screen and web
// page keep working while it connects.
const unsigned long CONNECT_TIMEOUT = 15000;                  // time one network gets to connect
const unsigned long SCAN_TIMEOUT = 15000;                     // a scan normally takes 2-4 s
const unsigned long LOST_GRACE = 20000;                       // after a drop, retry the same network this long...
const unsigned long RECONNECT_RETRY = 5000;                   // ...calling WiFi.reconnect() every 5 s
const unsigned long AP_RETRY_INTERVAL = 60000;                // in setup mode, retry the saved networks every 60 s
const unsigned long PRIORITY_CHECK_INTERVAL = 10 * 60000UL;   // on a lower network, look for a higher one every 10 min

enum WifiState {
  W_SCANNING,     // looking for which saved networks are in range
  W_CONNECTING,   // trying one network
  W_CONNECTED,    // online
  W_AP,           // setup Access Point on, waiting to retry
};
WifiState wState = W_AP;
unsigned long stateSince = 0;          // when the current state started

bool priorityScan = false;             // true: online, only looking for a higher network
int candidates[MAX_NETWORKS];          // networks to try, best first (positions in the saved list)
int candidateCount = 0, candidatePos = 0;
bool searchIfAllFail = false;          // when all candidates fail: true = scan again, false = setup mode
int requestedNetwork = -1;             // set by the web page: connect to this one now

String currentSsid = "";
unsigned long lostSince = 0, lastReconnect = 0, lastPriorityCheck = 0, lastAPRetry = 0;

void setLedAutoMode(bool state);
bool getScreenState();
void setScreenState(bool state);
void handleLed();
void handleScreen();

// Sensor data variables
float currentDistance = 0.0;
float currentPercent = 0.0;
bool sensorDataValid = false;

// Forward declarations
void checkResetButton();
void checkWiFiConnection();
void startScan(bool priority);
void startAPMode();
void handleRoot();
void handleScan();
void handleGetWifi();
void handleSaveWifi();
void handleDeleteWifi();
void handleMoveWifi();
void handleUseWifi();
void handleApiLevel();
void handleApiLevelText();
void handleGetTime();
void handleSetTime();
void handleSetClock();
void handleStatus();
void handleData();
void handleNotFound();
void handleGetTank();
void handleSetTank();
void handleSetValve();
int argIndex();
void sendResult(const String &err);
bool requirePin();
void handleGetLock();
void handleCheckLock();
void handleSetLock();

// ---------------- mDNS, OTA, clock ----------------
// Started once, the first time we join a Wi-Fi network. They keep working
// when the device later switches to another network.
bool networkServicesStarted = false;

void startNetworkServices() {
  if (networkServicesStarted) return;
  networkServicesStarted = true;

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Web page: http://%s.local\n", HOSTNAME);
  }

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("OTA update starting..."); showText("Updating..."); });
  ArduinoOTA.onEnd([]()   { Serial.println("OTA update done, restarting"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA error %u\n", e); });
  ArduinoOTA.begin();

  // Real time from the internet (time zone: see user-clock)
  startClockSync();
}

// Radio settings; must be applied again after every Wi-Fi mode change
void setRadio() {
  // ESP32-C3 Super Mini boards have a poorly matched antenna: at full TX power
  // the signal is distorted and neither STA nor AP works. Lower power fixes it.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setSleep(false);   // disable modem sleep -- a common cause of random silent disconnects
}

void initWiFi() {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  initNetworks();

  WiFi.mode(WIFI_STA);
  setRadio();
  if (getNetworkCount() > 0) {
    Serial.println("Wi-Fi: looking for saved networks...");
    startScan(false);   // setup mode starts only if none of them work
  } else {
    Serial.println("Wi-Fi: no saved networks");
    startAPMode();
  }

  server.on("/led", handleLed);
  server.on("/screen", handleScreen);
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/wifi", HTTP_GET, handleGetWifi);
  server.on("/wifi/save", HTTP_POST, handleSaveWifi);
  server.on("/wifi/delete", HTTP_POST, handleDeleteWifi);
  server.on("/wifi/move", HTTP_POST, handleMoveWifi);
  server.on("/wifi/use", HTTP_POST, handleUseWifi);
  server.on("/status", handleStatus);
  server.on("/data", handleData);
  server.on("/tank", HTTP_GET, handleGetTank);
  server.on("/tank", HTTP_POST, handleSetTank);
  server.on("/tank/valve", HTTP_POST, handleSetValve);
  server.on("/api/level", HTTP_GET, handleApiLevel);
  server.on("/api/level.txt", HTTP_GET, handleApiLevelText);
  server.on("/time", HTTP_GET, handleGetTime);
  server.on("/time", HTTP_POST, handleSetTime);
  server.on("/time/set", HTTP_POST, handleSetClock);
  server.on("/lock", HTTP_GET, handleGetLock);
  server.on("/lock/check", HTTP_POST, handleCheckLock);
  server.on("/lock/set", HTTP_POST, handleSetLock);
  server.on("/history", []() { server.send(200, "application/json", getHistoryJson()); });
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void handleWiFi() {
  server.handleClient();
  if (networkServicesStarted) ArduinoOTA.handle();
  checkResetButton();
  checkWiFiConnection();
}

bool isWiFiConnected() {
  return !isAPMode && WiFi.status() == WL_CONNECTED;
}

bool isWiFiAPMode() {
  return isAPMode;
}

String getWiFiIP() {
  if (isAPMode) {
    return WiFi.softAPIP().toString();
  } else {
    return WiFi.localIP().toString();
  }
}

bool isLedAutoMode() {
  return ledAutoMode;
}

unsigned long lastReadingAt = 0;   // millis() of the last valid reading

void updateSensorData(float distance, float percent) {
  currentDistance = distance;
  currentPercent = percent;
  sensorDataValid = true;
  lastReadingAt = millis();
}

void setSensorInvalid() {
  sensorDataValid = false;
}

void checkResetButton() {
  int buttonState = digitalRead(RESET_BUTTON_PIN);

  if (buttonState == LOW && !buttonPressed) {
    buttonPressed = true;
    buttonPressStart = millis();
    Serial.println("WiFi reset button pressed...");
  } else if (buttonState == LOW && buttonPressed) {
    unsigned long pressDuration = millis() - buttonPressStart;

    if (pressDuration >= 3000) {
      Serial.println("Reset button held for 3 seconds!");
      Serial.println("Forgetting all saved Wi-Fi networks and the settings PIN, restarting...");

      ledOn(255, 0, 0);
      delay(500);
      ledOff();
      delay(200);
      ledOn(255, 0, 0);
      delay(500);
      ledOff();

      clearNetworks();
      clearPin();                 // so a forgotten PIN can't lock you out

      Serial.println("Restarting...");
      delay(500);

      ESP.restart();
    }
  } else if (buttonState == HIGH && buttonPressed) {
    unsigned long pressDuration = millis() - buttonPressStart;
    Serial.printf("Button released after %lu ms\n", pressDuration);
    buttonPressed = false;
  }
}

// ---- Connection manager steps ----

void setState(WifiState s) {
  wState = s;
  stateSince = millis();
}

// Position of the current network in the saved list (0 = main).
// A network that isn't in the list (any more) ranks below all saved ones.
int currentRank() {
  int i = findNetwork(currentSsid);
  return i < 0 ? getNetworkCount() : i;
}

// Start a background scan; the result is picked up in checkWiFiConnection()
void startScan(bool priority) {
  priorityScan = priority;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  setState(W_SCANNING);
}

void connectToCandidate() {
  int i = candidates[candidatePos];
  Serial.printf("Wi-Fi: trying '%s' (#%d in the list)\n", getNetworkSsid(i).c_str(), i + 1);
  WiFi.disconnect();
  WiFi.begin(getNetworkSsid(i).c_str(), getNetworkPassword(i).c_str());
  setState(W_CONNECTING);
}

void onConnected() {
  currentSsid = WiFi.SSID();
  if (isAPMode) {
    WiFi.softAPdisconnect(true);   // setup network not needed any more
    isAPMode = false;
    setRadio();
  }
  Serial.printf("Wi-Fi: connected to '%s', IP %s, signal %d dBm\n",
                currentSsid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (currentRank() > 0)
    Serial.printf("Wi-Fi: not the main network; checking for a higher one every %lu min\n",
                  PRIORITY_CHECK_INTERVAL / 60000);
  lostSince = 0;
  lastPriorityCheck = millis();
  setState(W_CONNECTED);
  startNetworkServices();
}

// Every network in the candidate list failed
void candidatesFailed() {
  if (searchIfAllFail) {
    startScan(false);          // fall back to the best network that is in range
  } else {
    Serial.println("Wi-Fi: none of the saved networks worked");
    startAPMode();
  }
}

// found = number of networks the scan saw, or < 0 if the scan failed
void onScanDone(int found) {
  if (priorityScan && found < 0) {   // couldn't scan: just stay where we are
    setState(W_CONNECTED);
    return;
  }

  // Saved networks that are in range, best first. A priority scan only
  // looks at the networks above the current one.
  int limit = priorityScan ? currentRank() : getNetworkCount();
  candidateCount = 0;
  candidatePos = 0;
  for (int i = 0; i < limit; i++) {
    bool visible = found < 0;        // scan failed: try them all
    for (int j = 0; j < found && !visible; j++)
      visible = WiFi.SSID(j) == getNetworkSsid(i);
    if (visible) candidates[candidateCount++] = i;
  }
  if (found >= 0 && !priorityScan) {
    Serial.printf("Wi-Fi: %d of %d saved networks in range\n", candidateCount, getNetworkCount());
    if (candidateCount == 0) {
      // Show what the board can see, to help find the problem
      Serial.printf("  Networks the ESP can see (2.4 GHz only): %d\n", found);
      for (int j = 0; j < found; j++)
        Serial.printf("    %-32s  signal %d dBm\n", WiFi.SSID(j).c_str(), WiFi.RSSI(j));
    }
  }
  WiFi.scanDelete();

  if (priorityScan) {
    if (candidateCount == 0) {       // nothing better in range: stay
      setState(W_CONNECTED);
      return;
    }
    Serial.printf("Wi-Fi: higher network '%s' is in range, switching\n",
                  getNetworkSsid(candidates[0]).c_str());
    searchIfAllFail = true;          // if it doesn't work, go back to any network
    connectToCandidate();
    return;
  }

  searchIfAllFail = false;
  if (candidateCount > 0) connectToCandidate();
  else candidatesFailed();
}

void startAPMode() {
  WiFi.disconnect();                 // stop the station from searching on its own
  if (!isAPMode) {
    // AP for the setup page + station, so the saved networks can still be retried
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_password);
    setRadio();
    isAPMode = true;
    Serial.printf("Setup mode: join Wi-Fi '%s' and open http://%s\n",
                  ap_ssid, WiFi.softAPIP().toString().c_str());
  }
  lastAPRetry = millis();
  setState(W_AP);
}

// Runs every loop(): moves the connection manager one step forward
void checkWiFiConnection() {
  unsigned long now = millis();

  // "Connect" button on the web page (can't start while a scan is running)
  if (requestedNetwork >= 0 && wState != W_SCANNING) {
    candidates[0] = requestedNetwork;
    candidateCount = 1;
    candidatePos = 0;
    requestedNetwork = -1;
    searchIfAllFail = true;          // if it doesn't work, go back to the best network in range
    connectToCandidate();
    return;
  }

  switch (wState) {
    case W_SCANNING: {
      int found = WiFi.scanComplete();
      if (found == WIFI_SCAN_RUNNING) {
        if (now - stateSince < SCAN_TIMEOUT) break;
        found = WIFI_SCAN_FAILED;
      }
      onScanDone(found);
      break;
    }

    case W_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        onConnected();
      } else if (now - stateSince > CONNECT_TIMEOUT) {
        int st = WiFi.status();
        Serial.printf("Wi-Fi: '%s' failed (%s)\n", getNetworkSsid(candidates[candidatePos]).c_str(),
                      st == WL_NO_SSID_AVAIL ? "not in range" :
                      st == WL_CONNECT_FAILED ? "wrong password?" : "timed out");
        if (++candidatePos < candidateCount) connectToCandidate();
        else candidatesFailed();
      }
      break;

    case W_CONNECTED:
      if (WiFi.status() == WL_CONNECTED) {
        lostSince = 0;
        if (currentRank() > 0 && now - lastPriorityCheck > PRIORITY_CHECK_INTERVAL) {
          lastPriorityCheck = now;
          startScan(true);
        }
      } else if (lostSince == 0) {
        lostSince = lastReconnect = now;
        Serial.printf("Wi-Fi: lost connection to '%s', reconnecting...\n", currentSsid.c_str());
        WiFi.reconnect();
      } else if (now - lostSince > LOST_GRACE) {
        Serial.println("Wi-Fi: still disconnected, trying the other saved networks");
        lostSince = 0;
        startScan(false);
      } else if (now - lastReconnect > RECONNECT_RETRY) {
        lastReconnect = now;
        WiFi.reconnect();
      }
      break;

    case W_AP:
      if (getNetworkCount() > 0 && now - lastAPRetry > AP_RETRY_INTERVAL) startScan(false);
      break;
  }
}

// Quote a string for JSON (network names may contain quotes)
String jsonStr(const String &s) {
  String out = "\"";
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if ((uint8_t)c < 0x20) continue;
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

// "Wi-Fi Networks" part of the web page: the saved list (reorder, switch,
// delete) and a form to add a network. Shown both online and in setup mode.
void appendWifiSection(String &html) {
  html += R"HTML(
<h2 style='margin-top:30px'>Wi-Fi Networks</h2>
<p style='color:var(--muted);font-size:14px'>The device joins the highest network in this list that works.
  When it is on a lower one, it checks every <span id='chkMin'>10</span> minutes whether a higher one
  is back in range and switches to it. If none work, it opens the <b>ESP32-Config</b> setup network.</p>
<div id='netList'><p>Loading...</p></div>
<h3 style='color:var(--muted)'>Add a network</h3>
<button class='scan' onclick='scanNetworks()'>📡 Scan Networks</button>
<div id='networks' class='network-list'></div>
<input type='text' id='ssid' placeholder='Network name (SSID)' maxlength='32'>
<input type='password' id='password' placeholder='Password (empty for an open network)' maxlength='63'>
<button class='connect-btn' onclick='saveNet()'>Save network</button>
<p style='color:var(--faint);font-size:13px'>New networks go to the bottom of the list; use ▲ to make one the main network.
  Saving a name that is already in the list changes its password.</p>
<script>
let nets = [], scanned = [];
function esc(s){ return String(s).replace(/[&<>"']/g, c => '&#' + c.charCodeAt(0) + ';'); }
function loadNets(){
  fetch('/wifi').then(r=>r.json()).then(d=>{
    nets = d.nets;
    document.getElementById('chkMin').textContent = d.checkMins;
    let h = '';
    nets.forEach((s, i) => {
      const on = s == d.current;
      h += `<div class='network-item' style='display:flex;align-items:center;gap:6px;cursor:default'>
        <div style='flex:1;overflow-wrap:anywhere'><b>${i+1}. ${esc(s)}</b>${i==0 ? ' <small>(main)</small>' : ''}
          ${on ? "<br><small style='color:#4CAF50'>● connected</small>" : ''}</div>
        <button class='mini' title='Higher priority' onclick='moveNet(${i},-1)' ${i==0 ? 'disabled' : ''}>▲</button>
        <button class='mini' title='Lower priority' onclick='moveNet(${i},1)' ${i==nets.length-1 ? 'disabled' : ''}>▼</button>
        ${on ? '' : `<button class='mini on' onclick='useNet(${i})'>Connect</button>`}
        <button class='mini off' title='Delete' onclick='delNet(${i})'>✕</button></div>`;
    });
    document.getElementById('netList').innerHTML = h || '<p>No networks saved yet.</p>';
  });
}
function moveNet(i, dir){ post('/wifi/move', `i=${i}&dir=${dir}`).then(m => { if (m != 'OK') alert(m); loadNets(); }); }
function delNet(i){
  if (!confirm(`Delete "${nets[i]}" from the list?`)) return;
  post('/wifi/delete', 'i=' + i).then(m => { if (m != 'OK') alert(m); loadNets(); });
}
function useNet(i){
  if (!confirm(`Switch to "${nets[i]}" now?\n\nThis page will lose its connection. Join that network ` +
               `on your phone or PC and open http://watertank.local again.`)) return;
  post('/wifi/use', 'i=' + i).then(m => alert(m));
}
function saveNet(){
  const s = document.getElementById('ssid').value, p = document.getElementById('password').value;
  if (!s){ alert('Enter the network name'); return; }
  post('/wifi/save', 'ssid=' + encodeURIComponent(s) + '&password=' + encodeURIComponent(p)).then(m => {
    alert(m);
    if (!m.startsWith('Error')){ document.getElementById('password').value = ''; loadNets(); }
  });
}
function scanNetworks(){
  const box = document.getElementById('networks');
  box.innerHTML = '<p>Scanning...</p>';
  fetch('/scan').then(r=>r.json()).then(d=>{
    if (d.busy){ box.innerHTML = '<p>The device is busy connecting. Try again in a few seconds.</p>'; return; }
    scanned = d.networks;
    box.innerHTML = scanned.map((n, i) => `<div class='network-item' onclick='pickNet(${i})'>
      <strong>${esc(n.ssid)}</strong> (${n.rssi} dBm)${n.secure ? ' 🔒' : ' (open)'}</div>`).join('')
      || '<p>No networks found.</p>';
  });
}
function pickNet(i){
  document.getElementById('ssid').value = scanned[i].ssid;
  document.getElementById('password').value = '';
  if (scanned[i].secure) document.getElementById('password').focus();
}
window.addEventListener('load', loadNets);
</script>
)HTML";
}

// "Time" part of the web page: local time, time zone (GMT offset + summer
// time), and setting the clock from the phone when there is no internet.
void appendTimeSection(String &html) {
  html += R"HTML(
<h2 style='margin-top:30px'>Time</h2>
<p style='font-size:18px'>Device time: <b id='devTime'>--</b> <small id='tzName' style='color:var(--muted)'></small></p>
<div class='calibration-group'>
  <div class='calibration-input'><label>Time zone:</label>
    <select id='tzSel' style='padding:10px;border:1px solid var(--border);border-radius:5px'></select></div>
  <div class='calibration-input'><label>Summer time:</label>
    <label style='padding-top:10px'><input type='checkbox' id='tzSummer' style='width:auto;margin:0 6px 0 0'>+1 hour</label></div>
</div>
<button class='scan' onclick='useBrowserTz()'>Use this phone's time zone</button>
<button class='save-calibration' onclick='saveTz()'>Save time zone</button>
<p id='clockNote' style='color:var(--muted);font-size:13px'></p>
<script>
let clk = null, clkAt = 0, tzMin = null;
const fmtTz = m => 'GMT' + (m < 0 ? '-' : '+') + Math.floor(Math.abs(m) / 60) +
                   (Math.abs(m) % 60 ? ':' + String(Math.abs(m) % 60).padStart(2, '0') : '');
(function(){
  const sel = document.getElementById('tzSel');
  const half = [-570, -210, 210, 270, 330, 345, 390, 525, 570, 630, 765];
  const list = [];
  for (let h = -12; h <= 14; h++) list.push(h * 60);
  list.push(...half);
  list.sort((a, b) => a - b).forEach(m => sel.add(new Option(fmtTz(m), m)));
})();
function loadTime(){
  fetch('/time').then(r=>r.json()).then(d=>{
    const first = clk == null;
    clk = d; clkAt = Date.now(); tzMin = d.tz;
    if (first){
      document.getElementById('tzSel').value = d.offset;
      document.getElementById('tzSummer').checked = d.summer;
    }
    document.getElementById('tzName').textContent = '(' + fmtTz(d.tz) + ')';
    document.getElementById('clockNote').innerHTML = d.valid
      ? 'The clock is set from the internet automatically.'
      : "The clock isn't set (no internet). <button class='mini on' onclick='setClockFromPhone()'>Set from this phone</button>";
    showTime();
    if (typeof drawChart == 'function') drawChart();
  });
}
function showTime(){
  if (!clk || !clk.valid) { document.getElementById('devTime').textContent = '--'; return; }
  const t = clk.now + (Date.now() - clkAt) / 1000 + clk.tz * 60;
  const d = new Date(t * 1000);
  document.getElementById('devTime').textContent =
    d.toLocaleDateString(undefined, {weekday:'short', day:'numeric', month:'short', timeZone:'UTC'}) + ' ' +
    String(d.getUTCHours()).padStart(2, '0') + ':' + String(d.getUTCMinutes()).padStart(2, '0');
}
function useBrowserTz(){
  const m = -new Date().getTimezoneOffset();   // includes summer time if it is on now
  const sel = document.getElementById('tzSel');
  if (![...sel.options].some(o => +o.value == m)) { alert('Unusual time zone: ' + fmtTz(m)); return; }
  sel.value = m;
  document.getElementById('tzSummer').checked = false;
  alert('Selected ' + fmtTz(m) + '. Press "Save time zone" to keep it.');
}
function saveTz(){
  const body = 'offset=' + document.getElementById('tzSel').value +
               '&summer=' + (document.getElementById('tzSummer').checked ? 1 : 0);
  post('/time', body).then(m => { alert(m == 'OK' ? 'Time zone saved.' : m); loadTime(); });
}
function setClockFromPhone(){
  post('/time/set', 'now=' + Math.floor(Date.now() / 1000)).then(m => { if (m != 'OK') alert(m); loadTime(); });
}
window.addEventListener('load', () => { loadTime(); setInterval(showTime, 1000); setInterval(loadTime, 60000); });
</script>
)HTML";
}

// "Tank Setup" part of Settings: height + sensor gap + one or more parallel
// tanks. Its script is in handleRoot() (it also drives the litres display).
void appendTankSection(String &html) {
  html += R"HTML(
<h2>Tank Setup</h2>
<p style='color:var(--muted);font-size:14px'>All sizes are <b>inside</b> measurements in cm.</p>
<div class='calibration-group'>
  <div class='calibration-input'><label>Tank height (cm):</label>
    <input type='number' id='tH' step='0.1' min='0'>
    <small style='color:var(--faint)'>Bottom of the tank up to the full water line.</small></div>
  <div class='calibration-input'><label>Sensor gap (cm):</label>
    <input type='number' id='tG' step='0.1' min='0'>
    <small style='color:var(--faint)'>Sensor face down to the water when the tank is full.</small></div>
</div>
<div id='tankList'></div>
<div id='sensorPick'></div>
<button class='scan' id='addTankBtn' onclick='addTank()'>+ Add tank</button>
<p style='color:var(--muted);font-size:13px'>Tanks connected at the bottom (in parallel) fill to the same level,
  so they must all be the same height. Their shape, length, width and diameter may differ.
  The sensor can sit in any one of them; choose which, so closing a valve is counted right.</p>
<button class='save-calibration' onclick='saveTank()'>Save Tank Setup</button>
<p id='tankSummary' style='color:var(--text);font-size:14px'></p>
)HTML";
}

// Start of the "Settings" part of the page: a fold-out box that asks for the
// PIN first when one is set. open = start unfolded (setup mode).
void appendSettingsStart(String &html, bool open) {
  html += "<details id='settingsBox' style='margin-top:30px'";
  if (open) html += " open";
  html += R"HTML(>
<summary style='font-size:22px;font-weight:bold;color:var(--muted);cursor:pointer;padding:10px 0;border-bottom:2px solid #4CAF50'>⚙ Settings</summary>
<div id='lockBox' style='display:none'>
  <p>The settings are protected with a PIN.</p>
  <input type='password' id='pinIn' inputmode='numeric' maxlength='8' placeholder='PIN'
    onkeydown="if (event.key == 'Enter') unlock()">
  <button class='connect-btn' onclick='unlock()'>🔒 Unlock</button>
</div>
<div id='settingsBody' style='display:none'>
)HTML";
}

// End of Settings: the PIN controls, then the script that locks/unlocks.
void appendSettingsEnd(String &html) {
  html += R"HTML(
<h2 style='margin-top:30px'>Settings PIN</h2>
<p id='pinState' style='color:var(--muted);font-size:14px'></p>
<input type='password' id='newPin' inputmode='numeric' maxlength='8' placeholder='New PIN (4-8 digits)'>
<button class='save-calibration' onclick='savePin()'>Save PIN</button>
<button class='off' id='rmPinBtn' style='width:100%;display:none' onclick='removePin()'>Remove PIN</button>
<button class='scan' id='lockBtn' style='display:none;background:#607D8B' onclick='lockNow()'>🔒 Lock settings now</button>
<p style='color:var(--faint);font-size:13px'>Forgot the PIN? Hold the BOOT button on the board for 3 seconds.
  This removes the PIN <b>and</b> all saved Wi-Fi networks.</p>
</div>
</details>
<script>
let hasPin = false;
function keepPin(p){ PIN = p; }
function showLock(){
  const locked = hasPin && !PIN;
  document.getElementById('lockBox').style.display = locked ? '' : 'none';
  document.getElementById('settingsBody').style.display = locked ? 'none' : '';
  document.getElementById('rmPinBtn').style.display = hasPin ? '' : 'none';
  document.getElementById('lockBtn').style.display = hasPin ? '' : 'none';
  document.getElementById('pinState').textContent = hasPin
    ? 'A PIN is set: it is needed to change the settings.'
    : 'No PIN: anyone on your Wi-Fi can change the settings. Set a PIN to protect them.';
}
function loadLock(){
  fetch('/lock').then(r=>r.json()).then(d=>{
    hasPin = d.pin;
    keepPin('');
    showLock();
  });
}
// Folding Settings closed locks them again
window.addEventListener('load', () => {
  document.getElementById('settingsBox').addEventListener('toggle', e => {
    if (!e.target.open && hasPin){ keepPin(''); showLock(); }
  });
});
function unlock(){
  const p = document.getElementById('pinIn').value;
  if (!p) return;
  keepPin(p);
  post('/lock/check', '').then(m => {
    if (m != 'OK'){ keepPin(''); alert(m); }
    document.getElementById('pinIn').value = '';
    showLock();
  });
}
function lockNow(){ keepPin(''); showLock(); }
function savePin(){
  const p = document.getElementById('newPin').value;
  if (!/^[0-9]{4,8}$/.test(p)){ alert('The PIN must be 4 to 8 digits'); return; }
  post('/lock/set', 'new=' + p).then(m => {
    if (m != 'OK'){ alert(m); return; }
    hasPin = true; keepPin('');     // lock now: unlocking checks you know the new PIN
    document.getElementById('newPin').value = '';
    alert('PIN saved. The settings are locked now: enter the PIN to open them again.');
    showLock();
  });
}
function removePin(){
  if (!confirm('Remove the PIN? Anyone on your Wi-Fi will be able to change the settings.')) return;
  post('/lock/set', 'new=').then(m => {
    if (m != 'OK'){ alert(m); return; }
    hasPin = false; keepPin('');
    showLock();
  });
}
window.addEventListener('load', loadLock);
</script>
)HTML";
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'>";   // so symbols like ▲ ▼ 📡 show correctly
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";

  // Light / dark theme: "auto" follows the phone/PC; the choice is kept in
  // this browser only. Set before the page draws, so it doesn't flash white.
  html += R"HTML(<script>
const THEMES = {auto:'◐ Auto', dark:'🌙 Dark', light:'☀ Light'};
let theme = 'auto';
try { theme = localStorage.getItem('theme') || 'auto'; } catch(e) {}
function applyTheme(){
  if (theme == 'auto') delete document.documentElement.dataset.theme;
  else document.documentElement.dataset.theme = theme;
  const b = document.getElementById('themeBtn');
  if (b) b.textContent = THEMES[theme];
}
function cycleTheme(){
  theme = theme == 'auto' ? 'dark' : theme == 'dark' ? 'light' : 'auto';
  try { localStorage.setItem('theme', theme); } catch(e) {}
  applyTheme();
}
applyTheme();
window.addEventListener('DOMContentLoaded', applyTheme);
</script>)HTML";
  html += "<style>";
  // Colours are CSS variables so the page can switch between light and dark.
  // "Auto" follows the phone/PC setting; the button at the top forces one.
  html += R"CSS(
:root { color-scheme: light;
  --bg:#f5f5f5; --card:#fff; --panel:#f8f9fa; --item:#f0f0f0; --item-hover:#e0e0e0;
  --text:#333; --muted:#666; --faint:#888; --border:#ddd; --grid:#e5e5e5; --grid2:#f0f0f0;
  --input:#fff; --info:#e3f2fd; --warn:#fff3cd; --track:#e0e0e0; --shadow:rgba(0,0,0,0.1); }
@media (prefers-color-scheme: dark) { :root:not([data-theme='light']) { color-scheme: dark;
  --bg:#121417; --card:#1e2227; --panel:#262b31; --item:#2c3239; --item-hover:#363d45;
  --text:#e6e6e6; --muted:#a9b0b8; --faint:#8a929b; --border:#3d444c; --grid:#333a42; --grid2:#2a3037;
  --input:#262b31; --info:#1c2c3a; --warn:#3a3220; --track:#363d45; --shadow:rgba(0,0,0,0.5); } }
:root[data-theme='dark'] { color-scheme: dark;
  --bg:#121417; --card:#1e2227; --panel:#262b31; --item:#2c3239; --item-hover:#363d45;
  --text:#e6e6e6; --muted:#a9b0b8; --faint:#8a929b; --border:#3d444c; --grid:#333a42; --grid2:#2a3037;
  --input:#262b31; --info:#1c2c3a; --warn:#3a3220; --track:#363d45; --shadow:rgba(0,0,0,0.5); }
body { font-family: Arial, sans-serif; margin: 20px; background: var(--bg); color: var(--text); }
.container { max-width: 600px; margin: 0 auto; background: var(--card); padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px var(--shadow); }
h1 { color: var(--text); text-align: center; }
h2, h3 { color: var(--muted); }
h2 { border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }
button { padding: 12px 24px; margin: 5px; font-size: 16px; cursor: pointer; border: none; border-radius: 5px; color: white; }
.scan { background-color: #2196F3; width: 100%; }
.on { background-color: #4CAF50; }
.off { background-color: #f44336; }
button:hover { opacity: 0.8; }
.info { margin-top: 20px; padding: 15px; background: var(--info); border-radius: 5px; }
.network-list { margin-top: 20px; }
.network-item { padding: 10px; margin: 5px 0; background: var(--item); border-radius: 5px; cursor: pointer; }
.network-item:hover { background: var(--item-hover); }
input, select { background: var(--input); color: var(--text); }
input { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid var(--border); border-radius: 5px; box-sizing: border-box; }
.connect-btn { background-color: #4CAF50; width: 100%; }
.warning { background: var(--warn); border-left: 4px solid #ffc107; padding: 10px; margin: 10px 0; }
.tank-container { margin: 20px 0; padding: 20px; background: var(--panel); border-radius: 10px; }
.tank { width: 100%; height: 30px; background: var(--track); border: 2px solid var(--text); border-radius: 5px; position: relative; overflow: hidden; }
.tank-fill { height: 100%; background: linear-gradient(90deg, #4CAF50, #8BC34A); transition: width 0.5s; }
.tank-fill.low { background: linear-gradient(90deg, #f44336, #ff5722); }
.tank-fill.medium { background: linear-gradient(90deg, #FF9800, #FFC107); }
.sensor-data { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 15px; }
.sensor-item { padding: 15px; background: var(--card); border-radius: 5px; text-align: center; }
.sensor-label { font-size: 12px; color: var(--muted); }
.sensor-value { font-size: 24px; font-weight: bold; color: var(--text); }
.calibration-group { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin: 15px 0; }
.calibration-input { display: flex; flex-direction: column; }
.calibration-input label { font-size: 14px; color: var(--muted); margin-bottom: 5px; }
.save-calibration { background-color: #FF9800; width: 100%; }
.range { background: var(--track); color: var(--text); padding:8px 16px; } .range.on { background:#2196F3; color:white; }
select { width:100%; font-size:16px; }
.mini { padding:6px 10px; margin:0; font-size:14px; background:#607D8B; } .mini.on { background:#4CAF50; } .mini.off { background:#f44336; } button:disabled { opacity:0.3; cursor:default; }
.theme-btn { float:right; margin:0; padding:6px 10px; font-size:14px; background:var(--item); color:var(--text); border:1px solid var(--border); }
)CSS";
  html += "</style>";
  html += "<script>";

  // -----------------------------
  // LED + SCREEN EXTRA JAVASCRIPT
  // -----------------------------
  html += "function led(state){ fetch('/led?state='+state).then(r=>r.text()).then(console.log); }";
  html += "function screen(state){ fetch('/screen?state='+state).then(r=>r.text()).then(console.log); }";

  // Send a setting to the device. The settings PIN (if one is set and the
  // user has unlocked the Settings) goes along with every request.
  html += R"HTML(
let PIN = '';   // only kept while the page is open; a refresh asks again
function post(url, body){
  body = (body ? body + '&' : '') + 'pin=' + encodeURIComponent(PIN);
  return fetch(url, {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body})
    .then(r => r.text());
}
)HTML";

  // Auto-refresh sensor data
  html += "function updateSensorData() {";
  html += "  fetch('/data').then(r => r.json()).then(data => {";
  html += "    if (data.valid) {";
  html += "      document.getElementById('distance').textContent = data.distance.toFixed(1);";
  html += "      document.getElementById('percent').textContent = data.percent.toFixed(0);";
  html += "      const fill = document.getElementById('tankFill');";
  html += "      fill.style.width = data.percent + '%';";
  html += "      fill.className = 'tank-fill';";
  html += "      if (data.percent < 20) fill.className = 'tank-fill low';";
  html += "      else if (data.percent < 70) fill.className = 'tank-fill medium';";
  html += "    }";
  html += "  });";
  html += "}";
  html += "setInterval(updateSensorData, 1000);";
  html += "updateSensorData();";

  html += "</script>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<button id='themeBtn' class='theme-btn' title='Light / dark' onclick='cycleTheme()'>◐ Auto</button>";
  html += "<h1>Water Monitor</h1>";

  if (isAPMode) {
    html += "<div class='warning'>⚠ <strong>Setup Mode</strong><br>None of the saved Wi-Fi networks could be joined. "
            "Add or fix a network below. The device also retries the saved networks every minute.</div>";
    appendSettingsStart(html, true);
    appendWifiSection(html);
    appendTimeSection(html);
    appendSettingsEnd(html);

  } else {

    html += "<div class='tank-container'>";
    html += "<h2>Water Tank Level</h2>";
    html += "<div class='tank'>";
    html += "<div id='tankFill' class='tank-fill' style='width: 0%'></div>";
    html += "</div>";

    html += "<div class='sensor-data'>";
    html += "<div class='sensor-item'>";
    html += "<div class='sensor-label'>Distance</div>";
    html += "<div class='sensor-value'><span id='distance'>--</span> cm</div>";
    html += "</div>";
    html += "<div class='sensor-item'>";
    html += "<div class='sensor-label'>Level</div>";
    html += "<div class='sensor-value'><span id='percent'>--</span> %</div>";
    html += "</div>";
    html += "<div class='sensor-item' id='litresBox' style='grid-column: span 2; display:none'>";
    html += "<div class='sensor-label' id='litresLabel'>Water in tank</div>";
    html += "<div class='sensor-value'><span id='litres'>--</span> L <small style='font-size:14px;color:var(--muted)'>of <span id='capacity'>--</span> L</small></div>";
    html += "</div>";
    html += "</div>";
    html += "<div id='valveBox'></div>";   // valve switches, when there are 2+ tanks
    html += "</div>";

    // History graph (drawn by the script below)
    html += R"HTML(
<h2>History</h2>
<div>
  <button id='rDay' class='range on' onclick="setRange('day')">24 hours</button>
  <button id='rWeek' class='range' onclick="setRange('week')">7 days</button>
</div>
<div id='chartWrap' style='margin-top:10px'><svg id='chart' viewBox='0 0 560 210' style='width:100%;height:auto'></svg></div>
<p id='chartNote' style='color:var(--muted);font-size:13px'></p>
)HTML";

    // Scripts for the tank setup (in Settings), litres display and history chart
    html += R"HTML(
<script>
const SHAPES = {1:'Box (rectangular)', 2:'Round, standing upright', 3:'Round, lying on its side'};
const MAX_TANKS = 4;
let tanks = [], sensorTank = 0;

function field(i, key, label){
  return `<div class='calibration-input'><label>${label} (cm):</label>
    <input type='number' step='0.1' min='0' value='${tanks[i][key] || ''}'
      oninput='tanks[${i}].${key} = parseFloat(this.value) || 0'></div>`;
}
function renderTanks(){
  let h = '';
  tanks.forEach((t, i) => {
    h += `<div style='border:1px solid var(--border);border-radius:8px;padding:10px;margin:10px 0'>
      <div style='display:flex;justify-content:space-between;align-items:center'>
        <b>Tank ${i+1}${tanks.length > 1 && i == sensorTank ? ' <small>(sensor)</small>' : ''}</b>
        <button style='background:#f44336;padding:4px 10px;font-size:13px' onclick='removeTank(${i})'>Remove</button>
      </div>
      <select onchange='tanks[${i}].shape = +this.value; renderTanks()'
        style='padding:10px;margin:10px 0;border:1px solid var(--border);border-radius:5px'>
        ${Object.entries(SHAPES).map(([v, n]) => `<option value='${v}' ${t.shape == v ? 'selected' : ''}>${n}</option>`).join('')}
      </select>
      <div class='calibration-group' style='margin:0'>`;
    if (t.shape == 1) h += field(i, 'length', 'Length') + field(i, 'width', 'Width');
    if (t.shape == 2) h += field(i, 'diameter', 'Diameter');
    if (t.shape == 3) h += field(i, 'length', 'Length') +
      `<div class='calibration-input'><label>Diameter:</label><small style='color:var(--faint);padding-top:12px'>
        = tank height</small></div>`;
    h += `</div></div>`;
  });
  document.getElementById('tankList').innerHTML = h;
  // Which tank holds the sensor (matters when a valve is closed)
  document.getElementById('sensorPick').innerHTML = tanks.length < 2 ? '' :
    `<div class='calibration-input'><label>The sensor is in:</label>
      <select onchange='sensorTank = +this.value; renderTanks()'
        style='padding:10px;margin-bottom:10px;border:1px solid var(--border);border-radius:5px'>
      ${tanks.map((t, i) => `<option value='${i}' ${i == sensorTank ? 'selected' : ''}>Tank ${i+1}</option>`).join('')}
      </select></div>`;
  const btn = document.getElementById('addTankBtn');
  btn.textContent = tanks.length ? '+ Add parallel tank' : '+ Add tank';
  btn.style.display = tanks.length >= MAX_TANKS ? 'none' : '';
}
function addTank(){
  tanks.push({shape: 1, length: 0, width: 0, diameter: 0});
  renderTanks();
}
function removeTank(i){
  tanks.splice(i, 1);
  if (i < sensorTank || sensorTank >= tanks.length) sensorTank = Math.max(0, sensorTank - 1);
  renderTanks();
}
function loadTank(){
  fetch('/tank').then(r=>r.json()).then(d=>{
    document.getElementById('tH').value = d.height;
    document.getElementById('tG').value = d.gap;
    tanks = d.tanks;
    sensorTank = d.sensor;
    renderTanks();
    renderValves(d);
  });
}

// ---- Valves (shown under the level when there are 2+ tanks) ----
function loadValves(){ fetch('/tank').then(r=>r.json()).then(renderValves); }
function renderValves(d){
  const box = document.getElementById('valveBox');
  if (d.tanks.length < 2){ box.innerHTML = ''; return; }
  let h = "<div class='sensor-label' style='margin:15px 0 5px'>Valves: a closed tank is not counted</div>";
  d.tanks.forEach((t, i) => {
    const note = t.measured ? 'valve open, counted'
      : !t.open ? 'valve closed' + (t.held >= 0 ? `, holds about ${t.held} L` : '')
      : 'not measured: the sensor tank is closed';
    h += `<div class='network-item' style='display:flex;align-items:center;gap:8px;cursor:default;background:var(--card)'>
      <div style='flex:1'><b>Tank ${i+1}</b>${i == d.sensor ? ' <small>(sensor)</small>' : ''}<br>
        <small style='color:${t.measured ? '#4CAF50' : 'var(--faint)'}'>${note}</small></div>
      <button class='mini ${t.open ? 'off' : 'on'}' onclick='setValve(${i},${t.open ? 0 : 1})'>
        ${t.open ? 'Close valve' : 'Open valve'}</button></div>`;
  });
  box.innerHTML = h;
}
function setValve(i, open){
  post('/tank/valve', `i=${i}&open=${open}`).then(m => {
    if (m.includes('Wrong PIN') && !PIN){      // settings are locked: ask once
      const p = prompt('Enter the settings PIN');
      if (!p) return;
      PIN = p;
      return setValve(i, open);
    }
    if (m != 'OK'){ if (m.includes('PIN')) PIN = ''; alert(m); }
    if (typeof showLock == 'function') showLock();
    loadValves(); updateLitres();
  });
}

function saveTank(){
  let body = 'height=' + (document.getElementById('tH').value || 0) +
             '&gap=' + (document.getElementById('tG').value || 0) + '&count=' + tanks.length +
             '&sensor=' + sensorTank;
  tanks.forEach((t, i) => {
    body += `&shape${i}=${t.shape}&length${i}=${t.length||0}&width${i}=${t.width||0}&diameter${i}=${t.diameter||0}`;
  });
  post('/tank', body).then(msg => { alert(msg); updateLitres(); loadValves(); });
}

// Litres display + live check of the setup
function updateLitres(){
  fetch('/data').then(r=>r.json()).then(d=>{
    const box = document.getElementById('litresBox');
    if (d.capacity > 0 && d.litres >= 0){
      box.style.display = '';
      document.getElementById('litres').textContent = d.litres;
      document.getElementById('capacity').textContent = d.capacity;
      document.getElementById('litresLabel').textContent =
        (d.measured > 1 ? `Water in ${d.measured} tanks` : 'Water in tank') +
        (d.measured < d.tanks ? ` (${d.tanks - d.measured} closed off, not counted)` : '');
    } else box.style.display = 'none';
    document.getElementById('tankSummary').textContent = d.valid
      ? `Check: sensor reads ${d.distance} cm, so the water is ${d.depth} cm deep` +
        (d.capacity > 0 ? ` (${d.litres} of ${d.capacity} L).` : '.')
      : '';
  });
}
// ---- History chart ----
let range = 'day', hist = null;
function setRange(r){
  range = r;
  document.getElementById('rDay').className = 'range' + (r=='day' ? ' on' : '');
  document.getElementById('rWeek').className = 'range' + (r=='week' ? ' on' : '');
  drawChart();
}
function loadHistory(){
  fetch('/history').then(r=>r.json()).then(d=>{ hist = d; drawChart(); });
}
function drawChart(){
  if (!hist) return;
  const W=560, H=210, L=38, R=10, T=10, B=26;
  const span = range=='day' ? 86400 : 7*86400;
  const step = range=='day' ? 300 : 3600;
  const pts = hist[range];
  const now = hist.now, t0 = now - span;
  const x = t => L + (t - t0) / span * (W - L - R);
  const y = p => T + (100 - p) / 100 * (H - T - B);
  let s = '';
  // grid + % labels
  for (let p = 0; p <= 100; p += 25){
    s += `<line x1='${L}' x2='${W-R}' y1='${y(p)}' y2='${y(p)}' style='stroke:var(--grid)'/>`;
    s += `<text x='${L-6}' y='${y(p)+4}' text-anchor='end' font-size='11' style='fill:var(--faint)'>${p}%</text>`;
  }
  // time labels: every 6 h (24 h view) or every day (7 day view)
  const tick = range=='day' ? 6*3600 : 86400;
  // Use the device's time zone (Time section); the browser's until it's loaded
  const tz = (tzMin != null ? tzMin : -new Date().getTimezoneOffset()) * 60;
  const off = -tz;                                    // align ticks to local midnight
  for (let t = Math.ceil((t0 - off) / tick) * tick + off; t <= now; t += tick){
    const d = new Date((t + tz) * 1000);              // read with getUTC... = device local time
    const lbl = range=='day' ? d.getUTCHours().toString().padStart(2,'0') + ':00'
                             : d.toLocaleDateString(undefined, {weekday:'short', timeZone:'UTC'});
    s += `<line x1='${x(t)}' x2='${x(t)}' y1='${T}' y2='${H-B}' style='stroke:var(--grid2)'/>`;
    s += `<text x='${x(t)}' y='${H-8}' text-anchor='middle' font-size='11' style='fill:var(--faint)'>${lbl}</text>`;
  }
  // data line, split where there are gaps (device was off)
  let seg = [], segs = [];
  pts.filter(p => p[0] >= t0).forEach((p, i, a) => {
    if (i && p[0] - a[i-1][0] > step * 2.5){ segs.push(seg); seg = []; }
    seg.push(`${x(p[0] + step/2).toFixed(1)},${y(p[1]).toFixed(1)}`);
  });
  segs.push(seg);
  segs.forEach(sg => {
    if (sg.length == 1) s += `<circle cx='${sg[0].split(',')[0]}' cy='${sg[0].split(',')[1]}' r='2.5' fill='#2196F3'/>`;
    else if (sg.length) s += `<polyline points='${sg.join(' ')}' fill='none' stroke='#2196F3' stroke-width='2' stroke-linejoin='round'/>`;
  });
  document.getElementById('chart').innerHTML = s;

  const n = pts.filter(p => p[0] >= t0).length;
  document.getElementById('chartNote').textContent =
    n ? '' : (hist.now < 1700000000 ? 'Waiting for the clock to sync (needs internet)...'
           : 'No data yet - the first point appears after ' + (range=='day' ? '5 minutes.' : '1 hour.'));
}

window.addEventListener('load', () => {
  loadTank(); loadHistory(); updateLitres();
  setInterval(updateLitres, 2000);
  setInterval(loadHistory, 60000);
});
</script>
)HTML";

    // ==============================
    // LED CONTROL (No Page Reload)
    // ==============================
    html += "<h2>LED Control</h2>";
    html += "<p>Control the RGB LED (currently " + String(ledState == HIGH ? "ON" : "OFF") + "):</p>";
    html += "<button class='on' onclick=\"led('on')\">Turn ON</button>";
    html += "<button class='off' onclick=\"led('off')\">Turn OFF</button>";

    // ==============================
    // SCREEN CONTROL (No Page Reload)
    // ==============================
    html += "<h2 style='margin-top: 30px;'>Screen Control</h2>";
    html += "<p>Control the OLED display (currently " + String(getScreenState() ? "ON" : "OFF") + "):</p>";
    html += "<button class='on' onclick=\"screen('on')\">Turn Screen ON</button>";
    html += "<button class='off' onclick=\"screen('off')\">Turn Screen OFF</button>";

    appendSettingsStart(html, false);
    appendTankSection(html);
    appendTimeSection(html);
    appendWifiSection(html);
    appendSettingsEnd(html);
  }

  // Info section
  html += "<div class='info'>";
  html += "<p><strong>Device Info:</strong></p>";
  if (isAPMode) {
    html += "<p>Mode: Access Point</p>";
    html += "<p>AP IP: " + WiFi.softAPIP().toString() + "</p>";
  } else {
    html += "<p>Mode: Station (Connected)</p>";
    html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
    html += "<p>SSID: " + WiFi.SSID() + "</p>";
    html += "<p>RSSI: " + String(WiFi.RSSI()) + " dBm</p>";
  }
  html += "<p><small>Hold the BOOT button on the board for 3 seconds to forget all saved Wi-Fi networks and the settings PIN</small></p>";
  html += "</div>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleData() {
  float litres = sensorDataValid ? waterLitres(currentDistance) : -1;
  String json = "{";
  json += "\"valid\":" + String(sensorDataValid ? "true" : "false") + ",";
  json += "\"distance\":" + String(currentDistance, 1) + ",";
  json += "\"depth\":" + String(sensorDataValid ? waterDepth(currentDistance) : -1, 1) + ",";
  json += "\"percent\":" + String(currentPercent, 1) + ",";
  json += "\"litres\":" + String(litres, 0) + ",";
  json += "\"capacity\":" + String(tankCapacity(), 0) + ",";
  json += "\"tanks\":" + String(getTankCount()) + ",";
  json += "\"measured\":" + String(getMeasuredCount());   // tanks counted now (valves)
  json += "}";
  server.send(200, "application/json", json);
}

// ---------------- API for other devices ----------------
// Stable endpoints meant for other boards / scripts (e.g. a big display).
// Keep these field names unchanged so existing clients keep working.
//
// GET /api/level ->
//   {"valid":true,"percent":45.2,"litres":720,"capacity":1592,
//    "depth":77.1,"distance":122.9,"age":1,"time":1795033980}
//   valid    = false if the sensor isn't answering (then the numbers are -1)
//   litres / capacity = -1 if no tank sizes are set up
//   age      = seconds since the last good reading
//   time     = device clock (unix seconds, UTC), 0 if not set
//   tanks    = tanks set up; measured = tanks counted now (a closed valve
//              leaves a tank out of litres/capacity/percent)
//
// GET /api/level.txt -> "45.2;720"   (percent;litres, -1 when not available)
//   Easy to read without a JSON library: sscanf(text, "%f;%d", &pct, &l)
void handleApiLevel() {
  bool ok = sensorDataValid;
  String json = "{\"valid\":" + String(ok ? "true" : "false");
  json += ",\"percent\":" + String(ok ? currentPercent : -1, 1);
  json += ",\"litres\":" + String(ok ? lroundf(waterLitres(currentDistance)) : -1L);
  json += ",\"capacity\":" + String(lroundf(tankCapacity()));
  json += ",\"depth\":" + String(ok ? waterDepth(currentDistance) : -1, 1);
  json += ",\"distance\":" + String(ok ? currentDistance : -1, 1);
  json += ",\"age\":" + String(lastReadingAt ? (long)((millis() - lastReadingAt) / 1000) : -1L);
  json += ",\"time\":" + String(isClockValid() ? (uint32_t)time(nullptr) : 0);
  json += ",\"tanks\":" + String(getTankCount());
  json += ",\"measured\":" + String(getMeasuredCount());
  json += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");   // allow other web pages to read it too
  server.send(200, "application/json", json);
}

void handleApiLevelText() {
  bool ok = sensorDataValid;
  String txt = String(ok ? currentPercent : -1, 1) + ";" +
               String(ok ? lroundf(waterLitres(currentDistance)) : -1L);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", txt);
}

// GET /tank -> {"height":..,"gap":..,"capacity":..,"sensor":0,
//   "tanks":[{"shape":1,"length":..,"width":..,"diameter":..,"open":true,"measured":true,"held":-1},...]}
// open = valve open; measured = counted in litres now; held = litres in a closed tank (-1 = unknown)
void handleGetTank() {
  String json = "{\"height\":" + String(getTankHeight(), 1);
  json += ",\"gap\":" + String(getSensorGap(), 1);
  json += ",\"capacity\":" + String(tankCapacity(), 0);
  json += ",\"sensor\":" + String(getSensorTank());
  json += ",\"tanks\":[";
  for (int i = 0; i < getTankCount(); i++) {
    const TankUnit &t = getTankUnit(i);
    if (i) json += ",";
    json += "{\"shape\":" + String(t.shape);
    json += ",\"length\":" + String(t.length, 1);
    json += ",\"width\":" + String(t.width, 1);
    json += ",\"diameter\":" + String(t.diameter, 1);
    json += ",\"open\":" + String(isValveOpen(i) ? "true" : "false");
    json += ",\"measured\":" + String(isTankMeasured(i) ? "true" : "false");
    json += ",\"held\":" + String(heldLitres(i), 0) + "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// POST /tank/valve  i (tank, 0 = tank 1), open (1/0)
void handleSetValve() {
  if (!requirePin()) return;
  float depthNow = sensorDataValid ? waterDepth(currentDistance) : -1;
  sendResult(setValve(argIndex(), server.arg("open") == "1", depthNow));
}

// POST /tank  height, gap, count, shape0, length0, width0, diameter0, shape1, ...
void handleSetTank() {
  if (!requirePin()) return;
  int count = server.arg("count").toInt();
  if (count < 0 || count > MAX_TANKS) {
    server.send(400, "text/plain; charset=utf-8", "Error: up to " + String(MAX_TANKS) + " tanks");
    return;
  }
  TankUnit units[MAX_TANKS];
  for (int i = 0; i < count; i++) {
    String n(i);
    units[i].shape    = server.arg("shape" + n).toInt();
    units[i].length   = server.arg("length" + n).toFloat();
    units[i].width    = server.arg("width" + n).toFloat();
    units[i].diameter = server.arg("diameter" + n).toFloat();
  }
  String err = setTankSetup(server.arg("height").toFloat(), server.arg("gap").toFloat(), units, count,
                            server.arg("sensor").toInt());
  if (err.length()) {
    server.send(400, "text/plain; charset=utf-8", "Error: " + err);
    return;
  }
  float cap = tankCapacity();
  server.send(200, "text/plain; charset=utf-8", cap > 0 ? "Saved! Total capacity: " + String(cap, 0) + " L"
                                         : String("Saved!"));
}

void handleScan() {
  // A scan now would disturb the connection manager's own scan or connect attempt
  if (wState == W_SCANNING || wState == W_CONNECTING) {
    server.send(200, "application/json", "{\"busy\":true,\"networks\":[]}");
    return;
  }
  Serial.println("Scanning networks...");
  int n = WiFi.scanNetworks();

  String json = "{\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i).length() == 0) continue;   // hidden network
    if (json.length() > 14) json += ",";
    json += "{";
    json += "\"ssid\":" + jsonStr(WiFi.SSID(i)) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
    json += "}";
  }
  json += "]}";
  WiFi.scanDelete();

  server.send(200, "application/json", json);
}

// GET /wifi -> {"mode":"STA","current":"Home","checkMins":10,"nets":["Home","Phone hotspot"]}
// (passwords are never sent back)
void handleGetWifi() {
  String json = "{\"mode\":\"" + String(isAPMode ? "AP" : isWiFiConnected() ? "STA" : "CONNECTING") + "\"";
  json += ",\"current\":" + jsonStr(isWiFiConnected() ? WiFi.SSID() : String());
  json += ",\"checkMins\":" + String(PRIORITY_CHECK_INTERVAL / 60000);
  json += ",\"nets\":[";
  for (int i = 0; i < getNetworkCount(); i++) {
    if (i) json += ",";
    json += jsonStr(getNetworkSsid(i));
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// Reads the "i" (list position) argument; -1 if missing
int argIndex() {
  return server.hasArg("i") ? server.arg("i").toInt() : -1;
}

void sendResult(const String &err) {
  if (err.length()) server.send(400, "text/plain; charset=utf-8", "Error: " + err);
  else server.send(200, "text/plain; charset=utf-8", "OK");
}

// Every request that changes a setting calls this first. If a PIN is set,
// the request must carry it as "pin"; otherwise this replies 403 and
// returns false.
bool requirePin() {
  String err = checkPin(server.arg("pin"));
  if (err.length() == 0) return true;
  server.send(403, "text/plain; charset=utf-8", "Error: " + err);
  return false;
}

// GET /lock -> {"pin":true}   (whether the settings are PIN protected)
void handleGetLock() {
  server.send(200, "application/json", String("{\"pin\":") + (hasPin() ? "true" : "false") + "}");
}

// POST /lock/check  pin  -> "OK" if it's right (used by the Unlock button)
void handleCheckLock() {
  if (requirePin()) sendResult("");
}

// POST /lock/set  pin (the current one, if set), new (4-8 digits, empty = remove)
void handleSetLock() {
  if (requirePin()) sendResult(setPin(server.arg("new")));
}

// POST /wifi/save  ssid, password   (adds the network, or changes its password)
void handleSaveWifi() {
  if (!requirePin()) return;
  String ssid = server.arg("ssid");
  String err = saveNetwork(ssid, server.arg("password"));
  if (err.length()) {
    sendResult(err);
    return;
  }
  if (isAPMode) {
    // In setup mode, try the new network right away
    requestedNetwork = findNetwork(ssid);
    server.send(200, "text/plain; charset=utf-8", "Saved! Trying to connect to " + ssid + " now. If it works, this setup "
                "network disappears: join " + ssid + " and open http://watertank.local");
  } else {
    server.send(200, "text/plain; charset=utf-8", "Saved " + ssid + " as #" + String(findNetwork(ssid) + 1) + " in the list.");
  }
}

// POST /wifi/delete  i
void handleDeleteWifi() {
  if (!requirePin()) return;
  sendResult(deleteNetwork(argIndex()));
}

// POST /wifi/move  i, dir (-1 = up, 1 = down)
void handleMoveWifi() {
  if (!requirePin()) return;
  sendResult(moveNetwork(argIndex(), server.arg("dir").toInt() < 0 ? -1 : 1));
}

// POST /wifi/use  i   (switch to this network now)
void handleUseWifi() {
  if (!requirePin()) return;
  int i = argIndex();
  if (i < 0 || i >= getNetworkCount()) {
    sendResult("No such network");
    return;
  }
  requestedNetwork = i;   // done by checkWiFiConnection(), after this reply has been sent
  server.send(200, "text/plain; charset=utf-8", "Switching to " + getNetworkSsid(i) + ". If it doesn't work, the device "
              "goes back to the best network in range.");
}

// GET /time -> {"now":1795032772,"valid":true,"offset":180,"summer":false,"tz":180,"local":"Wed 18 Nov 23:11"}
// offset = minutes from GMT without summer time, tz = total offset used for display
void handleGetTime() {
  String json = "{\"now\":" + String((uint32_t)time(nullptr));
  json += ",\"valid\":" + String(isClockValid() ? "true" : "false");
  json += ",\"offset\":" + String(getClockOffset());
  json += ",\"summer\":" + String(getClockSummerTime() ? "true" : "false");
  json += ",\"tz\":" + String(getClockTotalOffset());
  json += ",\"local\":" + jsonStr(getClockLocalText());
  json += "}";
  server.send(200, "application/json", json);
}

// POST /time  offset (minutes from GMT), summer (1/0)
void handleSetTime() {
  if (!requirePin()) return;
  if (!server.hasArg("offset")) {
    sendResult("Choose a time zone");
    return;
  }
  sendResult(setClockZone(server.arg("offset").toInt(), server.arg("summer") == "1"));
}

// POST /time/set  now (unix seconds, UTC) -- set the clock from the browser
void handleSetClock() {
  if (!requirePin()) return;
  uint32_t t = strtoul(server.arg("now").c_str(), nullptr, 10);
  if (t < 1700000000UL) {
    sendResult("Invalid time");
    return;
  }
  setClockManual(t);
  sendResult("");
}

void handleLed() {
  if (!server.hasArg("state")) {
    server.send(400, "application/json", "{\"error\":\"missing state\"}");
    return;
  }

  String state = server.arg("state");

  if (state == "on") {
    setLedAutoMode(false);        // manual control, not water-level auto mode
    ledOn(255, 255, 255);         // turn the LED on (white)
    ledState = HIGH;              // mark that LED is active
    server.send(200, "application/json", "{\"status\":\"LED ON\"}");
    Serial.println("LED turned ON manually");
  }
  else if (state == "off") {
    setLedAutoMode(false);  // stay out of auto mode
    ledOff();               // actually turn off the LED
    ledState = LOW;         // mark LED state as OFF
    server.send(200, "application/json", "{\"status\":\"LED OFF\"}");
    Serial.println("LED turned OFF manually");
  }
  else {
    server.send(400, "application/json", "{\"error\":\"invalid state\"}");
  }
}

void handleScreen() {
  if (!server.hasArg("state")) {
    server.send(400, "application/json", "{\"error\":\"missing state\"}");
    return;
  }

  String state = server.arg("state");

  if (state == "on") {
    setScreenState(true);
    server.send(200, "application/json", "{\"status\":\"SCREEN ON\"}");
  }
  else if (state == "off") {
    setScreenState(false);
    server.send(200, "application/json", "{\"status\":\"SCREEN OFF\"}");
  }
  else {
    server.send(400, "application/json", "{\"error\":\"invalid state\"}");
  }
}

void handleStatus() {
  String json = "{";
  json += "\"led\":\"" + String(ledState == HIGH ? "ON" : "OFF") + "\",";
  json += "\"ledMode\":\"" + String(ledAutoMode ? "AUTO" : "MANUAL") + "\",";
  json += "\"screen\":\"" + String(getScreenState() ? "ON" : "OFF") + "\",";
  json += "\"mode\":\"" + String(isAPMode ? "AP" : isWiFiConnected() ? "STA" : "CONNECTING") + "\",";
  if (isAPMode) {
    json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  } else {
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"ssid\":" + jsonStr(WiFi.SSID()) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  }
  json += "\"uptime\":" + String(millis() / 1000) + ",";

  // Board health: memory, settings storage, chip temperature
  nvs_stats_t nvs = {};
  nvs_get_stats(NULL, &nvs);
  json += "\"heapFree\":" + String(ESP.getFreeHeap()) + ",";         // RAM free now (bytes)
  json += "\"heapMinFree\":" + String(ESP.getMinFreeHeap()) + ",";   // lowest free RAM since start
  json += "\"heapMaxBlock\":" + String(ESP.getMaxAllocHeap()) + ",";  // biggest single free block
  json += "\"heapTotal\":" + String(ESP.getHeapSize()) + ",";
  json += "\"appSize\":" + String(ESP.getSketchSize()) + ",";          // firmware size
  json += "\"appSpace\":" + String(ESP.getFreeSketchSpace()) + ",";    // room per firmware slot
  json += "\"nvsUsed\":" + String(nvs.used_entries) + ",";            // settings storage, 32-byte entries
  json += "\"nvsTotal\":" + String(nvs.total_entries) + ",";
  json += "\"chipTemp\":" + String(temperatureRead(), 1) + ",";       // degrees C
  json += "\"resetReason\":" + String((int)esp_reset_reason());       // 1 power-on, 3 software, 4 crash, 6/7 watchdog, 9 brownout
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleNotFound() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<style>body { font-family: Arial; text-align: center; margin: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>404 - Not Found</h1>";
  html += "<p>The page you're looking for doesn't exist.</p>";
  html += "<a href='/'>Go to home page</a>";
  html += "</body></html>";
  
  server.send(404, "text/html; charset=utf-8", html);
}