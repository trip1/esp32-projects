# ESP32 Diagnostic Console

A self-contained, router-connected diagnostic dashboard for ESP32 DevKit V1, ESP32-C3-DevKitM-1, ESP32-S3-DevKitC-1 v1.0, and ESP32-C6-DevKitC-1.

## Setup

1. Flash the exact board image.
2. Open USB serial at 115200 baud.
3. Join the `ESP-Diagnostics-XXXXXX` setup network using the randomized 8-character password printed over serial. It omits `I`, `O`, `0`, and `1`.
4. Open `http://192.168.4.1/` if the setup page does not appear automatically, then enter only the Wi-Fi name and password.
5. After validation and restart, open the local IPv4 address printed over serial.

Wi-Fi settings are held as a pending CRC-checked NVS record until the board obtains a usable address. To erase the saved network, press **RESET normally**, then hold **BOOT for at least 1.5 seconds during the five-second recovery window printed over serial**. Do not hold BOOT while powering on or resetting because it is a boot-strapping button. Provisioning accepts one station, rejects oversized/malformed requests, requires an exact local Host/Origin, and closes after ten minutes.

## Dashboard

The monitor-style dashboard targets one sample per second without overlapping requests and charts:

- free heap;
- Wi-Fi RSSI;
- internal chip temperature.

It also reports chip model/revision/cores/frequency, reset and wake causes, SDK version, heap/PSRAM details, flash/sketch capacity, SSID/BSSID/channel/addressing, network request count, controller counts, and board-specific bus pins. Samples remain only in the current browser tab and are capped at 900 points. Pause/resume and 1/5/15-minute ranges are interactive.

## I²C and SPI

The firmware performs a bounded I²C address-ACK sweep with no payload bytes on these pins:

| Board | SDA | SCL |
|---|---:|---:|
| ESP32 DevKit V1 | GPIO21 | GPIO22 |
| ESP32-C3-DevKitM-1 | GPIO4 | GPIO5 |
| ESP32-S3-DevKitC-1 v1.0 | GPIO8 | GPIO9 |
| ESP32-C6-DevKitC-1 | GPIO6 | GPIO7 |

Each address-phase write transaction has a 10 ms timeout. The dashboard shows up to 32 responding addresses and conservative address-family hints. Address hints are not device identity proof. Manual rescans have a ten-second cooldown because scanning can disturb devices that do not tolerate unexpected traffic.

SPI cannot enumerate devices: each peripheral requires a known chip-select pin and protocol. The dashboard therefore reports the board profile's default SCK/MISO/MOSI/SS pins without initializing, toggling, or claiming a device is present.

## Security and limits

The dashboard is plain HTTP intended for a trusted private LAN. It has no user authentication and must not be exposed to the internet. Responses set a restrictive CSP, no-sniff, no-referrer, no-store, and frame-denial controls. Requests, writes, and request rates are bounded; browser POSTs require an exact Origin and per-boot token. The rescan action also has a ten-second cooldown. Wi-Fi credentials remain readable to an attacker with physical flash access.

## Build and test

```bash
g++ -std=c++17 -Wall -Wextra -Werror \
  -I include src/diagnostics_logic.cpp test/native/test_main.cpp \
  -o /tmp/esp32-diagnostics-tests
/tmp/esp32-diagnostics-tests

~/.venvs/platformio/bin/pio run
```

Compilation and native tests do not verify physical flashing, Wi-Fi association, browser behavior, I²C electrical compatibility, or the accuracy of internal temperature readings. Test the exact board and attached bus before relying on the results.
