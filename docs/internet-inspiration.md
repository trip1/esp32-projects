# Internet inspiration for the board-only collection

The added projects are original firmware implementations built from established ESP32 patterns rather than copied sketches.

- **Pocket File Drop** combines the ESP32 SoftAP pattern—an ESP32 can host an HTTP server while acting as an access point—with the official LittleFS file operations example.[1][6]
- **Pocket Chat Room** adapts the common local access-point server pattern and the RFC6455 text-frame/broadcast capabilities documented by the Arduino WebSockets library.[1][5]
- **BLE UART Console** follows the NimBLE server model of services, characteristics, write callbacks, notifications, disconnect handling, and restarted advertising.[4]
- **Reboot Museum** grows from Espressif's Preferences example and API, which store small values in NVS across restarts and power loss.[2]
- **iBeacon Lab** follows Espressif's ESP32-C6 BLE advertising guidance and NimBLE's iBeacon example, including a 100 ms advertising interval and manufacturer-data beacon payload.[3][7]
- **Tiny Benchmark Lab** is an original bounded CPU and memory workload exposed through the same documented SoftAP-hosted HTTP pattern.[1]

Design constraints retained across the collection:

- Everything targets ESP32-C6 and requires no external sensor, display, relay, or other add-on.
- Experimental access points are local and open, so their pages explicitly say not to submit sensitive information.
- BLE input and chat messages are bounded before further processing.
- File names and file sizes are constrained before LittleFS writes.

## Sources

[1] https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html — Arduino ESP32 Wi-Fi API
[2] https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html — Arduino ESP32 Preferences API
[3] https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/ble/get-started/ble-device-discovery.html — ESP32-C6 BLE Device Discovery
[4] https://raw.githubusercontent.com/h2zero/NimBLE-Arduino/master/examples/NimBLE_Server/NimBLE_Server.ino — NimBLE Server Example
[5] https://registry.platformio.org/libraries/links2004/WebSockets — PlatformIO WebSockets Library
[6] https://raw.githubusercontent.com/espressif/arduino-esp32/master/libraries/LittleFS/examples/LITTLEFS_test/LITTLEFS_test.ino — Arduino ESP32 LittleFS Example
[7] https://raw.githubusercontent.com/h2zero/NimBLE-Arduino/master/examples/NimBLE_iBeacon/NimBLE_iBeacon.ino — NimBLE iBeacon Example
