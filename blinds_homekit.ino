#include <Arduino.h>
#include <arduino_homekit_server.h>
#include "Helper.h"
#include "pins.h"
#include "wifi.h"
#include <EasyButton.h>
#include <AccelStepper.h>

#define LOG_D(fmt, ...)   printf_P(PSTR(fmt "\n") , ##__VA_ARGS__)

// 28BYJ-48 via ULN2003 using HALF4WIRE; coil order IN1, IN3, IN2, IN4
AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);
const int STEPS_PER_REV = 2048; // ~64:1 gearbox, ~0.18° per step

enum mode { NORMAL, CALIBRATE };
enum calibrationStep { NONE, INIT, UP_KNOWN };

mode currentMode = NORMAL;
calibrationStep currentCalibrationStep = NONE;

// EasyButton with internal pullups and inverted logic (pressed = LOW)
EasyButton mainButton(BUTTON_MAIN, 35, true, true);
EasyButton upButton(BUTTON_UP_PIN, 35, true, true);
EasyButton downButton(BUTTON_DOWN_PIN, 35, true, true);

Helper helper;

int maxSteps = 0, currentStep = 0, targetStep = 0;
uint32_t startupTime = 0;
uint32_t lastMovementTime = 0;

// HomeKit characteristics (provided by accessory.c)
extern "C" homekit_characteristic_t currentPosition;
extern "C" homekit_characteristic_t targetPosition;
extern "C" homekit_characteristic_t positionState;
extern "C" homekit_server_config_t config;

// HomeKit getters/setters
homekit_value_t currentPositionGet() { return currentPosition.value; }
homekit_value_t targetPositionGet()  { return targetPosition.value; }
homekit_value_t positionStateGet()   { return positionState.value; }
void currentPositionSet(homekit_value_t value) { currentPosition.value = value; }
void targetPositionSet(homekit_value_t value)  { targetPosition.value = value; }
void positionStateSet(homekit_value_t value)   { positionState.value = value; }

static uint32_t nextLedMillis = 0;
static uint32_t nextHeapMillis = 0;

int getCurrentPosition();
bool loadConfig();
bool saveConfig();
bool resetConfig();
void enableCalibrationMode();
void handleButtons();
void properLedDisplay();
void handleEngineControllerActivity();
void homekitSetup();
void homekitLoop();
void blindControl();
void move(bool down);

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_MAIN, INPUT_PULLUP);
  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  startupTime = millis();

  loadConfig();
  if (maxSteps == 0) {
    enableCalibrationMode();
  }

  // Stepper tuning for 28BYJ-48 on ULN2003
  stepper.setMaxSpeed(500);      // steps/s (start with 300–500)
  stepper.setAcceleration(100);  // steps/s^2 (start with 50–150)
  stepper.setCurrentPosition(currentStep);

  wifiConnect();
  homekitSetup();

  mainButton.begin();
  upButton.begin();
  downButton.begin();
}

void loop() {
  mainButton.read();
  upButton.read();
  downButton.read();

  handleButtons();
  properLedDisplay();
  handleEngineControllerActivity();

  // If there is a target to reach, consider the motor active
  if (stepper.distanceToGo() != 0) {
    lastMovementTime = millis();
  }

  // Non-blocking stepper control
  stepper.run();

  // Keep software step counter in sync with driver
  currentStep = stepper.currentPosition();

  homekitLoop();
  blindControl();

  // Minimal delay for smooth stepping and WDT safety
  delay(1);
}

