# BME280 MQTT Deep-Sleep Sensor

A battery-conscious temperature, humidity, and barometric-pressure node for the four boards supported by the Device Foundry portal. It samples a BME280 in forced mode, makes one bounded Wi-Fi/MQTT attempt, publishes retained JSON, and enters timer deep sleep. The Adafruit driver supports BME280 temperature, pressure, and humidity measurements.[1]

## Wiring

Power the BME280 breakout from **3.3 V**, not a raw battery voltage. Connect `GND` to ground. The firmware checks both common I2C addresses, `0x76` and `0x77`.

| Portal target | SDA | SCL | Setup button |
|---|---:|---:|---:|
| ESP32 DevKit V1 | GPIO21 | GPIO22 | BOOT / GPIO0 |
| ESP32-C3-DevKitM-1 | GPIO4 | GPIO5 | BOOT / GPIO9 |
| ESP32-S3-DevKitC-1 v1.0 | GPIO8 | GPIO9 | BOOT / GPIO0 |
| ESP32-C6-DevKitC-1 | GPIO6 | GPIO7 | BOOT / GPIO9 |

I2C requires SDA/SCL pull-ups; most BME280 breakout boards include them, but a bare sensor does not.[2]

## First-boot setup

1. Flash the image for the exact board.
2. Open USB serial at 115200 baud and copy the random setup password printed by the board.
3. Join `BME280-Setup-XXXXXX` with that password.
4. Open `http://192.168.4.1` if the captive page does not appear.
5. Enter Wi-Fi, MQTT, topic-prefix, and wake-interval settings.
6. Save. The board stores them as pending, reboots, samples, and attempts a retained publish. It promotes the pending record only after publication succeeds.

To reopen setup, press **RESET normally**, then hold **BOOT for two seconds during the five-second recovery window printed on serial**. Do not hold BOOT while resetting, because that selects the ROM download mode instead of running the application.

The temporary setup network uses a new random WPA2 password each time and accepts one station. State-changing requests also carry a per-boot token, and the firmware rejects HTTP headers over 1 KiB, bodies over 1 KiB, unknown fields, duplicate fields, and incomplete requests. Provisioning times out after ten minutes and returns to deep sleep. Credentials are stored as CRC-checked NVS records and survive reset and deep sleep; Preferences/NVS retains values across power loss.[3]

The last verified active configuration is kept separately from a pending replacement. Failed Wi-Fi/MQTT settings are discarded without overwriting the active record.

## MQTT

This release uses plain MQTT intended for a trusted private LAN. Do not point it directly at an internet broker. Credentials are not encrypted at rest. A future TLS profile should require a validated broker CA rather than disabling certificate checks.

Retained topic:

```text
<topic-prefix>/<chip>-bme280-<wifi-mac>/state
```

Example payload:

```json
{
  "device_id": "esp32c6-bme280-aabbccddeeff",
  "sensor": "BME280",
  "valid": true,
  "temperature_c": 22.41,
  "humidity_percent": 46.18,
  "pressure_hpa": 1009.72,
  "i2c_address": "0x76",
  "wifi_rssi_dbm": -58,
  "sleep_minutes": 5,
  "awake_ms": 1832
}
```

PubSubClient supports retained publication; this firmware publishes one retained state message and disconnects cleanly before sleeping.[4] Delivery is QoS 0 and there is no persistent outbox: a failed sample or network attempt is reported over serial and retried at the next scheduled wake.

## Power behavior

The BME280 is placed into forced measurement mode, which returns it to sleep after sampling. The ESP32 configures a timer wakeup and enters deep sleep after every attempt; timer wakeup is the documented Arduino-ESP32 pattern.[5] Missing configuration opens provisioning for at most ten minutes, and AP, timer-setup, or network failures use bounded battery-safe sleep behavior rather than reboot loops.

Development boards include regulators, USB bridges, and power LEDs that can dominate sleep current. Deep sleep reduces MCU/radio consumption, but a purpose-built low-power board and regulator are better for long battery life. Measure the complete assembled device before estimating runtime.

## Build and verify

```bash
g++ -std=c++17 \
  -I include \
  src/sensor_config_logic.cpp \
  test/native/test_main.cpp \
  -o /tmp/bme280-config-tests
/tmp/bme280-config-tests

~/.venvs/platformio/bin/pio run
```

Actual sensor readings, MQTT delivery, setup-button behavior, and sleep current remain unverified until tested with each physical board and a BME280.

## Sources

[1] https://registry.platformio.org/libraries/adafruit/Adafruit%20BME280%20Library — Adafruit BME280 Library
[2] https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html — Arduino-ESP32 I2C API
[3] https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html — Arduino-ESP32 Preferences API
[4] https://pubsubclient.knolleary.net/api — PubSubClient API
[5] https://docs.espressif.com/projects/arduino-esp32/en/latest/api/deepsleep.html — Arduino-ESP32 Deep Sleep
