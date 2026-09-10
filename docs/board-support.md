# Supported ESP32 boards

The firmware portal builds each project independently for these targets:

- **ESP32 DevKit V1 / ESP32 Dev Module** — PlatformIO board `esp32dev`, manifest family `ESP32`.
- **ESP32-C3-DevKitM-1** — PlatformIO board `esp32-c3-devkitm-1`, manifest family `ESP32-C3`.
- **ESP32-S3-DevKitC-1 v1.0** — PlatformIO board `esp32-s3-devkitc-1`, manifest family `ESP32-S3`.
- **ESP32-C6-DevKitC-1** — PlatformIO board `esp32-c6-devkitc-1`, manifest family `ESP32-C6`.

All four profiles provide the Wi-Fi, BLE, flash, and serial capabilities used by the non-LED projects. Espressif documents the C3 board's addressable RGB LED on GPIO8 and the C6 board's addressable RGB LED on GPIO8.[2][5]

The S3 LED builds intentionally target **ESP32-S3-DevKitC-1 v1.0**, whose RGB LED is on GPIO48.[3] Espressif's v1.1 revision moves that LED to GPIO38, so the three LED projects are not claimed to support S3 v1.1 yet.[4]

The generic ESP32 DevKit profile receives sixteen projects: every project except the three that require a standardized addressable RGB LED. Its documented board controls include a power LED, USB-to-UART bridge, Boot button, and reset button, but not a software-controlled addressable RGB LED.[1]

## Compatibility

- **All four targets:** BLE Proximity Scanner, Wi-Fi Surveyor, Device Console, BLE Presence Beacon, Decision Oracle, BLE Alias Shuffler, Pocket File Drop, Pocket Chat Room, BLE UART Console, Reboot Museum, Tiny Benchmark Lab, iBeacon Lab, BME280 MQTT Sleep Sensor, Ultrasonic Parking Assistant, PIR Occupancy Timer, and NTP Desk Clock.
- **C3, S3 v1.0, and C6 only:** Pocket RGB Lamp, Pomodoro Light, and Morse Beacon.

Four projects require external hardware: BME280 MQTT Sleep Sensor, Ultrasonic Parking Assistant, PIR Occupancy Timer, and NTP Desk Clock. Their board-specific wiring and voltage cautions are documented in their project READMEs.

Select the exact board in the portal before installing. ESP Web Tools uses the selected target's chip-family manifest and factory image; images are not interchangeable across chip families.

Use each development board's **USB-to-UART** connector for flashing and serial output. The S3 and C6 boards also expose native USB, but these firmware profiles do not claim CDC-on-boot behavior through that connector.[3][5] Native-USB operation and physical runtime behavior remain unverified until tested on each board.

## Sources

[1] https://docs.espressif.com/projects/esp-idf/en/v3.2.5/get-started/get-started-devkitc.html — ESP32-DevKitC V4 Guide
[2] https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c3/esp32-c3-devkitm-1/user_guide.html — ESP32-C3-DevKitM-1 Guide
[3] https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html — ESP32-S3-DevKitC-1 v1.0 Guide
[4] https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html — ESP32-S3-DevKitC-1 v1.1 Guide
[5] https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html — ESP32-C6-DevKitC-1 Guide
