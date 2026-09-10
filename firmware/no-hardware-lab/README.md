# ESP32 No-Hardware Lab

This collection contains fourteen independently flashable experiments. Twelve support ESP32 DevKit V1, ESP32-C3-DevKitM-1, ESP32-S3-DevKitC-1 v1.0, and ESP32-C6-DevKitC-1; the three RGB projects support C3, S3 v1.0, and C6. No sensors, displays, relays, or other add-ons are required.

## Projects

- **Wi-Fi Surveyor** — local channel and signal dashboard
- **Pocket RGB Lamp** — browser control for the onboard RGB LED
- **Device Console** — local system diagnostics dashboard
- **BLE Presence Beacon** — stable BLE advertiser for scanner testing
- **Decision Oracle** — offline dice, coin, and dubious-answer server
- **Pomodoro Light** — onboard-LED focus timer
- **Morse Beacon** — onboard-LED Morse experiment
- **BLE Alias Shuffler** — rotating harmless joke BLE names
- **Pocket File Drop** — LittleFS upload/download portal
- **Pocket Chat Room** — local WebSocket group chat
- **BLE UART Console** — Nordic UART service and USB serial bridge
- **Reboot Museum** — persistent NVS boot history
- **Tiny Benchmark Lab** — bounded CPU and memory benchmarks
- **iBeacon Lab** — fixed lab beacon for scanner testing

The AP-based projects create an open local network named after the project and serve their UI at `http://192.168.4.1`. They do not provide internet access. Anyone nearby who joins an experimental AP can use its controls; for Pocket File Drop that includes downloading, replacing, and deleting stored files, and for Reboot Museum it includes rebooting or clearing history. Do not enter or store sensitive information.

The three LED projects are not built for the generic ESP32 DevKit profile. They use the onboard addressable RGB LED defined by the C3, S3 v1.0, and C6 board variants. ESP32-S3-DevKitC-1 v1.1 moves its LED to a different pin and is not included in the LED compatibility claim; see [`../../docs/board-support.md`](../../docs/board-support.md).

## Build

```bash
~/.venvs/platformio/bin/pio run
```

Build one image with `pio run -e <environment>`. Each environment produces `firmware.factory.bin` for browser flashing and `firmware.bin` for future OTA use.