void properLedDisplay() {
  if (currentMode == CALIBRATE) {
    const uint32_t t = millis();
    if (t > nextLedMillis) {
      nextLedMillis = t + 250;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    return;
  }
  digitalWrite(LED_PIN, LOW);
}

void reset() {
  WiFiManager wifiManager;
  helper.resetsettings(wifiManager);
  homekit_storage_reset();
  LOG_D("Stored settings removed");
}

// Turn motor power off after inactivity (kept for state housekeeping)
void handleEngineControllerActivity() {
  if (lastMovementTime != 0 && millis() - lastMovementTime > 100) {
    lastMovementTime = 0;
    saveConfig();
    if (maxSteps != 0) {
      currentPosition.value.int_value = getCurrentPosition();
      homekit_characteristic_notify(&currentPosition, currentPosition.value);
    }
  }
}

// 0% = bottom (closed), 100% = top (open)
int getCurrentPosition() {
  if (maxSteps <= 0) return 0;
  return 100 - (int)((((float)currentStep / (float)maxSteps) + 0.005f) * 100.0f);
}

bool loadConfig() {
  LOG_D("Loading config file");
  if (!helper.loadconfig()) return false;

  JsonVariant json = helper.getconfig();
  json.printTo(Serial);

  currentStep = json["currentStep"];
  maxSteps = json["maxSteps"];
  targetPosition.value.int_value = json["targetPositionValue"];
  currentPosition.value.int_value = getCurrentPosition();
  return true;
}

bool saveConfig() {
  LOG_D("Saving config");
  DynamicJsonBuffer jsonBuffer(500);
  JsonObject& json = jsonBuffer.createObject();
  json["currentStep"] = currentStep;
  json["maxSteps"] = maxSteps;
  json["targetPositionValue"] = targetPosition.value.int_value;
  return helper.saveconfig(json);
}

bool resetConfig() {
  DynamicJsonBuffer jsonBuffer(500);
  JsonObject& json = jsonBuffer.createObject();
  return helper.saveconfig(json);
}

int upStep = 0;

void enableCalibrationMode() {
  LOG_D("Calibrate mode");
  currentMode = CALIBRATE;
  currentCalibrationStep = INIT;
}

void handleButtons() {
  // Ignore buttons for 10s after boot for stability
  if (millis() - startupTime <= (10 * 1000)) {
    return;
  }

  // Factory reset: hold main 10s
  if (mainButton.pressedFor(10000)) {
    reset();
    digitalWrite(LED_PIN, HIGH);
    wifiConnect();
    homekitSetup();
    digitalWrite(LED_PIN, LOW);
  }

  // Enter calibration: hold main 5s
  if (mainButton.pressedFor(5000)) {
    enableCalibrationMode();
  }

  if (currentMode == CALIBRATE) {
    // Manual jogging during calibration
    if (upButton.isPressed()) {
      move(false); // up
    }
    if (downButton.isPressed()) {
      move(true);  // down
    }

    if (mainButton.wasPressed()) {
      if (currentCalibrationStep == INIT) {
        currentCalibrationStep = UP_KNOWN;
        upStep = currentStep;

        digitalWrite(LED_PIN, HIGH);
        delay(500);
        digitalWrite(LED_PIN, LOW);
      } else if (currentCalibrationStep == UP_KNOWN) {
        // Save travel length and exit calibration
        maxSteps = currentStep - upStep;
        currentStep = maxSteps;
        stepper.setCurrentPosition(currentStep);

        targetPosition.value.int_value = 0;
        homekit_characteristic_notify(&targetPosition, targetPosition.value);

        currentPosition.value.int_value = 0;
        homekit_characteristic_notify(&currentPosition, currentPosition.value);

        currentMode = NORMAL;
        saveConfig();

        LOG_D("Device calibrated, max steps: %d", maxSteps);
      }
    }
  } else {
    // Normal mode: quick presets
    if (upButton.isPressed()) {
      targetPosition.value.int_value = 100;
      homekit_characteristic_notify(&targetPosition, targetPosition.value);
    }
    if (downButton.isPressed()) {
      targetPosition.value.int_value = 0;
      homekit_characteristic_notify(&targetPosition, targetPosition.value);
    }
    if (mainButton.isPressed()) {
      targetPosition.value.int_value = currentPosition.value.int_value;
      homekit_characteristic_notify(&targetPosition, targetPosition.value);
    }
  }
}

void blindControl() {
  if (currentMode != NORMAL || maxSteps == 0) return;

  // Convert target percentage to steps
  targetStep = ((100 - (float)targetPosition.value.int_value) / 100.0f) * maxSteps;

  // Command stepper to the target (run() moves it)
  if (targetStep != stepper.targetPosition()) {
    stepper.moveTo(targetStep);
    lastMovementTime = millis();
  }
}

void homekitSetup() {
  currentPosition.setter = currentPositionSet;
  currentPosition.getter = currentPositionGet;

  targetPosition.setter = targetPositionSet;
  targetPosition.getter = targetPositionGet;

  positionState.setter = positionStateSet;
  positionState.getter  = positionStateGet;

  arduino_homekit_setup(&config);
}

void homekitLoop() {
  arduino_homekit_loop();

  const uint32_t t = millis();
  if (t > nextHeapMillis) {
    nextHeapMillis = t + 5000;
    LOG_D("Free heap: %d, HomeKit clients: %d",
          ESP.getFreeHeap(), arduino_homekit_connected_clients_count());
    LOG_D("Target position: %d", targetPosition.value.int_value);
  }
}

// Manual single step for calibration/buttons
void move(bool down) {
  // Down increases count, up decreases — same semantics as original code
  stepper.move(down ? +1 : -1);
  lastMovementTime = millis();
}
