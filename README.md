# DS9 ESP32 Projects

A public monorepo of browser-flashable ESP32-C6 firmware projects and the GitHub Pages installer that publishes them.

## Firmware catalog

### Practical

- **BLE Proximity Scanner** — BLE sightings, retained MQTT proximity state, and bounded local logs
- **Wi-Fi Surveyor** — nearby network, channel, security, and RSSI dashboard
- **Pocket RGB Lamp** — local browser control for the onboard RGB LED
- **Device Console** — live chip, heap, flash, uptime, and reset diagnostics
- **BLE Presence Beacon** — stable BLE advertisement for scanner and automation experiments

### Fun

- **Decision Oracle** — offline coin flips, dice rolls, and questionable advice
- **Pomodoro Light** — self-running 25/5 focus timer on the onboard RGB LED
- **Morse Beacon** — repeats `HELLO WORLD` in light and over serial
- **BLE Alias Shuffler** — cycles through harmless ridiculous BLE device names

The eight projects in `firmware/no-hardware-lab` require only an ESP32-C6-DevKitC-1. AP-based projects create an open local network and serve a UI at `http://192.168.4.1`. LED projects expect the board's onboard RGB LED on GPIO 8.

## Firmware portal

[`web/`](web) uses [ESP Web Tools](https://esphome.github.io/esp-web-tools/) and Web Serial. It is fully static: ESP Web Tools does not need a backend. GitHub Pages supplies HTTPS and serves the installer, catalog, manifests, and binaries.

Published portal: <https://trip1.github.io/esp32-projects/>

## Repository layout

```text
projects.json                     Build and public catalog source of truth
firmware/ble-mqtt-scanner/        BLE/MQTT scanner project
firmware/no-hardware-lab/         Eight hardware-free build environments
web/                              Static installer source
scripts/                          Manifest and site assembly tools
tests/                            Portal packaging tests
.github/workflows/pages.yml       Firmware CI and Pages deployment
```

## Local verification

```bash
python3 -m unittest discover -s tests -v

g++ -std=c++17 \
  -I firmware/no-hardware-lab/include \
  firmware/no-hardware-lab/src/lab_logic.cpp \
  firmware/no-hardware-lab/test/native/test_main.cpp \
  -o /tmp/no-hardware-lab-tests
/tmp/no-hardware-lab-tests

~/.venvs/platformio/bin/pio test -d firmware/ble-mqtt-scanner -e native
~/.venvs/platformio/bin/pio run -d firmware/ble-mqtt-scanner -e esp32-c6-devkitc-1
~/.venvs/platformio/bin/pio run -d firmware/no-hardware-lab
python3 scripts/build_site.py --output _site
```

Each catalog project publishes:

- `firmware.factory.bin` — complete image for browser installation and recovery
- `firmware.bin` — application-only image for future OTA
- `manifest.json` — ESP Web Tools installation manifest
- `ota-manifest.json` — version, image size, and SHA-256 metadata

Never commit credentials, signing keys, or per-device tokens. Actual USB flashing, radio behavior, onboard LED behavior, and captive-portal behavior require a connected board for verification.
