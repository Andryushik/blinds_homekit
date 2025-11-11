#include <Arduino.h>
#include <math.h>
#include <arduino_homekit_server.h>
#include "Helper.h"
#include "pins.h"
#include "wifi.h"
#include <EasyButton.h>
#include <AccelStepper.h>
#include "Buttons.h"
#include "Globals.h"

//  Speed settings (read-only shared constants)
const float SPEED_MAX = 3000.0f; // steps/s
const float ACCEL = 500.0f;      // steps/s^2
const float CAL_SPEED = 800.0f;  // steps/s during calibration (continuous)

// 28BYJ-48 via ULN2003 using HALF4WIRE; coil order IN1, IN3, IN2, IN4
AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);

// EasyButton with internal pullups and inverted logic (pressed = LOW)
// MAIN is synthesized as simultaneous press of UP+DOWN
EasyButton upButton(BUTTON_UP_PIN, 35, true, true);
EasyButton downButton(BUTTON_DOWN_PIN, 35, true, true);

Helper helper;

// Centralized runtime state
BlindsState state = {NORMAL, NONE, false, false, 0, false, false, false, 0, 0, 200, 0, 0, 0, 0, 0, 0};

// HomeKit characteristics (provided by accessory.c)
extern "C" homekit_characteristic_t currentPosition;
extern "C" homekit_characteristic_t targetPosition;
extern "C" homekit_characteristic_t positionState;
extern "C" homekit_server_config_t config;

// HomeKit getters/setters
homekit_value_t currentPositionGet() { return currentPosition.value; }
homekit_value_t targetPositionGet() { return targetPosition.value; }
homekit_value_t positionStateGet() { return positionState.value; }
void currentPositionSet(homekit_value_t value) { currentPosition.value = value; }
void targetPositionSet(homekit_value_t value) { targetPosition.value = value; }
void positionStateSet(homekit_value_t value) { positionState.value = value; }

static uint32_t nextLedMillis = 0;

int getCurrentPosition();
bool loadConfig();
bool saveConfig();
void enableCalibrationMode();
void properLedDisplay();
void handleEngineControllerActivity();
void homekitSetup();
void homekitLoop();
void blindControl();

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  // BUTTON_MAIN (D0) not used; MAIN is simultaneous UP+DOWN
  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  // Allow debug output to go through even during WiFi
  // Keep system debug output off to reduce serial noise
  Serial.setDebugOutput(false);
  state.startupTime = millis();

  loadConfig();
  if (state.maxSteps == 0)
  {
    enableCalibrationMode();
  }

  // Initialize stepper with normal motion profile.
  stepper.setMaxSpeed(SPEED_MAX);
  stepper.setAcceleration(ACCEL);
  stepper.setCurrentPosition(state.currentStep);

  wifiConnect();
  homekitSetup();
  // initialize buttons
  upButton.begin();
  downButton.begin();
}

void loop()
{
  static bool wasCalibrating = false;
  // mainButton removed; read UP/DOWN only
  upButton.read();
  downButton.read();
  // Heartbeat disabled to reduce log noise

  handleButtons();
  properLedDisplay();
  handleEngineControllerActivity();
  // Calibration: continuous runSpeed() for smooth/faster motion.
  if (state.currentMode == CALIBRATE)
  {
    wasCalibrating = true;

    bool up = upButton.isPressed();
    bool down = downButton.isPressed();
    // Wait for initial release so accidental held buttons don't start motion
    if (state.calRequireRelease)
    {
      if (!up && !down)
      {
        state.calRequireRelease = false;
      }
    }

    if (!state.calRequireRelease)
    {
      // Continuous speed control: setSpeed() + runSpeed() must be called frequently
      if (up && !down)
      {
        // negative speed moves toward top (smaller step numbers)
        stepper.setSpeed(-CAL_SPEED);
        stepper.runSpeed();
        state.lastMovementTime = millis();
      }
      else if (down && !up)
      {
        stepper.setSpeed(CAL_SPEED);
        stepper.runSpeed();
        state.lastMovementTime = millis();
      }
      else
      {
        // no buttons pressed during calibration: explicitly clear speed to avoid
        // leaving a stale speed value. We don't call runSpeed() so no stepping.
        stepper.setSpeed(0.0f);
      }
    }
  }
  else
  {
    // If we just exited calibration, restore normal motion profile
    if (wasCalibrating)
    {
      stepper.setAcceleration(ACCEL);
      stepper.setMaxSpeed(SPEED_MAX);
      wasCalibrating = false;
    }

    if (stepper.distanceToGo() != 0)
    {
      state.lastMovementTime = millis();
    }
    stepper.run();
  }

  // Keep software step counter in sync with driver
  state.currentStep = stepper.currentPosition();

  homekitLoop();
  blindControl();

  // Minimal delay for smooth stepping and WDT safety
  delay(1);
}

