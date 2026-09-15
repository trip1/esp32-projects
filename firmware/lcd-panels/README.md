# LCD1602 Network Panels

Five browser-flashable LCD1602 projects share bounded drivers and four exact ESP32 board profiles:

- **MQTT Home Status Panel** subscribes to one exact trusted-LAN topic. It recognizes `temperature_c` plus `humidity_percent`, recognizes a string `value`, and otherwise displays sanitized payload text. Payloads over 256 bytes are ignored; data is marked stale after two minutes.
- **UniFi Network Panel** reads a normalized metrics bridge. The bridge keeps the powerful UniFi API key off the display and returns only WAN state, latency, client count, AP count, and a freshness timestamp.
- **Space and Satellite Tracker** polls the public Where the ISS at endpoint for NORAD 25544 every ten seconds.
- **Wi-Fi Weather Desk Station** polls Open-Meteo current temperature, humidity, and WMO condition data every ten minutes for configured coordinates.
- **LCD1602 Smart Dashboard** combines ten screen types: Clock, MQTT Home, ISS, Weather, Next Launch, Moon, Solar Activity, Planet Visibility, Near-Earth Object, and Deep-Space Mission. Its protected setup orders every screen exactly once and assigns each screen its own 5-3600 second duration. The six space-summary screens refresh through `https://api.justsome.space/v1/lcd/space`; the request rounds configured coordinates to one decimal degree for planet visibility. Version 2.0.1 uses configuration schema v3; schema-v2 Wi-Fi, MQTT, weather, ordering, and duration settings migrate in memory, with the six new screens appended at 15 seconds each. Setup requests use a fixed global buffer so connecting a client cannot exhaust the Arduino loop task stack.

## Hardware and wiring

All builds support the Inland 1602 I²C module (SKU 221861 / KS0061) and HD44780-compatible 16×2 LCDs with the common PCF8574 mapping (P0=RS, P1=RW, P2=Enable, P3=backlight, P4–P7=data) at explicitly configured address `0x27`. On the fixed board-specific SDA/SCL pins, firmware reports address-phase responses only across the 16 possible PCF8574/PCF8574A addresses: `0x20`–`0x27` and `0x38`–`0x3F`. PCF8574 has no identity register, so alternate responders are diagnostic only and never receive data writes. The configured address must acknowledge repeatedly before initialization. Firmware never scans arbitrary GPIOs.

| Board | SDA | SCL | Setup button |
|---|---:|---:|---:|
| ESP32 DevKit V1 | GPIO21 | GPIO22 | BOOT / GPIO0 |
| ESP32-C3-DevKitM-1 | GPIO4 | GPIO5 | BOOT / GPIO9 |
| ESP32-S3-DevKitC-1 v1.0 | GPIO8 | GPIO9 | BOOT / GPIO0 |
| ESP32-C6-DevKitC-1 | GPIO6 | GPIO7 | BOOT / GPIO9 |

The Inland KS0061 is wired as a 5 V module. Power it from 5 V and use the required bidirectional I²C level shifter shown in each diagram: LV at 3.3 V, HV at 5 V, and common ground. Never expose an ESP32 GPIO to a 5 V pull-up. The responder scan clamps each transaction to its remaining 250 ms aggregate budget and 10 ms maximum. The checked display driver caps each write transaction at 25 ms, initialization at 250 ms, and each refresh at 150 ms, aborting on the first failed write.

## First boot

1. Open USB serial at 115200 baud.
2. Copy the randomized eight-character setup password. Ambiguous `I`, `O`, `0`, and `1` are omitted.
3. Join `LCD-Panel-Setup-XXXXXX` for a single-purpose panel, or `LCD-Dashboard-XXXXXX` for Smart Dashboard, then open `http://192.168.4.1/`.
4. Enter the fields shown for the selected project and choose **Save and test**.
5. Before dependency validation starts, an RTC rejection guard is armed so an eight-second transport restart cannot retry the same pending record forever. The candidate is CRC-checked in NVS and promoted only after Wi-Fi plus its MQTT/API dependency succeeds. A failed candidate is discarded while the previous active configuration is preserved in a separate fallback record.

