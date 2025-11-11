#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <EasyButton.h>
#include <AccelStepper.h>
#include "pins.h"

// Enums for mode and calibration step
enum mode
{
  NORMAL,
  CALIBRATE
};
enum calibrationStep
{
  NONE,
  INIT,
  UP_KNOWN
};

// HomeKit PositionState enum per spec
enum
{
  POS_DECREASING = 0,
  POS_INCREASING = 1,
  POS_STOPPED = 2
};

// Pairing window for near-simultaneous presses
static const uint32_t PAIRING_WINDOW_MS = 300;

// Motion constants (defined in the main translation unit). Declare as
// `extern const` so they are read-only and shared across translation units.
extern const float SPEED_MAX;
extern const float ACCEL;

// Shared state for blinds controller
struct BlindsState
{
  mode currentMode;
  calibrationStep currentCalibrationStep;
  bool calRequireRelease;

  // Button / MAIN state
  bool lastBothPressed;
  uint32_t bothPressStart;
  bool mainLong5Handled;
  bool mainLong10Handled;
  bool mainShortHandledOnPress;
  uint32_t upLastPressedAt;
  uint32_t downLastPressedAt;

  // Calibration / position
  int minTravel;
  int upStep;
  int downStep;
  int maxSteps;
  int currentStep;
  int targetStep;

  // Timing
  uint32_t startupTime;
  uint32_t lastMovementTime;
};

extern BlindsState state;

#endif // GLOBALS_H
