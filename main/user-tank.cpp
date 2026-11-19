// ============================================
// user-tank.cpp
// ============================================
#include "user-tank.h"
#include <Preferences.h>

namespace {

Preferences tankPrefs;          // own namespace, so a Wi-Fi reset keeps the tank setup
float tankHeight = 170;
float sensorGap = 30;
TankUnit units[MAX_TANKS];
int unitCount = 0;
int sensorTank = 0;             // tank the sensor sits in
uint8_t closedMask = 0;         // bit i set = valve of tank i is closed
float heldDepth[MAX_TANKS];     // water depth when the valve was closed, -1 = unknown

bool measured(int i) {
  bool sensorClosed = closedMask & (1 << sensorTank);
  if (sensorClosed) return i == sensorTank;   // the sensor tank is cut off: only it is measured
  return !(closedMask & (1 << i));            // otherwise: every tank whose valve is open
}

// Litres in one tank when the water is h cm deep
float unitLitres(const TankUnit &t, float h) {
  if (h <= 0) return 0;
  float cm3 = 0;
  switch (t.shape) {
    case SHAPE_RECT:
      cm3 = t.length * t.width * h;
      break;
    case SHAPE_CYLINDER: {
      float r = t.diameter / 2;
      cm3 = PI * r * r * h;
      break;
    }
    case SHAPE_HCYLINDER: {
      // Lying on its side: its diameter is the tank height
      float r = tankHeight / 2;
      h = min(h, tankHeight);
      float c = constrain((r - h) / r, -1.0f, 1.0f);
      float seg = r * r * acosf(c) - (r - h) * sqrtf(max(0.0f, 2 * r * h - h * h));
      cm3 = seg * t.length;
      break;
    }
  }
  return cm3 / 1000.0;
}

float totalLitres(float h) {
  float sum = 0;
  for (int i = 0; i < unitCount; i++)
    if (measured(i)) sum += unitLitres(units[i], h);
  return sum;
}

void save() {
  tankPrefs.putFloat("height", tankHeight);
  tankPrefs.putFloat("gap", sensorGap);
  tankPrefs.putBytes("units", units, unitCount * sizeof(TankUnit));
  tankPrefs.putUChar("sensor", sensorTank);
}

void saveValves() {
  tankPrefs.putUChar("closed", closedMask);
  tankPrefs.putBytes("held", heldDepth, sizeof(heldDepth));
}

} // namespace

void initTank() {
  tankPrefs.begin("tank", false);
  for (int i = 0; i < MAX_TANKS; i++) heldDepth[i] = -1;

  if (tankPrefs.isKey("height")) {
    tankHeight = tankPrefs.getFloat("height", 170);
    sensorGap = tankPrefs.getFloat("gap", 30);
    size_t len = tankPrefs.getBytesLength("units");
    if (len % sizeof(TankUnit) == 0 && len <= sizeof(units)) {
      tankPrefs.getBytes("units", units, len);
      unitCount = len / sizeof(TankUnit);
    }
    // Sensor tank and valves (missing in older versions: then tank 1, all open)
    sensorTank = tankPrefs.getUChar("sensor", 0);
    if (sensorTank >= unitCount) sensorTank = 0;
    closedMask = tankPrefs.getUChar("closed", 0);
    if (tankPrefs.getBytesLength("held") == sizeof(heldDepth))
      tankPrefs.getBytes("held", heldDepth, sizeof(heldDepth));
  } else {
    // First boot with this version: take over the old full/empty calibration
    // and single-tank size that were stored in the "wifi" namespace.
    Preferences old;
    old.begin("wifi", true);
    float full = old.getFloat("fullDist", 30), empty = old.getFloat("emptyDist", 200);
    String shape = old.getString("shape", "none");
    TankUnit t = { 0, old.getFloat("tankL", 0), old.getFloat("tankW", 0), old.getFloat("tankD", 0) };
    old.end();

    sensorGap = full;
    tankHeight = empty - full;
    if (shape == "rect")      { t.shape = SHAPE_RECT;      units[0] = t; unitCount = 1; }
    if (shape == "cylinder")  { t.shape = SHAPE_CYLINDER;  units[0] = t; unitCount = 1; }
    if (shape == "hcylinder") { t.shape = SHAPE_HCYLINDER; units[0] = t; unitCount = 1; }
    save();
    Serial.println("Tank: migrated old calibration settings");
  }
  Serial.printf("Tank: height=%.1f cm, sensor gap=%.1f cm, %d tank(s), %d measured, capacity=%.0f L\n",
                tankHeight, sensorGap, unitCount, getMeasuredCount(), tankCapacity());
}

