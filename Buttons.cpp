#include "Buttons.h"
#include "wifi.h"

// Non-blocking LED blink state (used to provide quick visual feedback on save)
static int blinkStepsRemaining = 0; // number of toggles left (on/off counts)
static unsigned long blinkIntervalMs = 0;
static unsigned long blinkLastToggleMs = 0;
static bool blinkLedState = false;

void startBlink(int times, int ms)
{
  if (times <= 0 || ms == 0)
    return;
  // times = number of ON pulses; each pulse has ON then OFF => steps = times*2
  blinkStepsRemaining = times * 2;
  blinkIntervalMs = ms;
  blinkLastToggleMs = millis();
  blinkLedState = true;
  digitalWrite(LED_PIN, HIGH);
  blinkStepsRemaining--; // consumed the initial ON
}

void blinkUpdate()
{
  if (blinkStepsRemaining <= 0)
    return;
  unsigned long now = millis();
  if ((now - blinkLastToggleMs) >= blinkIntervalMs)
  {
    // toggle
    blinkLedState = !blinkLedState;
    digitalWrite(LED_PIN, blinkLedState ? HIGH : LOW);
    blinkLastToggleMs = now;
    blinkStepsRemaining--;
    if (blinkStepsRemaining == 0)
    {
      // ensure LED is off at the end
      digitalWrite(LED_PIN, LOW);
      blinkLedState = false;
    }
  }
}

