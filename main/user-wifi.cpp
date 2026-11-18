// ============================================
// user-wifi.cpp
// ============================================
#include "user-wifi.h"
#include "user-led.h"
#include "user-screen.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "user-history.h"
#include "user-tank.h"

// Network name of the device: the web page is at http://watertank.local
// and OTA uploads go to watertank.local (see platformio.ini).
const char* HOSTNAME = "watertank";
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "watertank-ota"   // set in platformio.ini build_flags
#endif

// Access Point credentials
const char* ap_ssid = "ESP32-Config";
const char* ap_password = "";

// Default WiFi network to connect to on first boot (used only if no
// credentials have been saved yet via the AP setup page).
// They live in secrets.h, which is NOT uploaded to GitHub. To set them,
// copy secrets.example.h to secrets.h and fill in your network.
// Without secrets.h the board starts in setup (AP) mode on first boot.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWORD ""
#endif
const char* DEFAULT_SSID = DEFAULT_WIFI_SSID;
const char* DEFAULT_PASSWORD = DEFAULT_WIFI_PASSWORD;

// Create web server on port 80
WebServer server(80);
Preferences preferences;

// Variables
int ledState = LOW;
const int RESET_BUTTON_PIN = 0;
unsigned long buttonPressStart = 0;
bool buttonPressed = false;
bool isAPMode = true;

// Reconnect handling
String savedSsid = "";
String savedPassword = "";
unsigned long staDisconnectedSince = 0;
unsigned long lastStaReconnectAttempt = 0;
unsigned long lastAPRetryAttempt = 0;
const unsigned long STA_FALLBACK_TIMEOUT = 60000;    // give up reconnecting STA after 60s, fall back to AP
const unsigned long STA_RECONNECT_RETRY = 5000;      // retry WiFi.reconnect() every 5s while disconnected
const unsigned long AP_RETRY_INTERVAL = 30000;       // while in AP mode, retry saved WiFi every 30s
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
void connectToWiFi(const char* ssid, const char* password);
void startAPMode();
void handleRoot();
void handleScan();
void handleConnect();
void handleLedOn();
void handleLedOff();
void handleStatus();
void handleData();
void handleScreenOn();
void handleScreenOff();
void handleNotFound();
void handleGetTank();
void handleSetTank();

// ---------------- mDNS, OTA, clock ----------------
// Started once, the first time we join a Wi-Fi network.
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

  // Real time for the history graph (UTC; the browser shows local time)
  configTime(0, 0, "pool.ntp.org", "time.google.com");
}

