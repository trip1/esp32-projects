# DS9 ESP32 Projects

A monorepo for ESP32 firmware projects and a browser-based installer hosted with GitHub Pages.

## Included projects

- [`firmware/ble-mqtt-scanner`](firmware/ble-mqtt-scanner) — ESP32-C6 BLE scanner with MQTT sightings, retained proximity state, and bounded LittleFS logs.

## Firmware portal

The site in [`web/`](web) uses [ESP Web Tools](https://esphome.github.io/esp-web-tools/) and Web Serial. It is fully static: **ESP Web Tools does not require a backend server**. GitHub Pages provides the required HTTPS origin and serves the installer, manifest, and firmware binaries.

The published installer is intended for desktop Chrome or Edge. Browser support, USB permissions, and serial-port selection are handled locally by the browser.

## Repository layout

```text
firmware/                         PlatformIO firmware projects
  ble-mqtt-scanner/
web/                              Static installer source
scripts/                          Manifest and site assembly tools
tests/                            Portal tooling tests
.github/workflows/pages.yml       Firmware CI and Pages deployment
```

## Local verification

```bash
python3 -m unittest discover -s tests -v
~/.venvs/platformio/bin/pio test -d firmware/ble-mqtt-scanner -e native
~/.venvs/platformio/bin/pio run -d firmware/ble-mqtt-scanner -e esp32-c6-devkitc-1
python3 scripts/build_site.py \
  --firmware-build firmware/ble-mqtt-scanner/.pio/build/esp32-c6-devkitc-1 \
  --output _site \
  --version "$(tr -d '\n\r ' < VERSION)"
```

Serve `_site` from localhost to inspect the page. Web Serial is available only in a secure context; GitHub Pages supplies HTTPS for production.

## Adding firmware projects

Each project should have its own PlatformIO directory under `firmware/`, native tests for hardware-independent behavior, an ESP Web Tools manifest entry, and a CI build that copies both artifacts:

- `firmware.factory.bin` — complete image for USB installation and recovery
- `firmware.bin` — application-only image for OTA
- `ota-manifest.json` — generated version, image size, and SHA-256 metadata for future device-pulled updates

Never commit `secrets.h`, Wi-Fi credentials, broker credentials, signing keys, or per-device tokens.

## OTA status

The deployment publishes the application-only `firmware.bin` needed for OTA, but the current BLE scanner does not yet download updates itself. HTTPS pull-based OTA, runtime provisioning, signed manifests, and rollback self-tests belong in a dedicated firmware change before unattended updates are enabled.
