# DS9 ESP32 Projects

A public monorepo of browser-flashable firmware for ESP32 DevKit V1, ESP32-C3-DevKitM-1, ESP32-S3-DevKitC-1 v1.0, and ESP32-C6-DevKitC-1 boards, plus the GitHub Pages installer that publishes them.

## Firmware catalog

### Practical

- **BLE Proximity Scanner** — BLE sightings, retained MQTT proximity state, and bounded local logs
- **Wi-Fi Surveyor** — nearby network, channel, security, and RSSI dashboard
- **Pocket RGB Lamp** — local browser control for the onboard RGB LED
- **Device Console** — live chip, heap, flash, uptime, and reset diagnostics
- **BLE Presence Beacon** — stable BLE advertisement for scanner and automation experiments
- **Pocket File Drop** — local LittleFS uploads and downloads without a router
- **BLE UART Console** — Nordic-UART-compatible BLE and USB serial bridge
- **Tiny Benchmark Lab** — bounded CPU and memory performance experiments
- **iBeacon Lab** — standards-shaped beacon payload for scanner testing
- **BME280 MQTT Sleep Sensor** — temperature, humidity, and pressure telemetry with captive setup and configurable deep sleep
- **Ultrasonic Parking Assistant** — filtered HC-SR04 distance with a local parking/tank gauge
- **PIR Occupancy Timer** — interrupt-driven motion events with a rollover-safe hold timer
- **NTP Desk Clock** — network-synchronized local time on a TM1637 four-digit display

### Fun

- **Decision Oracle** — offline coin flips, dice rolls, and questionable advice
- **Pomodoro Light** — self-running 25/5 focus timer on the onboard RGB LED
- **Morse Beacon** — repeats `HELLO WORLD` in light and over serial
- **BLE Alias Shuffler** — cycles through harmless ridiculous BLE device names
- **Pocket Chat Room** — temporary nearby WebSocket chat over the board's access point
- **Reboot Museum** — persistent NVS boot counter with unnecessary drama

The catalog contains 73 board-specific firmware targets across 19 projects. Sixteen projects support all four board profiles. Pocket RGB Lamp, Pomodoro Light, and Morse Beacon support the C3, S3 v1.0, and C6 profiles because the generic ESP32 DevKit has no addressable RGB LED. Four projects require external hardware. See [`docs/board-support.md`](docs/board-support.md) for the exact board matrix, [`firmware/hardware-lab/README.md`](firmware/hardware-lab/README.md) for the new hardware wiring, and [`docs/internet-inspiration.md`](docs/internet-inspiration.md) for research sources.

Every external-hardware project includes a board-specific, color-coded wiring diagram and a non-affiliate Amazon search list for the required sensor, display, breadboard, jumpers, and safety components. The diagram changes with the exact board selected in the portal.

BLE Proximity Scanner, BME280 MQTT Sleep Sensor, and NTP Desk Clock require first-boot network setup. Each creates a temporary password-protected setup network; its random password is printed only over USB serial. The portal identifies required fields before flashing. All other projects work from their local/offline defaults without first-boot settings.

The local-dashboard AP projects create an open local network and serve a UI at `http://192.168.4.1`; those experimental networks never request router or broker credentials. Do not enter or store sensitive information on them.

## Firmware portal

[`web/`](web) uses [ESP Web Tools](https://esphome.github.io/esp-web-tools/) and Web Serial. It is fully static: ESP Web Tools does not need a backend. GitHub Pages supplies HTTPS and serves the installer, catalog, manifests, and binaries.

Published portal: <https://trip1.github.io/esp32-projects/>

## Repository layout

```text
projects.json                     Build and public catalog source of truth
firmware/ble-mqtt-scanner/        BLE/MQTT scanner project
firmware/no-hardware-lab/         Fourteen hardware-free build environments
firmware/bme280-mqtt-sensor/      Four-board BME280 MQTT/deep-sleep sensor
firmware/hardware-lab/            HC-SR04, PIR, and TM1637/NTP projects
web/                              Static installer source
scripts/                          Manifest and site assembly tools
web/wiring/                       Generated board-specific SVG wiring diagrams
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
~/.venvs/platformio/bin/pio run -d firmware/ble-mqtt-scanner
~/.venvs/platformio/bin/pio run -d firmware/no-hardware-lab
~/.venvs/platformio/bin/pio run -d firmware/bme280-mqtt-sensor
python3 scripts/build_site.py --output _site
```

Each compatible project/board pair publishes:

- `firmware.factory.bin` — complete image for browser installation and recovery
- `firmware.bin` — application-only image for future OTA
- `manifest.json` — ESP Web Tools installation manifest
- `ota-manifest.json` — version, image size, and SHA-256 metadata

Never commit credentials, signing keys, or per-device tokens. Actual USB flashing, radio behavior, onboard LED behavior, and captive-portal behavior require a connected board for verification.
