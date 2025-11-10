# ESP HomeKit Roller Blinds (ESP8266, 28BYJ‑48) for IKEA FRIDANS

DIY smart roller blinds for IKEA FRIDANS based on ESP8266 with Apple Home (HomeKit) control, physical buttons, and calibration mode. State and settings are stored in SPIFFS; Wi‑Fi is configured via the WiFiManager portal.

## Features

- Control from Apple Home (HomeKit), including percentage positioning.
- Calibration of top/bottom endpoints for precise positioning.
- Three physical buttons: UP / DOWN / MAIN (stop/service).
- LED indicator (optional; on‑board): blinks in calibration; stays on in normal mode.
- Persists settings and current state in SPIFFS (/config.json).
- Wi‑Fi auto‑setup via captive portal (access point name: “Roller Blind Configuration”).

## Hardware

- Controller: ESP8266 NodeMCU v3.
- Motor: 28BYJ‑48 stepper.
- Driver: ULN2003 (companion board for 28BYJ‑48).
- Buttons: 3 tactile buttons (UP, DOWN, MAIN) with pull‑ups.
- Power supply: 5 V, recommended 2 A for stability (headroom for peaks).
- Mounting and printed parts: 3D‑printed brackets/gears for the blind tube (ABS), M3 heat‑set inserts, and M3 bolts.

## Pins (ESP8266)

- LED: D0 (GPIO16).
- Buttons: UP — D2 (GPIO4), DOWN — D3 (GPIO0), MAIN — D3 (GPIO0).
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
- To force calibration, hold MAIN for ~5 seconds.
- Press/hold UP until the top position → short press MAIN to save top.
- Press/hold DOWN until the bottom position → short press MAIN to save bottom.
- The computed maxSteps is stored in /config.json and normal mode resumes.

## Manual control

- UP — open (move toward top).
- DOWN — close (move toward bottom).
- MAIN — stop/service action (used for calibration and reset).

## Configuration (SPIFFS: /config.json)

- Stores maxSteps, currentStep, targetPositionValue (%). It is managed automatically by the firmware.

## Reset

- Hold MAIN for ~10 seconds until indicated, then power‑cycle.
- This clears stored settings and the HomeKit pairing; the AP “Roller Blind Configuration” appears again.

## Inspiration and related materials

- <https://www.instructables.com/Motorized-WiFi-IKEA-Roller-Blind/>
- <https://github.com/arturkuma/ESP8266-Homekit-Roller-Blinds/tree/master?tab=readme-ov-file>

## License

- Free non‑commercial use (as‑is).
- HomeKit implementation is subject to Apple’s terms for non‑commercial use.
