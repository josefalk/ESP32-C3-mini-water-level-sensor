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
// their shape and other sizes may differ. The sensor sits in any one of them.
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

// Returns an error message, or "" on success (and saves to flash)
String setTankSetup(float height, float gap, const TankUnit *units, int count);

float waterDepth(float distance);    // cm of water above the bottom (0..height)
float waterLitres(float distance);   // total litres in all tanks, -1 if no tanks set
float tankCapacity();                // total litres when full, -1 if no tanks set
float levelPercent(float distance);  // 0..100 (by volume when tanks are set, else by height)
