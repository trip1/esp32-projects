# Raspberry Pi Pico firmware lab

This PlatformIO project provides exact UF2 builds for four official Raspberry Pi boards:

- Raspberry Pi Pico — RP2040, 2 MB flash
- Raspberry Pi Pico W — RP2040, 2 MB flash, CYW43439 wireless
- Raspberry Pi Pico 2 — RP2350, 4 MB flash
- Raspberry Pi Pico 2 W — RP2350, 4 MB flash, CYW43439 wireless

Header-equipped official variants use the matching target; the header/debug-connector differences do not change the firmware image.

The build pins `maxgerhardt/platform-raspberrypi` to commit `5d4561a05e3b212660ac6fdd3fbfb328d1988aa1`. That integration currently resolves Arduino-Pico to `fd65f6d4ab168d4bb181a3709738fe353ac93585`.

## Projects

### Pico Board Check

Available on all four boards. It identifies the compiled board and chip family over USB serial, reports CPU and flash configuration, and blinks the onboard status LED every 500 ms.

### Pico Morse Beacon

Available on all four boards. It repeats `HELLO WORLD` in Morse code on the onboard status LED and prints each decoded character and symbol sequence over USB serial.

### Pico W Wi-Fi Surveyor

Available only on Pico W and Pico 2 W. As a deliberate privacy and resource policy, firmware performs one bounded station-mode scan at boot, stores at most 20 results, then changes to access-point mode instead of scanning while clients are connected. Join `Pico-W-Surveyor` with the randomized 8-character uppercase WPA2 password printed over USB serial; `I`, `O`, `0`, and `1` are omitted. Then open `http://192.168.4.1/`. Reboot to refresh the scan and rotate the password.

The dashboard is read-only, escapes SSIDs before HTML rendering, and accepts only a root-page GET with exactly one approved `Host` header through a fixed 1 KiB request buffer with a 1.5-second timeout. It accepts no router credentials and exposes no state-changing endpoint. Nearby SSIDs can be identifying information and are visible to every client that knows the temporary AP password.

## Install a UF2

1. Download the UF2 for the exact selected board from the portal.
2. Disconnect the Pico.
3. Hold **BOOTSEL** while reconnecting USB.
4. Release BOOTSEL when the `RPI-RP2` or `RP2350` mass-storage drive appears.
5. Copy the UF2 onto that drive. The Pico reboots automatically.

RP2040 and RP2350 UF2 files are not interchangeable. The portal does not route Pico binaries through ESP Web Tools. It uses Raspberry Pi's documented BOOTSEL mass-storage installation path instead of embedding an unreviewed third-party WebUSB flasher.

## Verify locally

```bash
g++ -std=c++17 -Wall -Wextra -Werror \
  -I firmware/pico-lab/include \
  firmware/pico-lab/src/pico_logic.cpp \
  firmware/pico-lab/test/native/test_main.cpp \
  -o /tmp/pico-lab-tests
/tmp/pico-lab-tests

~/.venvs/platformio/bin/pio run -d firmware/pico-lab
```

Compilation verifies source compatibility and artifact generation only. BOOTSEL installation, USB serial, onboard LEDs, Wi-Fi scanning, AP behavior, and the local dashboard remain hardware-unverified until exercised on physical boards.

## Sources

- Raspberry Pi Pico documentation: https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html
- Pico 2 datasheet: https://pip.raspberrypi.com/documents/RP-008299-DS-pico-2-datasheet.pdf
- Pico 2 W datasheet: https://pip.raspberrypi.com/documents/RP-008304-DS-pico-2-w-datasheet.pdf
- Arduino-Pico PlatformIO integration: https://arduino-pico.readthedocs.io/en/latest/platformio.html
- Arduino-Pico Wi-Fi limitations: https://arduino-pico.readthedocs.io/en/latest/wifi.html