float getTankHeight()  { return tankHeight; }
float getSensorGap()   { return sensorGap; }
int getTankCount()     { return unitCount; }
int getSensorTank()    { return sensorTank; }
const TankUnit &getTankUnit(int i) { return units[i]; }

bool isValveOpen(int i)    { return !(closedMask & (1 << i)); }
bool isTankMeasured(int i) { return i >= 0 && i < unitCount && measured(i); }

int getMeasuredCount() {
  int n = 0;
  for (int i = 0; i < unitCount; i++) if (measured(i)) n++;
  return n;
}

float heldLitres(int i) {
  if (i < 0 || i >= unitCount || isValveOpen(i) || heldDepth[i] < 0) return -1;
  return unitLitres(units[i], heldDepth[i]);
}

String setValve(int i, bool open, float depthNow) {
  if (i < 0 || i >= unitCount) return "No such tank";
  if (open == isValveOpen(i)) return "";        // nothing changes: don't write flash
  if (open) {
    closedMask &= ~(1 << i);
  } else {
    closedMask |= (1 << i);
    heldDepth[i] = depthNow;                    // its water stays at this level
  }
  saveValves();
  Serial.printf("Tank %d valve %s, now %d tank(s) measured, capacity=%.0f L\n",
                i + 1, open ? "opened" : "closed", getMeasuredCount(), tankCapacity());
  return "";
}

String setTankSetup(float height, float gap, const TankUnit *in, int count, int sensorIn) {
  if (height <= 0)                 return "Tank height must be more than 0";
  if (gap < 0)                     return "Sensor gap can't be negative";
  if (count < 0 || count > MAX_TANKS) return "Too many tanks (max " + String(MAX_TANKS) + ")";
  if (count > 0 && (sensorIn < 0 || sensorIn >= count)) return "Choose which tank the sensor is in";

  for (int i = 0; i < count; i++) {
    const TankUnit &t = in[i];
    String name = "Tank " + String(i + 1) + ": ";
    switch (t.shape) {
      case SHAPE_RECT:
        if (t.length <= 0 || t.width <= 0) return name + "enter length and width";
        break;
      case SHAPE_CYLINDER:
        if (t.diameter <= 0) return name + "enter the diameter";
        break;
      case SHAPE_HCYLINDER:
        if (t.length <= 0) return name + "enter the length";
        break;
      default:
        return name + "choose a shape";
    }
  }

  // A different number of tanks means the tank numbers may have moved:
  // open all valves again so no wrong tank stays shut off.
  if (count != unitCount && closedMask) {
    closedMask = 0;
    saveValves();
  }
  tankHeight = height;
  sensorGap = gap;
  unitCount = count;
  sensorTank = count > 0 ? sensorIn : 0;
  for (int i = 0; i < count; i++) units[i] = in[i];
  save();
  Serial.printf("Tank saved: height=%.1f gap=%.1f, %d tank(s), capacity=%.0f L\n",
                height, gap, count, tankCapacity());
  return "";
}

float waterDepth(float distance) {
  return constrain(sensorGap + tankHeight - distance, 0, tankHeight);
}

float waterLitres(float distance) {
  return unitCount ? totalLitres(waterDepth(distance)) : -1;
}

float tankCapacity() {
  return unitCount ? totalLitres(tankHeight) : -1;
}

float levelPercent(float distance) {
  float cap = tankCapacity();
  if (cap > 0) return constrain(waterLitres(distance) / cap * 100, 0, 100);
  return waterDepth(distance) / tankHeight * 100;
}