void initWiFi() {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  
  preferences.begin("wifi", false);
  
  savedSsid = preferences.getString("ssid", "");
  savedPassword = preferences.getString("password", "");

  if (savedSsid.length() == 0) {
    // No credentials saved yet -- fall back to the built-in default network
    // instead of dropping straight into AP config mode.
    Serial.println("No saved WiFi credentials. Using default network...");
    savedSsid = DEFAULT_SSID;
    savedPassword = DEFAULT_PASSWORD;
  }

  Serial.println("Attempting to connect to WiFi...");
  connectToWiFi(savedSsid.c_str(), savedPassword.c_str());
  server.on("/led", handleLed);
  server.on("/screen", handleScreen);
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/status", handleStatus);
  server.on("/data", handleData);
  server.on("/tank", HTTP_GET, handleGetTank);
  server.on("/tank", HTTP_POST, handleSetTank);
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

void updateSensorData(float distance, float percent) {
  currentDistance = distance;
  currentPercent = percent;
  sensorDataValid = true;
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
      Serial.println("Clearing WiFi credentials and restarting in AP mode...");
      
      ledOn(255, 0, 0);
      delay(500);
      ledOff();
      delay(200);
      ledOn(255, 0, 0);
      delay(500);
      ledOff();
      
      preferences.clear();
      preferences.end();
      
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

void checkWiFiConnection() {
  if (!isAPMode) {
    // Connected mode: watch for drops and try to recover before giving up
    if (WiFi.status() != WL_CONNECTED) {
      if (staDisconnectedSince == 0) {
        staDisconnectedSince = millis();
        lastStaReconnectAttempt = millis();
        Serial.println("WiFi connection lost, attempting to reconnect...");
        WiFi.reconnect();
      } else if (millis() - staDisconnectedSince > STA_FALLBACK_TIMEOUT) {
        Serial.println("Reconnect attempts timed out, falling back to AP mode...");
        staDisconnectedSince = 0;
        startAPMode();
      } else if (millis() - lastStaReconnectAttempt > STA_RECONNECT_RETRY) {
        lastStaReconnectAttempt = millis();
        Serial.println("Still disconnected, retrying...");
        WiFi.reconnect();
      }
    } else {
      staDisconnectedSince = 0;
    }
    return;
  }

  // AP mode: periodically retry the saved network so a stuck device recovers on its own
  if (savedSsid.length() == 0) return;

  if (millis() - lastAPRetryAttempt < AP_RETRY_INTERVAL) return;
  lastAPRetryAttempt = millis();

  Serial.println("AP mode: retrying saved WiFi network...");
  connectToWiFi(savedSsid.c_str(), savedPassword.c_str());
}

void connectToWiFi(const char* ssid, const char* password) {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  // ESP32-C3 Super Mini boards have a poorly matched antenna: at full TX power
  // the signal is distorted and neither STA nor AP works. Lower power fixes it.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setSleep(false);   // disable modem sleep -- a common cause of random silent disconnects
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    ledOn(0, 0, 255);
    delay(100);
    ledOff();
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connected successfully!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    isAPMode = false;
    startNetworkServices();

    ledOn(0, 255, 0);
    delay(1000);
    ledOff();
  } else {
    Serial.println();
    Serial.printf("Failed to connect to WiFi (status code %d: 1=network not found, 4=wrong password/rejected, 6=disconnected)\n",
                  (int)WiFi.status());
    // Diagnose: is the network visible at all, and how strong is it?
    int n = WiFi.scanNetworks();
    bool found = false;
    Serial.printf("Networks the ESP can see (2.4 GHz only): %d\n", n);
    for (int i = 0; i < n; i++) {
      Serial.printf("  %-32s  signal %d dBm  channel %d\n",
                    WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
      if (WiFi.SSID(i) == ssid) found = true;
    }
    if (!found)
      Serial.printf(">> '%s' NOT visible: router is 5 GHz only, too far away, or name is different\n", ssid);
    else
      Serial.printf(">> '%s' IS visible, so the password is probably wrong\n", ssid);
    WiFi.scanDelete();
    Serial.println("Starting AP mode for configuration...");
    startAPMode();
  }
}

void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);   // see note in connectToWiFi()
  
  Serial.println("=============================");
  Serial.println("AP Mode Started");
  Serial.println("=============================");
  Serial.print("SSID: ");
  Serial.println(ap_ssid);
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Connect to this network and");
  Serial.println("go to http://192.168.4.1");
  Serial.println("=============================");
  
  isAPMode = true;
  
  ledOn(128, 0, 128);
  delay(500);
  ledOff();
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += "h1 { color: #333; text-align: center; }";
  html += "h2 { color: #666; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }";
  html += "button { padding: 12px 24px; margin: 5px; font-size: 16px; cursor: pointer; border: none; border-radius: 5px; color: white; }";
  html += ".scan { background-color: #2196F3; width: 100%; }";
  html += ".on { background-color: #4CAF50; }";
  html += ".off { background-color: #f44336; }";
  html += "button:hover { opacity: 0.8; }";
  html += ".info { margin-top: 20px; padding: 15px; background: #e3f2fd; border-radius: 5px; }";
  html += ".network-list { margin-top: 20px; }";
  html += ".network-item { padding: 10px; margin: 5px 0; background: #f0f0f0; border-radius: 5px; cursor: pointer; }";
  html += ".network-item:hover { background: #e0e0e0; }";
  html += "input { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }";
  html += ".connect-btn { background-color: #4CAF50; width: 100%; }";
  html += ".warning { background: #fff3cd; border-left: 4px solid #ffc107; padding: 10px; margin: 10px 0; }";
  html += ".tank-container { margin: 20px 0; padding: 20px; background: #f8f9fa; border-radius: 10px; }";
  html += ".tank { width: 100%; height: 30px; background: #e0e0e0; border: 2px solid #333; border-radius: 5px; position: relative; overflow: hidden; }";
  html += ".tank-fill { height: 100%; background: linear-gradient(90deg, #4CAF50, #8BC34A); transition: width 0.5s; }";
  html += ".tank-fill.low { background: linear-gradient(90deg, #f44336, #ff5722); }";
  html += ".tank-fill.medium { background: linear-gradient(90deg, #FF9800, #FFC107); }";
  html += ".sensor-data { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 15px; }";
  html += ".sensor-item { padding: 15px; background: white; border-radius: 5px; text-align: center; }";
  html += ".sensor-label { font-size: 12px; color: #666; }";
  html += ".sensor-value { font-size: 24px; font-weight: bold; color: #333; }";
  html += ".calibration-group { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin: 15px 0; }";
  html += ".calibration-input { display: flex; flex-direction: column; }";
  html += ".calibration-input label { font-size: 14px; color: #666; margin-bottom: 5px; }";
  html += ".save-calibration { background-color: #FF9800; width: 100%; }";
  html += ".range { background:#e0e0e0; color:#333; padding:8px 16px; } .range.on { background:#2196F3; color:white; }";
  html += "select { width:100%; font-size:16px; }";
  html += "</style>";
  html += "<script>";

  // -----------------------------
  // LED + SCREEN EXTRA JAVASCRIPT
  // -----------------------------
  html += "function led(state){ fetch('/led?state='+state).then(r=>r.text()).then(console.log); }";
  html += "function screen(state){ fetch('/screen?state='+state).then(r=>r.text()).then(console.log); }";

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

  // Network scanning
  html += "function scanNetworks() {";
  html += "  document.getElementById('networks').innerHTML = '<p>Scanning...</p>';";
  html += "  fetch('/scan').then(r => r.json()).then(data => {";
  html += "    let html = '';";
  html += "    data.networks.forEach(n => {";
  html += "      html += '<div class=\"network-item\" onclick=\"selectNetwork(\\''+n.ssid+'\\','+n.secure+')\">';";
  html += "      html += '<strong>'+n.ssid+'</strong> ('+n.rssi+' dBm) '+(n.secure ? ' (SECURE) ' : ' (OPEN) ');";
  html += "      html += '</div>';"; 
  html += "    });";
  html += "    document.getElementById('networks').innerHTML = html;"; 
  html += "  });";
  html += "}";
  html += "function selectNetwork(ssid, secure) {";
  html += "  document.getElementById('ssid').value = ssid;";
  html += "  if (!secure) document.getElementById('password').value = '';";
  html += "}";
  html += "function connectWiFi() {";
  html += "  const ssid = document.getElementById('ssid').value;";
  html += "  const password = document.getElementById('password').value;";
  html += "  if (!ssid) { alert('Please enter SSID'); return; }";
  html += "  fetch('/connect', {";
  html += "    method: 'POST',";
  html += "    headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
  html += "    body: 'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(password)";
  html += "  }).then(r => r.text()).then(data => { alert(data); });";
  html += "}";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h1>Water Monitor</h1>";

  if (isAPMode) {
    html += "<div class='warning'>⚠ <strong>Configuration Mode</strong><br>Device is in Access Point mode. Connect to WiFi to start monitoring.</div>";
    html += "<h2>WiFi Setup</h2>";
    html += "<p>Connect to your WiFi network:</p>";
    html += "<button class='scan' onclick='scanNetworks()'>📡 Scan Networks</button>";
    html += "<div id='networks' class='network-list'></div>";
    html += "<div style='margin-top: 20px;'>";
    html += "<input type='text' id='ssid' placeholder='WiFi SSID' />";
    html += "<input type='password' id='password' placeholder='WiFi Password' />";
    html += "<button class='connect-btn' onclick='connectWiFi()'>Connect</button>";
    html += "</div>";

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
    html += "<div class='sensor-value'><span id='litres'>--</span> L <small style='font-size:14px;color:#666'>of <span id='capacity'>--</span> L</small></div>";
    html += "</div>";
    html += "</div>";
    html += "</div>";

    // History graph (drawn by the script below)
    html += R"HTML(
<h2>History</h2>
<div>
  <button id='rDay' class='range on' onclick="setRange('day')">24 hours</button>
  <button id='rWeek' class='range' onclick="setRange('week')">7 days</button>
</div>
<div id='chartWrap' style='margin-top:10px'><svg id='chart' viewBox='0 0 560 210' style='width:100%;height:auto'></svg></div>
<p id='chartNote' style='color:#666;font-size:13px'></p>
)HTML";

    // Tank setup: height + sensor gap + one or more parallel tanks
    html += R"HTML(
<h2>Tank Setup</h2>
<p style='color:#666;font-size:14px'>All sizes are <b>inside</b> measurements in cm.</p>
<div class='calibration-group'>
  <div class='calibration-input'><label>Tank height (cm):</label>
    <input type='number' id='tH' step='0.1' min='0'>
    <small style='color:#888'>Bottom of the tank up to the full water line.</small></div>
  <div class='calibration-input'><label>Sensor gap (cm):</label>
    <input type='number' id='tG' step='0.1' min='0'>
    <small style='color:#888'>Sensor face down to the water when the tank is full.</small></div>
</div>
<div id='tankList'></div>
<button class='scan' id='addTankBtn' onclick='addTank()'>+ Add tank</button>
<p style='color:#666;font-size:13px'>Tanks connected at the bottom (in parallel) fill to the same level,
  so they must all be the same height. Their shape, length, width and diameter may differ.
  The sensor can sit in any one of them.</p>
<button class='save-calibration' onclick='saveTank()'>Save Tank Setup</button>
<p id='tankSummary' style='color:#333;font-size:14px'></p>

<script>
const SHAPES = {1:'Box (rectangular)', 2:'Round, standing upright', 3:'Round, lying on its side'};
const MAX_TANKS = 4;
let tanks = [];

function field(i, key, label){
  return `<div class='calibration-input'><label>${label} (cm):</label>
    <input type='number' step='0.1' min='0' value='${tanks[i][key] || ''}'
      oninput='tanks[${i}].${key} = parseFloat(this.value) || 0'></div>`;
}
function renderTanks(){
  let h = '';
  tanks.forEach((t, i) => {
    h += `<div style='border:1px solid #ddd;border-radius:8px;padding:10px;margin:10px 0'>
      <div style='display:flex;justify-content:space-between;align-items:center'>
        <b>Tank ${i+1}</b>
        <button style='background:#f44336;padding:4px 10px;font-size:13px' onclick='removeTank(${i})'>Remove</button>
      </div>
      <select onchange='tanks[${i}].shape = +this.value; renderTanks()'
        style='padding:10px;margin:10px 0;border:1px solid #ddd;border-radius:5px'>
        ${Object.entries(SHAPES).map(([v, n]) => `<option value='${v}' ${t.shape == v ? 'selected' : ''}>${n}</option>`).join('')}
      </select>
      <div class='calibration-group' style='margin:0'>`;
    if (t.shape == 1) h += field(i, 'length', 'Length') + field(i, 'width', 'Width');
    if (t.shape == 2) h += field(i, 'diameter', 'Diameter');
    if (t.shape == 3) h += field(i, 'length', 'Length') +
      `<div class='calibration-input'><label>Diameter:</label><small style='color:#888;padding-top:12px'>
        = tank height</small></div>`;
    h += `</div></div>`;
  });
  document.getElementById('tankList').innerHTML = h;
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
  renderTanks();
}
function loadTank(){
  fetch('/tank').then(r=>r.json()).then(d=>{
    document.getElementById('tH').value = d.height;
    document.getElementById('tG').value = d.gap;
    tanks = d.tanks;
    renderTanks();
  });
}
function saveTank(){
  let body = 'height=' + (document.getElementById('tH').value || 0) +
             '&gap=' + (document.getElementById('tG').value || 0) + '&count=' + tanks.length;
  tanks.forEach((t, i) => {
    body += `&shape${i}=${t.shape}&length${i}=${t.length||0}&width${i}=${t.width||0}&diameter${i}=${t.diameter||0}`;
  });
  fetch('/tank', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body})
    .then(r=>r.text()).then(msg => { alert(msg); updateLitres(); });
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
        d.tanks > 1 ? `Water in ${d.tanks} tanks` : 'Water in tank';
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
    s += `<line x1='${L}' x2='${W-R}' y1='${y(p)}' y2='${y(p)}' stroke='#e5e5e5'/>`;
    s += `<text x='${L-6}' y='${y(p)+4}' text-anchor='end' font-size='11' fill='#888'>${p}%</text>`;
  }
  // time labels: every 6 h (24 h view) or every day (7 day view)
  const tick = range=='day' ? 6*3600 : 86400;
  const off = new Date().getTimezoneOffset() * 60;   // align ticks to local midnight
  for (let t = Math.ceil((t0 - off) / tick) * tick + off; t <= now; t += tick){
    const d = new Date(t * 1000);
    const lbl = range=='day' ? d.getHours().toString().padStart(2,'0') + ':00'
                             : d.toLocaleDateString(undefined, {weekday:'short'});
    s += `<line x1='${x(t)}' x2='${x(t)}' y1='${T}' y2='${H-B}' stroke='#f0f0f0'/>`;
    s += `<text x='${x(t)}' y='${H-8}' text-anchor='middle' font-size='11' fill='#888'>${lbl}</text>`;
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
  html += "<p><small>Hold GPIO 0 button for 3 seconds to reset WiFi settings</small></p>";
  html += "</div>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
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
  json += "\"tanks\":" + String(getTankCount());
  json += "}";
  server.send(200, "application/json", json);
}

