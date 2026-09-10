# ESP32-C6 No-Hardware Lab

Fourteen independently flashable firmware experiments that use only the ESP32-C6 DevKitC-1 itself. No sensors, displays, relays, or other add-ons are required.

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

The LED projects expect the ESP32-C6-DevKitC-1 onboard addressable RGB LED on GPIO 8. Board revisions without that LED still run and report over serial but cannot show colors.

## Build

```bash
~/.venvs/platformio/bin/pio run
```

Build one image with `pio run -e <environment>`. Each environment produces `firmware.factory.bin` for browser flashing and `firmware.bin` for future OTA use.
