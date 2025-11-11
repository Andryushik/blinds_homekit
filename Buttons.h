#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>
#include <EasyButton.h>
#include <AccelStepper.h>
#include <arduino_homekit_server.h>
#include "pins.h"

// Buttons interface — uses centralized Globals
#include "Globals.h"

// Objects still live in the main TU
extern EasyButton upButton;
extern EasyButton downButton;
extern AccelStepper stepper;

// HomeKit characteristics (from accessory.c)
extern "C" homekit_characteristic_t currentPosition;
extern "C" homekit_characteristic_t targetPosition;
extern "C" homekit_characteristic_t positionState;
extern "C" homekit_server_config_t config;

// Functions implemented elsewhere in the main TU
extern void reset();
extern void enableCalibrationMode();
extern bool saveConfig();

// central state instance
extern BlindsState state;

void handleButtons();
int getCurrentPosition();
void startBlink(int times, int ms);
void blinkUpdate();

#endif // BUTTONS_H