// GET /tank -> {"height":..,"gap":..,"capacity":..,"tanks":[{"shape":1,"length":..,"width":..,"diameter":..},...]}
void handleGetTank() {
  String json = "{\"height\":" + String(getTankHeight(), 1);
  json += ",\"gap\":" + String(getSensorGap(), 1);
  json += ",\"capacity\":" + String(tankCapacity(), 0);
  json += ",\"tanks\":[";
  for (int i = 0; i < getTankCount(); i++) {
    const TankUnit &t = getTankUnit(i);
    if (i) json += ",";
    json += "{\"shape\":" + String(t.shape);
    json += ",\"length\":" + String(t.length, 1);
    json += ",\"width\":" + String(t.width, 1);
    json += ",\"diameter\":" + String(t.diameter, 1) + "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// POST /tank  height, gap, count, shape0, length0, width0, diameter0, shape1, ...
void handleSetTank() {
  int count = server.arg("count").toInt();
  if (count < 0 || count > MAX_TANKS) {
    server.send(400, "text/plain", "Error: up to " + String(MAX_TANKS) + " tanks");
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
  String err = setTankSetup(server.arg("height").toFloat(), server.arg("gap").toFloat(), units, count);
  if (err.length()) {
    server.send(400, "text/plain", "Error: " + err);
    return;
  }
  float cap = tankCapacity();
  server.send(200, "text/plain", cap > 0 ? "Saved! Total capacity: " + String(cap, 0) + " L"
                                         : String("Saved!"));
}

void handleScan() {
  Serial.println("Scanning networks...");
  int n = WiFi.scanNetworks();
  
  String json = "{\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
    json += "}";
  }
  json += "]}";
  
  server.send(200, "application/json", json);
}

void handleConnect() {
  if (server.hasArg("ssid")) {
    String ssid = server.arg("ssid");
    String password = server.hasArg("password") ? server.arg("password") : "";
    
    Serial.println("Saving WiFi credentials...");
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    
    server.send(200, "text/plain", "Credentials saved! Device will restart and connect to: " + ssid);
    
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing SSID");
  }
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



void handleLedOn() {
  ledAutoMode = true;
  ledState = HIGH;
  Serial.println("LED set to AUTO mode (ON)");
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial; text-align: center; margin: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>LED AUTO Mode Enabled</h1>";
  html += "<p>LED will now respond to water level</p>";
  html += "<p>Redirecting back...</p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleLedOff() {
  ledAutoMode = false;
  ledState = LOW;
  ledOff();
  Serial.println("LED set to MANUAL mode (OFF)");
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial; text-align: center; margin: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>LED Turned OFF</h1>";
  html += "<p>LED will stay off until turned back on</p>";
  html += "<p>Redirecting back...</p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
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



void handleScreenOn() {
  setScreenState(true);
  Serial.println("Screen turned ON via web");
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial; text-align: center; margin: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Screen Turned ON</h1>";
  html += "<p>OLED display is now active</p>";
  html += "<p>Redirecting back...</p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleScreenOff() {
  setScreenState(false);
  Serial.println("Screen turned OFF via web");
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial; text-align: center; margin: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Screen Turned OFF</h1>";
  html += "<p>OLED display is now inactive</p>";
  html += "<p>Redirecting back...</p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleStatus() {
  String json = "{";
  json += "\"led\":\"" + String(ledState == HIGH ? "ON" : "OFF") + "\",";
  json += "\"ledMode\":\"" + String(ledAutoMode ? "AUTO" : "MANUAL") + "\",";
  json += "\"screen\":\"" + String(getScreenState() ? "ON" : "OFF") + "\",";
  json += "\"mode\":\"" + String(isAPMode ? "AP" : "STA") + "\",";
  if (isAPMode) {
    json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  } else {
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  }
  json += "\"uptime\":" + String(millis() / 1000);
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
  
  server.send(404, "text/html", html);
}