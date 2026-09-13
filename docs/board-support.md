# Supported microcontroller boards

The portal publishes exact-board artifacts for four ESP32 profiles and four Raspberry Pi Pico profiles. Select the physical board before downloading or installing firmware; binaries are not interchangeable across chip families.

## ESP32 profiles

- **ESP32 DevKit V1 / ESP32 Dev Module** — PlatformIO board `esp32dev`, ESP Web Tools family `ESP32`.
- **ESP32-C3-DevKitM-1** — PlatformIO board `esp32-c3-devkitm-1`, ESP Web Tools family `ESP32-C3`.
- **ESP32-S3-DevKitC-1 v1.0** — PlatformIO board `esp32-s3-devkitc-1`, ESP Web Tools family `ESP32-S3`.
- **ESP32-C6-DevKitC-1** — PlatformIO board `esp32-c6-devkitc-1`, ESP Web Tools family `ESP32-C6`.

All four ESP32 profiles provide the Wi-Fi, BLE, flash, and serial capabilities used by the non-LED ESP projects. Espressif documents the C3 and C6 boards' addressable RGB LED on GPIO8.[2][5]

The S3 LED builds intentionally target **ESP32-S3-DevKitC-1 v1.0**, whose RGB LED is on GPIO48.[3] Espressif's v1.1 revision moves that LED to GPIO38, so the three original LED projects are not claimed to support S3 v1.1.[4]

The generic ESP32 DevKit profile receives seventeen projects: every original project except the three that require a standardized addressable RGB LED. Its documented controls include a power LED, USB-to-UART bridge, Boot button, and reset button, but not a standardized software-controlled addressable RGB LED.[1]

### ESP32 compatibility

- **All four ESP32 profiles:** BLE Proximity Scanner, Wi-Fi Surveyor, Device Console, ESP32 Diagnostic Console, BLE Presence Beacon, Decision Oracle, BLE Alias Shuffler, Pocket File Drop, Pocket Chat Room, BLE UART Console, Reboot Museum, Tiny Benchmark Lab, iBeacon Lab, BME280 MQTT Sleep Sensor, Ultrasonic Parking Assistant, PIR Occupancy Timer, and NTP Desk Clock.
- **C3, S3 v1.0, and C6 only:** Pocket RGB Lamp, Pomodoro Light, and Morse Beacon.

ESP32 targets use ESP Web Tools through each development board's USB-to-UART connector. The S3 and C6 native-USB connectors are not claimed as verified flashing paths.

## Raspberry Pi Pico profiles

- **Raspberry Pi Pico** — RP2040, PlatformIO board `rpipico`, 2 MB flash, no wireless.
- **Raspberry Pi Pico W** — RP2040, PlatformIO board `rpipicow`, 2 MB flash, CYW43439 Wi-Fi/Bluetooth radio.
- **Raspberry Pi Pico 2** — RP2350, PlatformIO board `rpipico2`, 4 MB flash, no wireless.
- **Raspberry Pi Pico 2 W** — RP2350, PlatformIO board `rpipico2w`, 4 MB flash, CYW43439 Wi-Fi/Bluetooth radio.

Header-equipped Pico H, Pico WH, Pico 2 with headers, and Pico 2 W with headers use the matching profile above. Raspberry Pi documents these as the same core hardware with presoldered headers/debug-connector differences, so they do not need separate binaries.[6]

The Pico build pins `maxgerhardt/platform-raspberrypi` at commit `5d4561a05e3b212660ac6fdd3fbfb328d1988aa1` and uses its Earle Philhower Arduino-Pico integration. All four exact environments produced UF2 artifacts in local builds.

### Pico compatibility

- **Pico, Pico W, Pico 2, and Pico 2 W:** Pico Board Check and Pico Morse Beacon.
- **Pico W and Pico 2 W only:** Pico W Wi-Fi Surveyor.

Wireless hardware does not mean every ESP32 Wi-Fi or BLE project is already portable. The current Pico release intentionally excludes ESP-specific NVS, provisioning, BLE, deep-sleep, and peripheral applications until each one has a native Pico implementation and real target build. Pico W Wi-Fi Surveyor deliberately scans before starting its password-protected AP; this is project policy, not a claim that the framework lacks combined-mode APIs.

Pico targets use official BOOTSEL mass-storage flashing: hold BOOTSEL while connecting USB, then copy the exact `.uf2` onto the `RPI-RP2` or `RP2350` drive.[6] The portal offers downloads and does not present Pico images through ESP Web Tools or claim automatic browser flashing.

## External hardware

Four ESP32 projects currently require external hardware: BME280 MQTT Sleep Sensor, Ultrasonic Parking Assistant, PIR Occupancy Timer, and NTP Desk Clock. Their exact-board diagrams, connection tables, parts lists, and voltage cautions appear in the portal.

No Pico project in this first release requires external wiring. Pico wiring diagrams will be added when sensor/display projects are ported.

## Verification limits

Compilation, native tests, UF2 generation, and portal packaging do not verify physical hardware. BOOTSEL copying, USB serial, onboard LED behavior, Pico W scanning/AP behavior, ESP flashing, external wiring, and peripheral behavior remain explicitly hardware-unverified.

## Sources

[1] https://docs.espressif.com/projects/esp-idf/en/v3.2.5/get-started/get-started-devkitc.html — ESP32-DevKitC V4 Guide
[2] https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c3/esp32-c3-devkitm-1/user_guide.html — ESP32-C3-DevKitM-1 Guide
[3] https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html — ESP32-S3-DevKitC-1 v1.0 Guide
[4] https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html — ESP32-S3-DevKitC-1 v1.1 Guide
[5] https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html — ESP32-C6-DevKitC-1 Guide
[6] https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html — Raspberry Pi Pico boards and BOOTSEL flashing
[7] https://arduino-pico.readthedocs.io/en/latest/platformio.html — Arduino-Pico PlatformIO integration
[8] https://arduino-pico.readthedocs.io/en/latest/wifi.html — Pico W Wi-Fi support and limitations