void properLedDisplay()
{
  blinkUpdate();
  if (state.currentMode == CALIBRATE)
  {
    const uint32_t t = millis();
    if (t > nextLedMillis)
    {
      nextLedMillis = t + 250;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    return;
  }
  digitalWrite(LED_PIN, LOW);
}

void reset()
{
  WiFiManager wifiManager;
  helper.resetsettings(wifiManager);
  homekit_storage_reset();
}

// Turn motor power off after inactivity (kept for state housekeeping)
void handleEngineControllerActivity()
{
  if (state.lastMovementTime != 0 && millis() - state.lastMovementTime > 100)
  {
    state.lastMovementTime = 0;
    // Avoid saving config while in CALIBRATE; saves during calibration were
    // noisy and not useful. Persist only when in NORMAL mode.
    if (state.currentMode != CALIBRATE)
    {
      saveConfig();
      if (state.maxSteps != 0)
      {
        currentPosition.value.int_value = getCurrentPosition();
        homekit_characteristic_notify(&currentPosition, currentPosition.value);
        if (positionState.value.int_value != POS_STOPPED)
        {
          positionState.value.int_value = POS_STOPPED;
          homekit_characteristic_notify(&positionState, positionState.value);
        }
      }
    }
  }
}

// 0% = bottom (closed), 100% = top (open)
int getCurrentPosition()
{
  if (state.maxSteps <= 0)
    return 0;
  // 100% = top (currentStep==0), 0% = bottom (currentStep==maxSteps)
  int pos = (int)roundf(100.0f - ((float)state.currentStep / (float)state.maxSteps) * 100.0f);
  if (pos < 0)
    pos = 0;
  if (pos > 100)
    pos = 100;
  return pos;
}

bool loadConfig()
{
  if (!helper.loadconfig())
    return false;

  JsonVariant json = helper.getconfig();
  state.currentStep = json["currentStep"];
  state.maxSteps = json["maxSteps"];
  targetPosition.value.int_value = json["targetPositionValue"];
  // Load raw calibration points if present
  JsonVariant v;
  v = json["rawUpStep"];
  if (v)
    state.upStep = (int)v;
  else
    state.upStep = 0;
  v = json["rawDownStep"];
  if (v)
    state.downStep = (int)v;
  else
    state.downStep = 0;
  // Load configurable minTravel if present
  v = json["minTravel"];
  if (v)
    state.minTravel = (int)v;
  currentPosition.value.int_value = getCurrentPosition();
  return true;
}

bool saveConfig()
{
  DynamicJsonBuffer jsonBuffer(500);
  JsonObject &json = jsonBuffer.createObject();
  json["currentStep"] = state.currentStep;
  json["maxSteps"] = state.maxSteps;
  json["targetPositionValue"] = targetPosition.value.int_value;
  // store raw calibration points if present
  json["rawUpStep"] = state.upStep;
  json["rawDownStep"] = state.downStep;
  // store configurable min travel
  json["minTravel"] = state.minTravel;
  return helper.saveconfig(json);
}

void enableCalibrationMode()
{
  state.currentMode = CALIBRATE;
  state.currentCalibrationStep = INIT;
  // require release of buttons that might have been held when entering CAL
  state.calRequireRelease = true;
  stepper.moveTo(stepper.currentPosition());
  stepper.run();
  state.lastMovementTime = 0;
  // During calibration we want a smooth, continuous action via runSpeed()
  // Disable acceleration so runSpeed() produces steady velocity.
  stepper.setAcceleration(0.0f);
  stepper.setMaxSpeed(CAL_SPEED);
  Serial.println("Entered CALIBRATE mode (continuous runSpeed)");
}

void blindControl()
{
  if (state.currentMode != NORMAL || state.maxSteps == 0)
    return;

  // Convert target percentage to steps
  state.targetStep = ((100 - (float)targetPosition.value.int_value) / 100.0f) * state.maxSteps;

  // Command stepper to the target (run() moves it)
  if (state.targetStep != stepper.targetPosition())
  {
    stepper.moveTo(state.targetStep);
    state.lastMovementTime = millis();

    // Update positionState based on direction
    long dist = stepper.targetPosition() - stepper.currentPosition();
    int newState;
    if (dist == 0)
      newState = POS_STOPPED;
    else if (dist > 0)
      newState = POS_DECREASING; // moving toward larger steps -> blinds going down (closing)
    else
      newState = POS_INCREASING; // moving toward smaller steps -> blinds going up (opening)

    if (positionState.value.int_value != newState)
    {
      positionState.value.int_value = newState;
      homekit_characteristic_notify(&positionState, positionState.value);
    }
  }

  // If no distance left and we previously reported moving, set STOPPED
  if (stepper.distanceToGo() == 0 && positionState.value.int_value != POS_STOPPED)
  {
    positionState.value.int_value = POS_STOPPED;
    homekit_characteristic_notify(&positionState, positionState.value);
  }
}

void homekitSetup()
{
  currentPosition.setter = currentPositionSet;
  currentPosition.getter = currentPositionGet;

  targetPosition.setter = targetPositionSet;
  targetPosition.getter = targetPositionGet;

  positionState.setter = positionStateSet;
  positionState.getter = positionStateGet;

  arduino_homekit_setup(&config);
}

void homekitLoop()
{
  arduino_homekit_loop();
}
