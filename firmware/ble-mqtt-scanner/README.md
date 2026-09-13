# ESP32 BLE → MQTT Scanner

Continuously scans nearby **Bluetooth Low Energy** advertisements, keeps a bounded JSONL sighting log in LittleFS, publishes deduplicated sightings, and maintains retained MQTT proximity state. The firmware intentionally scans BLE only on every target.

## Default behavior

- Active BLE scan in repeating 30-second windows.
- Tracks up to 256 recently observed addresses.
- Logs and publishes the first sighting, then at most once every five minutes per address.
- Local logs rotate between `/sightings.jsonl` and `/sightings.1.jsonl` at 384 KiB each.
- MQTT status is retained as `online`/`offline` using a last will; queue overflows are reported in status JSON and on serial.
- Devices stronger than `-75 dBm` enter proximity; they change to away after 90 seconds without an advertisement.
- Presence changes missed during an MQTT outage remain pending in RAM and are published after reconnect.
- No connections are made to discovered BLE devices.

Addresses can be randomized by modern BLE devices, so an address is an observed radio identifier—not a reliable person identity. Follow applicable privacy and consent rules.

The proximity feature works best with BLE tags and sensors that advertise stable addresses. Phones and watches may rotate addresses, stop advertising while locked, or omit a useful name, so they are not dependable identity beacons.

## First-boot setup

1. Flash the image for the exact board and open USB serial at 115200 baud.
2. Copy the randomized 8-character uppercase setup password printed by the scanner. It omits `I`, `O`, `0`, and `1`.
3. Join `BLE-Scanner-Setup-XXXXXX` with that password.
4. Open `http://192.168.4.1` if the captive page does not appear.
5. Enter Wi-Fi, MQTT broker, optional MQTT credentials, and topic-prefix settings.
6. Save. The scanner stores the candidate separately, reboots, and promotes it only after both Wi-Fi and a non-retained MQTT validation publication succeed.

To reopen setup, press **RESET normally**, then hold **BOOT for two seconds during the five-second serial recovery window**. Do not hold BOOT while resetting because that can select ROM download mode instead of running the firmware.

The setup network uses a new random 8-character WPA2 password each session and accepts one station. Its unambiguous 32-character alphabet provides a 40-bit password search space. State changes require a separate per-boot token. HTTP headers and bodies are bounded to 1 KiB each, malformed framing and duplicate/unknown form fields are rejected, and the portal rotates after ten minutes. The last verified active configuration remains available when replacement settings fail.

Credentials and local LittleFS sighting logs are not encrypted at rest. Protect physical access to the board. MQTT is plain TCP for a trusted LAN; use a VPN or a future certificate-validated TLS profile before sending sightings over an untrusted network.

## MQTT topics

Each board derives a stable scanner ID from its Wi-Fi station MAC. The setup page defaults the topic prefix to `esp32/ble-sightings`:

```text
esp32/ble-sightings/<chip-prefix>-<mac>/events
esp32/ble-sightings/<chip-prefix>-<mac>/status
esp32/ble-sightings/<chip-prefix>-<mac>/presence/<address-without-colons>
```

Prefixes are `esp32`, `esp32c3`, `esp32s3`, and `esp32c6`. Existing C6 installations keep the original `esp32c6-<mac>` identity, so their retained MQTT topics do not move during the multi-board upgrade.

Presence topics are retained and contain `present`, `state`, RSSI, name, address, and last-seen time. Tune proximity behavior in `include/config.h` or with build flags:

```cpp
#define PRESENCE_ENTER_RSSI (-75)
#define PRESENCE_EXIT_TIMEOUT_MS 90000UL
#define PRESENCE_TRACKER_CAPACITY 128
```

Example event:

```json
{
  "scanner_id": "esp32c6-aabbccddeeff",
  "address": "11:22:33:44:55:66",
  "address_type": 1,
  "name": "Nearby sensor",
  "rssi": -61,
  "manufacturer_data": "4c0002...",
  "service_uuids": "180f,181a",
  "first_seen_ms": 1240,
  "last_seen_ms": 301240,
  "seen_count": 87,
  "seen_at_unix": 1789000000
}
```

## Build and test

PlatformIO is installed at `~/.venvs/platformio/bin/pio` on this development machine.

```bash
~/.venvs/platformio/bin/pio test -e native
~/.venvs/platformio/bin/pio run
```

Flash and monitor after connecting the board:

```bash
~/.venvs/platformio/bin/pio run -e scanner--esp32-c6 -t upload
~/.venvs/platformio/bin/pio device monitor -b 115200
```

Available upload environments are `scanner--esp32`, `scanner--esp32-c3`, `scanner--esp32-s3`, and `scanner--esp32-c6`.

## Local log access

The firmware logs one JSON object per line. The MQTT payload omits `mqtt_published`; the local copy adds that field so offline periods are visible. Logging is bounded, but long MQTT outages are **not replayed automatically**—the local file remains the source for those missed publications.
