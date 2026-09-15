# DS9 Microcontroller Projects

A public firmware monorepo for four ESP32 boards and four official Raspberry Pi Pico/Pico 2 variants, plus the GitHub Pages portal that publishes exact-board images.

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
- **NTP Desk Clock** — network-synchronized local time with selectable TM1637 or Inland/common LCD1602 I²C builds and bounded responder diagnostics
- **Pico Board Check** — RP2040/RP2350 USB diagnostics and onboard LED heartbeat
- **Pico W Wi-Fi Surveyor** — bounded boot-time wireless survey and local dashboard
- **ESP32 Diagnostic Console** — protected Wi-Fi setup and real-time chip, memory, network, I²C, and interface dashboard
- **MQTT Home Status Panel** — exact-topic trusted-LAN status display on an LCD1602
- **UniFi Network Panel** — sanitized WAN, latency, client, and AP metrics from a local bridge
- **Wi-Fi Weather Desk Station** — current Open-Meteo conditions for configured coordinates
- **LCD1602 Smart Dashboard** — combines clock, MQTT, ISS, and weather screens with web-configured order and duration

### Fun

- **Decision Oracle** — offline coin flips, dice rolls, and questionable advice
- **Pomodoro Light** — self-running 25/5 focus timer on the onboard RGB LED
- **Morse Beacon** — repeats `HELLO WORLD` in light and over serial
- **BLE Alias Shuffler** — cycles through harmless ridiculous BLE device names
- **Pocket Chat Room** — temporary nearby WebSocket chat over the board's access point
- **Reboot Museum** — persistent NVS boot counter with unnecessary drama
- **Pico Morse Beacon** — repeats `HELLO WORLD` on the Pico status LED and USB serial
- **Space and Satellite Tracker** — live ISS position and altitude on an LCD1602

The catalog contains 107 exact-board targets and 111 exact firmware build configurations across 28 projects. Existing ESP32 support is unchanged. Pico Board Check and Pico Morse Beacon support Pico, Pico W, Pico 2, and Pico 2 W; Pico W Wi-Fi Surveyor supports the two wireless boards. Nine projects require external hardware. See [`docs/board-support.md`](docs/board-support.md) for the exact matrix, [`firmware/pico-lab/README.md`](firmware/pico-lab/README.md) for Pico behavior and UF2 installation, and the hardware project READMEs for exact wiring.

Every external-hardware project includes a board-specific, color-coded wiring diagram and a non-affiliate Amazon search list for the required sensor, display, breadboard, jumpers, and safety components. The diagram changes with the exact board selected in the portal.

BLE Proximity Scanner, BME280 MQTT Sleep Sensor, NTP Desk Clock, ESP32 Diagnostic Console, the four single-purpose LCD panels, and LCD1602 Smart Dashboard require first-boot network setup. Each creates a temporary protected setup network with an 8-character, uppercase password printed only over USB serial. The alphabet omits `I`, `O`, `0`, and `1` to prevent transcription mistakes. The portal identifies required fields before flashing. All other projects work from their local/offline defaults without first-boot settings.

The ESP local-dashboard AP projects create an open local network and serve a UI at `http://192.168.4.1`; those experimental networks never request router or broker credentials. Pico W Wi-Fi Surveyor instead prints the same readable 8-character per-boot WPA2 password format over USB serial. Surveyed SSIDs are visible to every client that joins that protected AP.

## Firmware portal

[`web/`](web) is fully static. ESP targets use [ESP Web Tools](https://esphome.github.io/esp-web-tools/) and Web Serial. Pico targets provide hashed UF2 downloads with BOOTSEL drag-and-drop instructions; ESP Web Tools cannot install RP2040/RP2350 images.

Published portal: <https://trip1.github.io/esp32-projects/>

## Repository layout

```text
projects.json                     Build and public catalog source of truth
firmware/ble-mqtt-scanner/        BLE/MQTT scanner project
firmware/no-hardware-lab/         Fourteen hardware-free build environments
firmware/bme280-mqtt-sensor/      Four-board BME280 MQTT/deep-sleep sensor
firmware/hardware-lab/            HC-SR04, PIR, and TM1637/LCD1602 NTP projects
firmware/pico-lab/                RP2040/RP2350 and Pico W firmware
firmware/esp32-diagnostics/       Four-board real-time diagnostic console
firmware/lcd-panels/              Four LCD1602 network panels across four ESP32 boards
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
~/.venvs/platformio/bin/pio run -d firmware/pico-lab
~/.venvs/platformio/bin/pio run -d firmware/esp32-diagnostics
g++ -std=c++17 -Wall -Wextra -Werror -I firmware/common -I firmware/lcd-panels/include firmware/lcd-panels/src/panel_logic.cpp firmware/lcd-panels/test/native/test_main.cpp -o /tmp/lcd-panel-tests && /tmp/lcd-panel-tests
g++ -std=c++17 -Wall -Wextra -Werror -I firmware/lcd-panels/include firmware/lcd-panels/src/dashboard_logic.cpp firmware/lcd-panels/test/dashboard_native/test_main.cpp -o /tmp/dashboard-tests && /tmp/dashboard-tests
~/.venvs/platformio/bin/pio run -d firmware/lcd-panels
python3 scripts/build_site.py --output _site
```

Each ESP32 project/board pair publishes:

- `firmware.factory.bin` — complete image for browser installation and recovery
- `firmware.bin` — application-only image for future OTA
- `manifest.json` — ESP Web Tools installation manifest
- `ota-manifest.json` — version, image size, and SHA-256 metadata

Each Pico project/board pair publishes:

- `firmware.uf2` — exact-board BOOTSEL mass-storage image
- `uf2-manifest.json` — version, image size, and SHA-256 metadata

Never commit credentials, signing keys, or per-device tokens. Actual USB flashing, radio behavior, onboard LED behavior, and captive-portal behavior require a connected board for verification.