To reopen setup, reset normally and hold BOOT for two seconds during the printed five-second application window. The protected AP has a separate per-boot state token, an exact local Host check, a strictly validated same-origin Origin or Referer on submissions, bounded headers/body/fields, one station maximum, and a ten-minute rotation timeout. Wi-Fi and MQTT credentials remain readable to someone with physical flash access because flash encryption is not enabled.

## UniFi metrics bridge contract

Do not put a UniFi controller or Site Manager API key on the ESP32. Official UniFi keys can authorize write operations and are not documented as read-only. Run a local bridge that holds the key, calls only approved GET endpoints, and returns this exact bounded JSON object:

```json
{"wan_up":true,"latency_ms":12,"clients":24,"access_points":2,"observed_at":1789340000}
```

The panel accepts only `http://<private-ip>[:port]/api/unifi/summary`, where the numeric address is in RFC1918 space (`10/8`, `172.16/12`, or `192.168/16`) and the port is 80, 8080, or 8090. HTTPS, public hosts, URL credentials, query strings, fragments, whitespace, other paths, and other ports are rejected. The bridge response is limited to 2048 bytes and must contain exactly the five correctly typed fields above. `observed_at` is Unix UTC and must be within two minutes of SNTP time. On a trusted LAN, an unauthenticated bridge endpoint should expose only these aggregates—not controller responses, identifiers, keys, or configuration.

The local Network Integration API base is normally `https://<console>/proxy/network/integration/v1`; check the version-matched **Network → Settings → Integrations** documentation before building a bridge. The firmware intentionally does not bypass a self-signed controller certificate.

## Network behavior

- HTTPS validates against bundled ISRG Root X1; `setInsecure()` is never used.
- Weather and ISS data require SNTP time before TLS and reject stale timestamps.
- HTTP operations use fixed buffers with 2048-byte aggregate headers, 256-byte header/chunk lines, a 2048-byte body cap, strict content-length/chunked framing, and a disposable task with an eight-second outer deadline. A deadline overrun restarts the device to reset transport state.
- MQTT accepts only a numeric RFC1918 broker address and one exact topic. Its local protocol client requires successful CONNACK and SUBACK, rejects oversized remaining lengths before draining them, caps packets and payloads, applies a six-second connect/subscribe deadline, disconnects two-second drip-fed packets, and uses ten-second reconnect backoff. Plain MQTT is suitable only on a trusted LAN.
- API errors retain no unbounded response and render an explicit unavailable state.
- Smart Dashboard runs at most one HTTP refresh worker at a time. LCD rotation and MQTT servicing continue while ISS, weather, or the five-minute DS9 space summary is in flight; snapshots cross tasks through a critical-section-protected fixed frame. Public feed failures affect only their cached/unavailable screens and do not block configuration promotion.

## Build and test

```bash
g++ -std=c++17 -Wall -Wextra -Werror -I ../common -I include src/panel_logic.cpp test/native/test_main.cpp -o /tmp/lcd-panel-tests
/tmp/lcd-panel-tests
~/.venvs/platformio/bin/pio run
```

Firmware builds do not prove physical operation. LCD voltage levels, PCF8574 mapping/address, contrast/backlight, selected GPIOs, setup recovery, Wi-Fi, MQTT delivery, metrics-bridge behavior, and live API refreshes remain hardware-unverified until tested on each named board.

## API references

- Open-Meteo forecast API: https://open-meteo.com/en/docs
- Where the ISS at API: https://wheretheiss.at/w/developer
- Official UniFi API overview: https://help.ui.com/hc/en-us/articles/30076656117655-Getting-Started-with-the-Official-UniFi-API
- UniFi developer portal: https://developer.ui.com/
- ISRG Root X1 certificate: https://letsencrypt.org/certificates/
