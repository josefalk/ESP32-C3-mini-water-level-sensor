// ============================================
// user-tank.h
// ============================================
// Tank model: turns a sensor distance into water depth, litres and percent.
//
//   sensor  ─┬─                     sensorGap = sensor face -> full water line
//            │ gap
//   full ────┼──────────            tankHeight = full water line -> tank bottom
//            │ height               (inside height, same for all parallel tanks)
//   bottom ──┴──────────
//
// Several tanks can be connected in parallel (joined at the bottom, so the
// water is at the same level in all of them). They must have the same height;
// their shape and other sizes may differ. The sensor sits in one of them
// (the "sensor tank").
//
// Valves: each tank can be shut off from the others. A closed tank keeps its
// water, but the sensor can't see it any more, so it is left out of the
// litres, capacity and percent:
//   - valve of another tank closed  -> that tank is left out
//   - valve of the sensor tank closed -> only the sensor tank is measured
// When a valve is closed, the water level at that moment is remembered, so
// the page can show roughly how much water the closed tank holds.
#pragma once
#include <Arduino.h>

enum TankShape : uint8_t {
  SHAPE_RECT = 1,       // box: length x width
  SHAPE_CYLINDER = 2,   // round, standing upright: diameter
  SHAPE_HCYLINDER = 3,  // round, lying on its side: length (diameter = tank height)
};

struct TankUnit {
  uint8_t shape;
  float length, width, diameter;   // cm
};

const int MAX_TANKS = 4;

void initTank();

float getTankHeight();               // cm
float getSensorGap();                // cm
int getTankCount();
const TankUnit &getTankUnit(int i);

int getSensorTank();                 // which tank the sensor is in (0 = tank 1)

// Returns an error message, or "" on success (and saves to flash)
String setTankSetup(float height, float gap, const TankUnit *units, int count, int sensorTank);

// ---- Valves ----
bool isValveOpen(int i);
bool isTankMeasured(int i);          // true if tank i counts in litres/capacity now
int getMeasuredCount();              // how many tanks are counted now
float heldLitres(int i);             // closed tank: litres when it was closed, -1 if unknown
// Open or close the valve of tank i. depthNow = current water depth (cm),
// or -1 if unknown. Returns an error message, or "" on success.
String setValve(int i, bool open, float depthNow);

float waterDepth(float distance);    // cm of water above the bottom (0..height)
float waterLitres(float distance);   // litres in the measured tanks, -1 if no tanks set
float tankCapacity();                // litres in the measured tanks when full, -1 if no tanks set
float levelPercent(float distance);  // 0..100 (by volume when tanks are set, else by height)
