# ESP HomeKit Roller Blinds (ESP8266, 28BYJ‑48) for IKEA FRIDANS

DIY smart roller blinds for IKEA FRIDANS based on ESP8266 with Apple Home (HomeKit) control, physical buttons, and calibration mode. State and settings are stored in SPIFFS; Wi‑Fi is configured via the WiFiManager portal.

## Features

- Control from Apple Home (HomeKit), including percentage positioning.
- Calibration of top/bottom endpoints for precise positioning.
  -- Two physical buttons: UP / DOWN. MAIN (stop/service) is synthesized by pressing UP+DOWN together.
  -- LED indicator (optional; on‑board): blinks in calibration; is steady (LOW) in normal mode.
- Persists settings and current state in SPIFFS (/config.json).
- Wi‑Fi auto‑setup via captive portal (access point name: “Roller Blind Configuration”).

## Hardware

- Controller: ESP8266 NodeMCU v3.
- Motor: 28BYJ‑48 stepper.
- Driver: ULN2003 (companion board for 28BYJ‑48).
  -- Buttons: 2 tactile buttons (UP, DOWN) with pull‑ups. MAIN actions are performed with UP+DOWN together.
- Power supply: 5 V, recommended 2 A for stability (headroom for peaks).
- Mounting and printed parts: 3D‑printed brackets/gears for the blind tube (ABS), M3 heat‑set inserts, and M3 bolts.

## Pins (ESP8266)

- LED: D4 (GPIO2), on‑board LED, active‑low (ON when LOW).
- Buttons (INPUT_PULLUP; pressed = LOW): UP — D2 (GPIO4), DOWN — D3 (GPIO0).
  Note: there is no dedicated MAIN pin; MAIN actions are performed by pressing UP+DOWN together. Do not hold DOWN (GPIO0) while powering up — that can put the board into flash mode.
- 28BYJ‑48 + ULN2003 (AccelStepper HALF4WIRE): IN1 — D5 (GPIO14), IN2 — D6 (GPIO12), IN3 — D7 (GPIO13), IN4 — D1 (GPIO5).

## Software and build

- Arduino IDE or PlatformIO.
- Libraries: WiFiManager, ArduinoJson (v5 API), AccelStepper, EasyButton, HomeKit implementation for ESP (non‑commercial use), FS/SPIFFS.
- Enable SPIFFS; on first flash, format the filesystem if needed.
- Upload and monitor at 115200 baud.

## First setup and HomeKit

- On first boot, the device exposes an AP: “Roller Blind Configuration”. Connect and set your Wi‑Fi.
- Add the accessory in Apple Home, PIN code is 281-42-814 (defined in accessory.c).
- Control position (percent), use scenes and automations as usual.

## Calibration

Set the top and bottom endpoints to control position in percent.

- If not yet calibrated, the device enters calibration mode automatically (LED blinks).
- To force calibration, press and hold BOTH buttons (UP+DOWN) for ~5 seconds.
- While in calibration, press and hold UP or DOWN to jog the motor — motion occurs only while the button is held (release stops).
- Use a short BOTH press (UP+DOWN) to act as MAIN and save the current position: first save TOP, then move to BOTTOM and save to finish calibration.
- On saving TOP or BOTTOM the on‑board LED gives a quick visual confirmation (5 short blinks) and the firmware logs the raw positions over Serial.
- Calibration uses a gentler profile (60% of normal speed/acceleration) for finer control.
- The computed `maxSteps` (and raw calibration points) are stored in `/config.json` and normal mode resumes.

### Calibration tips

- LED feedback: while in calibration the on‑board LED blinks; in normal mode it is steady (active‑LOW).
- Fine control: during calibration the motor runs at 60% of normal speed/acceleration for finer control.
- Button release stops motion immediately — wait for it to stop before pressing BOTH to record a position.
- Save order: first save TOP (short BOTH), then move to BOTTOM and save (short BOTH) to finish calibration.
- Invalid travel: if top and bottom are the same (below `minTravel`) calibration isn’t saved — adjust and try again.

## Manual control

- UP — open (move toward top).
- DOWN — close (move toward bottom).
- MAIN (stop/service) — press both UP+DOWN together briefly to stop or trigger MAIN short actions.

## Configuration (SPIFFS: /config.json)

- Stores `maxSteps`, `currentStep`, `targetPositionValue` (%), and calibration metadata (`rawUpStep`, `rawDownStep`, `minTravel`). It is managed automatically by the firmware.

## Reset

- Factory reset / clear pairings: press and hold BOTH buttons (UP+DOWN) for ~10 seconds. The device will clear stored settings and HomeKit pairing and return to the Wi‑Fi setup AP on next boot.

Note: long‑press actions are performed with the UP+DOWN combination, since there is no dedicated MAIN pin.

## Inspiration and related materials

- <https://www.instructables.com/Motorized-WiFi-IKEA-Roller-Blind/>
- <https://github.com/arturkuma/ESP8266-Homekit-Roller-Blinds/tree/master?tab=readme-ov-file>

## License

- Free non‑commercial use (as‑is).
- HomeKit implementation is subject to Apple’s terms for non‑commercial use.
