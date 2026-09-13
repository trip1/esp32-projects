# ESP32 Hardware Lab

Three browser-flashable projects adapted from useful Random Nerd Tutorials patterns rather than copied sketches. Each target uses an explicit GPIO map for ESP32 DevKit V1, ESP32-C3-DevKitM-1, ESP32-S3-DevKitC-1 v1.0, or ESP32-C6-DevKitC-1.

## Ultrasonic Parking Assistant

The HC-SR04 project measures echo pulse duration, rejects missing/overlong echoes, applies a five-sample median, and serves a live parking/tank gauge from the open `ESP32-Parking-Assistant` network. The underlying RNT project demonstrates HC-SR04 distance measurement on ESP32.[2]

| Board | Trigger | Echo |
|---|---:|---:|
| ESP32 DevKit V1 | GPIO25 | GPIO26 |
| ESP32-C3-DevKitM-1 | GPIO4 | GPIO5 |
| ESP32-S3-DevKitC-1 v1.0 | GPIO4 | GPIO5 |
| ESP32-C6-DevKitC-1 | GPIO6 | GPIO7 |

Power a standard HC-SR04 from 5 V, but reduce its 5 V Echo output to 3.3 V with a resistor divider or level shifter before the ESP32 input. Tie all grounds together. The firmware limits `pulseIn()` to 30 ms so a missing echo cannot block forever.

## PIR Occupancy Timer

The PIR project records rising edges in a minimal ISR, counts motion in the main loop, and keeps occupancy active for 30 seconds after the latest event using rollover-safe timing. Its open `ESP32-PIR-Occupancy` network serves the live state and event count. The RNT inspiration uses interrupts and timers for PIR motion events.[3]

| Board | PIR signal |
|---|---:|
| ESP32 DevKit V1 | GPIO27 |
| ESP32-C3-DevKitM-1 | GPIO4 |
| ESP32-S3-DevKitC-1 v1.0 | GPIO4 |
| ESP32-C6-DevKitC-1 | GPIO6 |

Use a PIR module with a 3.3 V-safe signal output. Check the module's supply requirement; common HC-SR501 boards are typically powered from 5 V while their signal output is logic-level.

## NTP Desk Clock

The clock synchronizes through NTP and provides two selectable firmware builds per board: the original four-digit TM1637 display and an optional 16×2 HD44780 LCD with a PCF8574 I²C backpack. NTP provides network time without a separate RTC, while the configured POSIX timezone applies local offset and daylight-saving rules.[1][4] The TM1637 dependency is pinned, while the checked LCD1602 driver is maintained in this firmware. The LCD build writes a fixed-width date on row one and time with seconds on row two.[5]

**TM1637 build**

| Board | CLK | DIO | Setup button |
|---|---:|---:|---:|
| ESP32 DevKit V1 | GPIO18 | GPIO19 | BOOT / GPIO0 |
| ESP32-C3-DevKitM-1 | GPIO4 | GPIO5 | BOOT / GPIO9 |
| ESP32-S3-DevKitC-1 v1.0 | GPIO4 | GPIO5 | BOOT / GPIO0 |
| ESP32-C6-DevKitC-1 | GPIO6 | GPIO7 | BOOT / GPIO9 |

**LCD1602 I²C build**

| Board | SDA | SCL | Address | Setup button |
|---|---:|---:|---:|---:|
| ESP32 DevKit V1 | GPIO21 | GPIO22 | `0x27` | BOOT / GPIO0 |
| ESP32-C3-DevKitM-1 | GPIO4 | GPIO5 | `0x27` | BOOT / GPIO9 |
| ESP32-S3-DevKitC-1 v1.0 | GPIO8 | GPIO9 | `0x27` | BOOT / GPIO0 |
| ESP32-C6-DevKitC-1 | GPIO6 | GPIO7 | `0x27` | BOOT / GPIO9 |

Choose the display build in the portal before installing. Connect the selected module according to the board-specific diagram. After the I²C controller starts, the LCD build uses checked PCF8574 writes with a 25 ms transaction timeout, a 250 ms aggregate initialization deadline, a 150 ms display-refresh deadline, and fail-fast handling on the first bus error. It retries initialization every five seconds if the backpack is absent. A response at `0x27` confirms only that a device acknowledged; it does not prove the backpack model or pin mapping. A backpack at `0x3F` requires a local build with `CLOCK_LCD_ADDRESS=0x3F` after confirming both its address and compatible PCF8574-to-HD44780 mapping.

Power TM1637 from 3.3 V. For LCD1602, use a display/backpack whose contrast and backlight work at 3.3 V. Many PCF8574 backpacks pull SDA/SCL up to their VCC; if yours requires 5 V, use a bidirectional I²C level shifter and never connect 5 V pull-ups directly to ESP32 GPIO. On first boot:

1. Open USB serial at 115200 baud and copy the randomized 8-character uppercase setup password; `I`, `O`, `0`, and `1` are omitted.
2. Join `NTP-Clock-Setup-XXXXXX` with that password.
3. Open `http://192.168.4.1` and select Wi-Fi plus a supported timezone (US Central is the default).
4. Save. The clock stores a pending record, reboots, and promotes it only after Wi-Fi and NTP return a plausible date.

To reopen setup, reset normally and then hold BOOT for two seconds during the five-second recovery window. The setup AP uses a random 8-character WPA2 password with a 40-bit search space, a separate per-boot state token, strict Host/Origin checks, fixed request limits, and a ten-minute rotation timeout. Credentials are stored in CRC-checked NVS and are not encrypted at rest.

## Build and verification

```bash
g++ -std=c++17 -I include src/hardware_logic.cpp test/native/test_main.cpp -o /tmp/hardware-lab-tests
/tmp/hardware-lab-tests
g++ -std=c++17 -Wall -Wextra -Werror -I include src/ntp_clock_display.cpp test/clock_native/test_main.cpp -o /tmp/ntp-clock-display-tests
/tmp/ntp-clock-display-tests
~/.venvs/platformio/bin/pio run
```

The firmware builds do not prove electrical safety or physical behavior. Sensor accuracy, PIR module levels, TM1637/LCD1602 operation, LCD backpack address and voltage levels, setup-button timing, NTP synchronization, and all selected GPIOs remain hardware-unverified until tested on the named boards.

## Sources

[1] https://randomnerdtutorials.com/esp32-date-time-ntp-client-server-arduino — ESP32 NTP Client-Server
[2] https://randomnerdtutorials.com/esp32-hc-sr04-ultrasonic-arduino — ESP32 HC-SR04 Ultrasonic Sensor
[3] https://randomnerdtutorials.com/esp32-pir-motion-sensor-interrupts-timers — ESP32 PIR Motion Interrupts
[4] https://randomnerdtutorials.com/esp32-ntp-digital-clock-timezone — ESP32 NTP Digital Clock
[5] https://github.com/avishorp/TM1637 — TM1637 Arduino Library