void handleButtons()
{
  // Ignore buttons for 10s after boot for stability
  if (millis() - state.startupTime <= (10 * 1000))
  {
    return;
  }

  // Sample press states and events once so we don't consume EasyButton events twice.
  bool upIs = upButton.isPressed();
  bool downIs = downButton.isPressed();
  bool upWas = upButton.wasPressed();
  bool downWas = downButton.wasPressed();

  // Update per-button last-pressed timestamps and log simple presses
  if (upWas)
  {
    state.upLastPressedAt = millis();
    Serial.println("Button: UP pressed");
  }
  if (downWas)
  {
    state.downLastPressedAt = millis();
    Serial.println("Button: DOWN pressed");
  }

  bool bothPressed = upIs && downIs;
  bool bothShortPress = false;

  if (bothPressed && !state.lastBothPressed)
  {
    // both just pressed
    state.bothPressStart = millis();
    Serial.println("Buttons: BOTH pressed (MAIN)");
    // In NORMAL mode we want STOP to be instant on press; handle it now.
    if (state.currentMode == NORMAL)
    {
      int newTarget = getCurrentPosition();
      targetPosition.value.int_value = newTarget;
      homekit_characteristic_notify(&targetPosition, targetPosition.value);
      stepper.moveTo(stepper.currentPosition());
      positionState.value.int_value = POS_STOPPED;
      homekit_characteristic_notify(&positionState, positionState.value);
      state.mainShortHandledOnPress = true;
    }
    else
    {
      state.mainShortHandledOnPress = false;
    }
  }

  // long-press detection while both held
  if (bothPressed)
  {
    uint32_t dur = millis() - state.bothPressStart;
    if (dur >= 10000 && !state.mainLong10Handled)
    {
      state.mainLong10Handled = true;
      state.mainLong5Handled = true; // suppress 5s action
      reset();
      digitalWrite(LED_PIN, HIGH);
      wifiConnect();
      arduino_homekit_setup(&config);
      digitalWrite(LED_PIN, LOW);
    }
    else if (dur >= 5000 && !state.mainLong5Handled && state.currentMode != CALIBRATE)
    {
      state.mainLong5Handled = true;
      enableCalibrationMode();
    }
  }

  // detect release (short press) -> treat as MAIN short press on release for CAL only
  if (!bothPressed && state.lastBothPressed)
  {
    uint32_t dur = millis() - state.bothPressStart;
    if (dur < 5000)
    {
      if (state.currentMode == CALIBRATE)
      {
        // In CAL we still save on release to avoid accidental saves while holding for long-press
        bothShortPress = true;
      }
      else
      {
        // In NORMAL we've already handled short action on press; if for some reason it wasn't handled on press,
        // handle it now. Then clear the on-press handled flag.
        if (!state.mainShortHandledOnPress)
        {
          bothShortPress = true;
        }
        state.mainShortHandledOnPress = false;
      }
    }
    // re-arm one-shot guards on release
    state.mainLong5Handled = false;
    state.mainLong10Handled = false;
  }
  state.lastBothPressed = bothPressed;

  // detect near-simultaneous press using per-button timestamps and pairing window
  bool pairActive = false;
  if (bothPressed)
    pairActive = true;
  else
  {
    uint32_t now = millis();
    if (state.upLastPressedAt != 0 && downIs && (now - state.upLastPressedAt <= PAIRING_WINDOW_MS))
      pairActive = true;
    if (state.downLastPressedAt != 0 && upIs && (now - state.downLastPressedAt <= PAIRING_WINDOW_MS))
      pairActive = true;
  }

  if (state.currentMode == CALIBRATE)
  {
    // Capture points with MAIN short presses only; movement handled in loop()
    if (bothShortPress)
    {
      if (state.currentCalibrationStep == INIT)
      {
        // Record the raw step position at the top (user may have gone past the
        // logical zero). We will rebase positions after the bottom is saved so
        // that 0 == TOP and maxSteps == BOTTOM.
        state.upStep = stepper.currentPosition();
        state.currentCalibrationStep = UP_KNOWN;
        Serial.print("Calibration: saved TOP raw position = ");
        Serial.println(state.upStep);
        // quick LED feedback: 5 short blinks (non-blocking)
        startBlink(5, 80);
      }
      else if (state.currentCalibrationStep == UP_KNOWN)
      {
        state.downStep = stepper.currentPosition();
        Serial.print("Calibration: saved BOTTOM raw position = ");
        Serial.println(state.downStep);
        int travel = abs(state.currentStep - state.upStep);
        Serial.print("Calibration: measured travel = ");
        Serial.println(travel);
        if (travel < state.minTravel)
        {
          Serial.println("Calibration: travel too small, aborting save");
          return;
        }
        state.maxSteps = travel;
        // Rebase positions so TOP == 0 and BOTTOM == maxSteps. New current
        // position = old_currentStep - upStep.
        int rebasedCurrent = state.currentStep - state.upStep;
        stepper.setCurrentPosition(rebasedCurrent);
        state.currentStep = rebasedCurrent;
        targetPosition.value.int_value = 0;
        homekit_characteristic_notify(&targetPosition, targetPosition.value);
        currentPosition.value.int_value = 0;
        homekit_characteristic_notify(&currentPosition, currentPosition.value);
        state.currentMode = NORMAL;
        stepper.setMaxSpeed(SPEED_MAX);
        stepper.setAcceleration(ACCEL);
        saveConfig();
        Serial.println("Calibration: finished, rebased and saved");
        // quick LED feedback: 5 short blinks (non-blocking)
        startBlink(5, 80);
      }
    }
  }
  else
  {
    // Normal mode: if bothShortPress (MAIN) was detected, handle it first
    if (bothShortPress)
    {
      // Immediate stop at current position (MAIN == UP+DOWN short press)
      int newTarget = getCurrentPosition();
      Serial.print("MAIN short press: STOP at position %: ");
      Serial.println(newTarget);
      targetPosition.value.int_value = newTarget;
      homekit_characteristic_notify(&targetPosition, targetPosition.value);
      stepper.moveTo(stepper.currentPosition());
      positionState.value.int_value = POS_STOPPED;
      homekit_characteristic_notify(&positionState, positionState.value);
    }
    else if (pairActive)
    {
      // Suppress single-button presets when pair press detected to avoid conflicting commands
    }
    else
    {
      // quick presets
      if (upWas)
      {
        if (targetPosition.value.int_value != 100)
        {
          targetPosition.value.int_value = 100;
          homekit_characteristic_notify(&targetPosition, targetPosition.value);
        }
      }
      if (downWas)
      {
        if (targetPosition.value.int_value != 0)
        {
          targetPosition.value.int_value = 0;
          homekit_characteristic_notify(&targetPosition, targetPosition.value);
        }
      }
    }
  }
}
