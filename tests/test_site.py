import configparser
import hashlib
import json
import re
import struct
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from scripts import build_site

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_NEW_PROJECTS = {
    "wifi-surveyor",
    "rgb-lamp",
    "device-console",
    "ble-presence-beacon",
    "decision-oracle",
    "pomodoro-light",
    "morse-beacon",
    "ble-alias-shuffler",
    "littlefs-dropbox",
    "local-chat-room",
    "ble-uart-console",
    "boot-counter",
    "cpu-benchmark",
    "ibeacon-lab",
    "bme280-mqtt-sensor",
    "hc-sr04-parking",
    "pir-occupancy-timer",
    "ntp-desk-clock",
    "pico-board-check",
    "pico-morse-beacon",
    "pico-wifi-surveyor",
    "esp32-diagnostics",
    "mqtt-home-status-panel",
    "unifi-network-panel",
    "space-satellite-tracker",
    "wifi-weather-station",
    "lcd1602-smart-dashboard",
}
ESP_TARGETS = {
    "esp32-devkit-v1": "ESP32",
    "esp32-c3-devkitm-1": "ESP32-C3",
    "esp32-s3-devkitc-1": "ESP32-S3",
    "esp32-c6-devkitc-1": "ESP32-C6",
}
PICO_TARGETS = {
    "raspberry-pi-pico": "RP2040",
    "raspberry-pi-pico-w": "RP2040",
    "raspberry-pi-pico-2": "RP2350",
    "raspberry-pi-pico-2-w": "RP2350",
}


def make_uf2(family_id, payload=b"test"):
    data = payload.ljust(256, b"\0")
    header = struct.pack(
        "<IIIIIIII",
        0x0A324655,
        0x9E5D5157,
        0x00002000,
        0x10000000,
        256,
        0,
        1,
        family_id,
    )
    return header + data + bytes(508 - len(header) - len(data)) + struct.pack("<I", 0x0AB16F30)
LED_PROJECTS = {"rgb-lamp", "pomodoro-light", "morse-beacon"}
EXTRA_HARDWARE_PROJECTS = {
    "bme280-mqtt-sensor", "hc-sr04-parking", "pir-occupancy-timer", "ntp-desk-clock",
    "mqtt-home-status-panel", "unifi-network-panel", "space-satellite-tracker", "wifi-weather-station",
    "lcd1602-smart-dashboard",
}


class FirmwarePortalTests(unittest.TestCase):
    def load_catalog(self):
        return json.loads((ROOT / "projects.json").read_text())

    def test_catalog_contains_twenty_three_projects(self):
        catalog = self.load_catalog()
        slugs = {project["slug"] for project in catalog}
        self.assertEqual(EXPECTED_NEW_PROJECTS, slugs - {"ble-mqtt-scanner"})
        self.assertEqual(28, len(catalog))
        for project in catalog:
            self.assertTrue(project["installable"])
            self.assertEqual(project["slug"] in EXTRA_HARDWARE_PROJECTS, project["extra_hardware"])
            self.assertTrue(project["hardware"])

    def test_catalog_preserves_existing_esp32_board_support(self):
        catalog = self.load_catalog()
        for project in catalog:
            targets = {target["id"]: target["chip"] for target in project["targets"] if target["id"] in ESP_TARGETS}
            expected = dict(ESP_TARGETS)
            if project["slug"] in LED_PROJECTS:
                expected.pop("esp32-devkit-v1")
            if project["slug"].startswith("pico-"):
                expected = {}
            self.assertEqual(expected, targets, project["slug"])
            for target in project["targets"]:
                self.assertTrue(target["name"])
                self.assertTrue(target["environment"])
        self.assertEqual(107, sum(len(project["targets"]) for project in catalog))
        self.assertEqual(111, sum(len(project["targets"]) + sum(len(target.get("variants", [])) for target in project["targets"]) for project in catalog))

    def test_lcd1602_smart_dashboard_combines_all_screens(self):
        project = next(project for project in self.load_catalog() if project["slug"] == "lcd1602-smart-dashboard")
        self.assertEqual("1.1.0", project["version"])
        self.assertEqual(set(ESP_TARGETS), {target["id"] for target in project["targets"]})
        self.assertIn("four-screen order", project["setup"]["summary"])
        self.assertNotIn("UniFi", project["description"])
        self.assertNotIn("UniFi summary bridge URL", project["setup"]["fields"])
        root = ROOT / project["project_dir"]
        source = (root / "src" / "dashboard_main.cpp").read_text()
        config_source = (root / "src" / "dashboard_config.cpp").read_text()
        logic = (root / "src" / "dashboard_logic.cpp").read_text()
        for required in ("DashboardScreen::Clock", "DashboardScreen::Mqtt", "DashboardScreen::Satellite", "DashboardScreen::Weather", "sntp_set_time_sync_notification_cb", "serviceRefresh"):
            self.assertIn(required, source)
        self.assertNotIn("DashboardScreen::Unifi", source)
        self.assertNotIn("fetchUnifi", source)
        for required in ("order_", "duration_%s", "min=5 max=3600", "Content-Security-Policy", "dashboardStoreConfig(\"pending\"", "<title>", "<html lang=en>", "Leave blank to keep current"):
            self.assertIn(required, config_source)
        self.assertIn("dashboardSlotExpired", logic)
        self.assertIn("dashboardDurationForSlot", logic)
        self.assertIn('if (force_backup_marker == kRejectedPendingMarker) return loadExact("backup", value);', config_source)
        self.assertIn('nvs_set_u32(handle, "forcebak"', config_source)
        self.assertIn('nvs_get_u32(handle, "forcebak"', config_source)
        self.assertNotIn('RTC_DATA_ATTR uint32_t force_backup_marker', config_source)
        for message in ("request is incomplete", "setup session expired", "timezone", "screen order", "MQTT", "weather"):
            self.assertIn(message, config_source)
        self.assertNotIn("unifi_url", config_source)
        self.assertNotIn("UniFi bridge", config_source)
        self.assertIn('role=alert', config_source)
        self.assertIn('return mqtt_ok && fetchWeather(candidate);', source)
        self.assertIn('for (;;) delay(1000)', source)
        platform = configparser.ConfigParser(interpolation=None)
        platform.read(root / "platformio.ini")
        for target in project["targets"]:
            self.assertIn(f"env:{target['environment']}", platform)

    def test_lcd1602_network_panels_cover_all_esp_boards(self):
        projects = {project["slug"]: project for project in self.load_catalog()}
        expected = {
            "mqtt-home-status-panel",
            "unifi-network-panel",
            "space-satellite-tracker",
            "wifi-weather-station",
        }
        self.assertTrue(expected <= projects.keys())
        for slug in expected:
            project = projects[slug]
            self.assertEqual("1.1.1", project["version"])
            self.assertEqual(set(ESP_TARGETS), {target["id"] for target in project["targets"]})
            self.assertTrue(project["extra_hardware"])
            self.assertIn("LCD1602", project["hardware"])
            self.assertIn("Inland", project["hardware"])
            shifter = next(part for part in project["parts"] if part["name"] == "Bidirectional I2C level shifter")
            self.assertTrue(shifter["required"])
            config = configparser.ConfigParser(interpolation=None)
            config.read(ROOT / project["project_dir"] / "platformio.ini")
            for target in project["targets"]:
                flags = config[f"env:{target['environment']}"]["build_flags"]
                self.assertIn("PANEL_LCD_ADDRESS=0x27", flags)

    def test_lcd1602_network_panels_bound_network_and_provisioning_inputs(self):
        root = ROOT / "firmware" / "lcd-panels"
        main = (root / "src" / "main.cpp").read_text()
        logic = (root / "src" / "panel_logic.cpp").read_text()
        http = (root / "src" / "bounded_http.cpp").read_text()
        mqtt = (root / "src" / "bounded_mqtt.cpp").read_text()
        config = (root / "src" / "panel_config.cpp").read_text()
        lcd = (root / "src" / "panel_lcd.cpp").read_text()
        address_scan = (ROOT / "firmware" / "common" / "lcd1602_i2c.h").read_text()
        workflow = (ROOT / ".github" / "workflows" / "pages.yml").read_text()
        for required in (
            "kMaximumResponse = 2048U", "kHttpDeadlineMs = 8000U", "xQueueReceive", "vTaskSuspend", "timestampFresh",
            "length > 256U",
        ):
            self.assertIn(required, main)
        for required in ("total_header > 2048U", "readChunked", "capacity > 2049U", "setCACert(ca_certificate)"):
            self.assertIn(required, http)
        for required in ("remaining_length_ > sizeof(body_)", "response_type != 0x90U", "network_.startDeadline(6000U)", "MSG_DONTWAIT", "packet_active_", "packet_type_ == 0xd0U"):
            self.assertIn(required, mqtt)
        self.assertIn("validExactMqttTopic", logic)
        self.assertNotIn("setInsecure", main)
        self.assertLess(main.index("if (mqtt.connected()) mqtt.loop();"), main.index("const uint32_t mqtt_now = millis();"))
        self.assertNotIn("HTTPClient", main + http)
        self.assertNotIn("unifi_api_key", main + config)
        for required in (
            "WiFi.softAP(ssid, password, 1, false, 1)", "panelParseHttpRequest",
            "Content-Security-Policy", "rejected_pending_marker", "panelStoreConfig(\"pending\"",
            "panelBeginPendingValidation",
        ):
            self.assertIn(required, config)
        self.assertIn("nvs_get_blob", config)
        self.assertIn("ESP_ERR_NVS_NOT_FOUND", config)
        self.assertNotIn("preferences.isKey", config)
        self.assertIn("Wire.setTimeOut", lcd)
        self.assertIn("Wire.endTransmission(true) == 0U", lcd)
        self.assertIn("lcd1602::scanAddresses", lcd)
        self.assertIn("kCandidateAddressCount = 16U", address_scan)
        self.assertIn("0x20U + index", address_scan)
        self.assertIn("0x38U + (index - 8U)", address_scan)
        self.assertNotIn("for (unsigned char address = 1", lcd)
        self.assertIn("pio run -d firmware/lcd-panels", workflow)

    def test_catalog_supports_four_exact_raspberry_pi_pico_boards(self):
        projects = {project["slug"]: project for project in self.load_catalog()}
        for slug in ("pico-board-check", "pico-morse-beacon"):
            targets = {target["id"]: target["chip"] for target in projects[slug]["targets"] if target["id"] in PICO_TARGETS}
            self.assertEqual(PICO_TARGETS, targets, slug)
        surveyor = {target["id"]: target["chip"] for target in projects["pico-wifi-surveyor"]["targets"] if target["id"] in PICO_TARGETS}
        self.assertEqual({
            "raspberry-pi-pico-w": "RP2040",
            "raspberry-pi-pico-2-w": "RP2350",
        }, surveyor)

    def test_pico_surveyor_protects_private_scan_results(self):
        source = (ROOT / "firmware" / "pico-lab" / "src" / "apps" / "wifi-surveyor.cpp").read_text()
        readme = (ROOT / "firmware" / "pico-lab" / "README.md").read_text()
        self.assertIn("get_rand_64()", source)
        self.assertIn("WiFi.softAP(AP_NAME, ap_password)", source)
        self.assertIn("parseSurveyRequest", source)
        self.assertIn("Content-Security-Policy", source)
        self.assertIn("X-Content-Type-Options", source)
        self.assertIn("visible to every client", readme)

    def test_builder_registry_maps_exact_pico_board_ids(self):
        expected = {
            "raspberry-pi-pico": ("RP2040", "rpipico"),
            "raspberry-pi-pico-w": ("RP2040", "rpipicow"),
            "raspberry-pi-pico-2": ("RP2350", "rpipico2"),
            "raspberry-pi-pico-2-w": ("RP2350", "rpipico2w"),
        }
        for target_id, (chip, board) in expected.items():
            target = build_site.SUPPORTED_TARGETS[target_id]
            self.assertEqual(chip, target["chip"])
            self.assertEqual(board, target["board"])
            self.assertEqual("uf2", target["format"])

    def test_catalog_has_practical_and_fun_projects(self):
        categories = {project["category"] for project in self.load_catalog()}
        self.assertEqual({"Practical", "Fun"}, categories)

    def test_catalog_declares_first_boot_setup_for_every_project(self):
        catalog = self.load_catalog()
        setup_required = {
            "ble-mqtt-scanner", "bme280-mqtt-sensor", "ntp-desk-clock", "esp32-diagnostics",
            "mqtt-home-status-panel", "unifi-network-panel", "space-satellite-tracker", "wifi-weather-station",
            "lcd1602-smart-dashboard",
        }
        mqtt_required = {"ble-mqtt-scanner", "bme280-mqtt-sensor", "mqtt-home-status-panel", "lcd1602-smart-dashboard"}
        for project in catalog:
            setup = project["setup"]
            self.assertIsInstance(setup["required"], bool)
            self.assertEqual(project["slug"] in setup_required, setup["required"])
            self.assertTrue(setup["summary"])
            self.assertIsInstance(setup["fields"], list)
            if setup["required"]:
                self.assertIn("Wi-Fi", setup["fields"])
                if project["slug"] in mqtt_required:
                    self.assertIn("MQTT", " ".join(setup["fields"]))

    def test_diagnostics_project_contract(self):
        project = next(project for project in self.load_catalog() if project["slug"] == "esp32-diagnostics")
        self.assertEqual("1.0.0", project["version"])
        self.assertEqual("Board only", project["hardware"])
        self.assertEqual(set(ESP_TARGETS), {target["id"] for target in project["targets"]})
        self.assertEqual(["Wi-Fi"], project["setup"]["fields"])
        source = (ROOT / project["project_dir"] / "src" / "main.cpp").read_text()
        for required in (
            "Preferences", "WiFi.softAP", "kMaximumRequestBytes", "parseBoundedHttpRequest",
            "scanI2c", "Wire.setTimeOut", "application/json", "canvas", "requestAnimationFrame",
            "prefers-reduced-motion", "/api/diagnostics", "/api/i2c/scan", "Cache-Control: no-store",
            "Content-Security-Policy", "X-Content-Type-Options", "esp_reset_reason", "esp_sleep_get_wakeup_cause",
            "ESP.getFreeHeap", "ESP.getMinFreeHeap", "ESP.getFlashChipSize", "WiFi.RSSI", "WiFi.channel",
            "esp_ota_get_running_partition", "AbortController", "aria-live", "role=radiogroup",
            "kI2cScanCooldownMs", "writeAllBounded", "extendDiagnosticMillis",
            "nvs_get_blob", "ESP_ERR_NVS_NOT_FOUND", "if (!server)", "kRecoveryWindowMs", "Hold BOOT now",
        ):
            self.assertIn(required, source)
        self.assertNotIn("setInterval(poll", source)
        self.assertNotIn("https://", source)
        self.assertNotIn("http://", source)

    def test_diagnostics_project_is_built_in_ci(self):
        workflow = (ROOT / ".github" / "workflows" / "pages.yml").read_text()
        self.assertIn("pio run -d firmware/esp32-diagnostics", workflow)

    def test_github_actions_are_pinned_to_commit_shas(self):
        workflow = (ROOT / ".github" / "workflows" / "pages.yml").read_text()
        references = re.findall(r"uses:\s+([^\s#]+)", workflow)
        self.assertTrue(references)
        for reference in references:
            self.assertRegex(reference, r"^[^@]+@[0-9a-f]{40}$", reference)

    def test_extra_hardware_projects_have_parts_and_board_wiring(self):
        diagrams = set()
        for project in self.load_catalog():
            if not project["extra_hardware"]:
                self.assertNotIn("parts", project)
                continue
            self.assertTrue(project["parts"], project["slug"])
            for part in project["parts"]:
                self.assertIsInstance(part["quantity"], int)
                self.assertGreater(part["quantity"], 0)
                self.assertTrue(part["name"])
                self.assertTrue(part["specification"])
                self.assertIsInstance(part["required"], bool)
                self.assertTrue(part["url"].startswith("https://www.amazon.com/"))
            for target in project["targets"]:
                wiring = target["wiring"]
                self.assertTrue(wiring["diagram"].startswith("./wiring/"))
                self.assertTrue(wiring["connections"])
                for connection in wiring["connections"]:
                    self.assertEqual({"from", "to", "wire"}, set(connection))
                    self.assertTrue(all(connection.values()))
                self.assertIsInstance(wiring["warnings"], list)
                self.assertTrue(any("not yet physically verified" in warning for warning in wiring["warnings"]))
                diagram = ROOT / "web" / wiring["diagram"].removeprefix("./")
                self.assertTrue(diagram.is_file(), diagram)
                self.assertGreater(diagram.stat().st_size, 1000)
                svg = diagram.read_text()
                for connection in wiring["connections"]:
                    self.assertIn(connection["from"], svg)
                    self.assertIn(connection["to"], svg)
                for warning in wiring["warnings"]:
                    self.assertIn(warning, svg)
                diagrams.add(diagram)
        self.assertEqual(36, len(diagrams))

    def test_c6_external_wiring_avoids_gpio4_and_gpio5(self):
        for project in self.load_catalog():
            if not project["extra_hardware"]:
                continue
            target = next(target for target in project["targets"] if target["id"] == "esp32-c6-devkitc-1")
            wiring_text = json.dumps(target["wiring"])
            self.assertNotIn("GPIO4", wiring_text)
            self.assertNotIn("GPIO5", wiring_text)
            self.assertIn("GPIO6", wiring_text)

    def test_hc_sr04_wiring_documents_voltage_divider(self):
        project = next(project for project in self.load_catalog() if project["slug"] == "hc-sr04-parking")
        for target in project["targets"]:
            wiring_text = json.dumps(target["wiring"], ensure_ascii=False)
            self.assertIn("1 kΩ", wiring_text)
            self.assertIn("2 kΩ", wiring_text)
            self.assertIn("5 V Echo", wiring_text)

    def test_wiring_gpio_labels_match_compiled_build_flags(self):
        macro_sets = {
            "bme280-mqtt-sensor": ("SENSOR_SDA_PIN", "SENSOR_SCL_PIN"),
            "hc-sr04-parking": ("TRIGGER_PIN", "ECHO_PIN"),
            "pir-occupancy-timer": ("PIR_PIN",),
            "ntp-desk-clock": ("CLOCK_CLK_PIN", "CLOCK_DIO_PIN"),
            "mqtt-home-status-panel": ("PANEL_SDA_PIN", "PANEL_SCL_PIN"),
            "unifi-network-panel": ("PANEL_SDA_PIN", "PANEL_SCL_PIN"),
            "space-satellite-tracker": ("PANEL_SDA_PIN", "PANEL_SCL_PIN"),
            "wifi-weather-station": ("PANEL_SDA_PIN", "PANEL_SCL_PIN"),
        }
        projects = {project["slug"]: project for project in self.load_catalog()}
        for slug, macros in macro_sets.items():
            project = projects[slug]
            config = configparser.ConfigParser(interpolation=None)
            config.read(ROOT / project["project_dir"] / "platformio.ini")
            for target in project["targets"]:
                flags = config[f"env:{target['environment']}"]["build_flags"]
                wiring_text = json.dumps(target["wiring"], ensure_ascii=False)
                for macro in macros:
                    match = re.search(rf"-D\s*{macro}\s*=\s*(\d+)", flags)
                    if match is None:
                        self.fail(f"{slug} {target['id']} missing {macro}")
                    self.assertIn(f"GPIO{match.group(1)}", wiring_text)

    def test_multiboard_scanner_preserves_c6_identity_prefix(self):
        source = (ROOT / "firmware" / "ble-mqtt-scanner" / "src" / "main.cpp").read_text()
        self.assertIn("CONFIG_IDF_TARGET_ESP32C6", source)
        self.assertIn('"esp32c6"', source)
        self.assertIn('"esp32"', source)

    def test_scanner_does_not_retain_online_before_promotion(self):
        source = (ROOT / "firmware" / "ble-mqtt-scanner" / "src" / "main.cpp").read_text()
        validation = source.split("bool validatePendingConfiguration", 1)[1].split("}\n", 1)[0]
        self.assertNotIn('mqtt.publish(status_topic, "online", true)', validation)
        self.assertIn('mqtt.publish(status_topic, "validating", false)', validation)
        self.assertNotIn('status_topic, 0, true, "offline"', validation)

    def test_bme_pending_validation_does_not_retain_before_promotion(self):
        source = (ROOT / "firmware" / "bme280-mqtt-sensor" / "src" / "main.cpp").read_text()
        wake = source.split("void processConfiguredWake", 1)[1].split("}  // namespace", 1)[0]
        self.assertIn("publishReading(reading, value, !pending)", wake)
        self.assertLess(wake.index("promotePending(value)"), wake.index("publishReading(reading, value, true)"))

    def test_bme_mqtt_io_has_a_wake_level_deadline(self):
        source = (ROOT / "firmware" / "bme280-mqtt-sensor" / "src" / "main.cpp").read_text()
        self.assertIn("class DeadlineWiFiClient", source)
        self.assertIn("network.startDeadline(kMqttConnectDeadlineMs)", source)
        self.assertIn("network.startDeadline(kMqttConfirmationTimeoutMs)", source)
        self.assertIn("mqtt.setSocketTimeout(1)", source)
        self.assertIn("if (deadlineExpired())", source)
        self.assertIn("int connect(IPAddress ip, uint16_t port) override", source)
        self.assertIn("size_t write(const uint8_t* buffer, size_t size) override", source)
        self.assertIn("select(socket_fd + 1", source)
        self.assertIn("mqtt.setServer(broker_ip, value.mqtt_port)", source)
        self.assertNotIn("mqtt.setServer(value.mqtt_host", source)

    def test_bme_ipv4_upgrade_break_is_documented(self):
        readme = (ROOT / "firmware" / "bme280-mqtt-sensor" / "README.md").read_text()
        self.assertIn("## Upgrading from 1.2.x", readme)
        self.assertIn("replace any MQTT hostname with its numeric IPv4 address before flashing", readme)
        self.assertIn("existing hostname-based configuration will be rejected", readme)

    def test_ntp_clock_requires_a_fresh_sync_before_promotion(self):
        source = (ROOT / "firmware" / "hardware-lab" / "src" / "apps" / "ntp-desk-clock.cpp").read_text()
        validation = source.split("bool connectAndSynchronize", 1)[1].split("void showUnavailable", 1)[0]
        self.assertIn("fresh_ntp_sync", validation)
        self.assertIn("sntp_set_time_sync_notification_cb", source)
        self.assertIn("std::atomic<bool> fresh_ntp_sync", source)

    def test_ntp_clock_has_optional_lcd1602_i2c_builds_for_every_esp_board(self):
        project = next(project for project in self.load_catalog() if project["slug"] == "ntp-desk-clock")
        self.assertEqual("1.3.0", project["version"])
        shifter = next(part for part in project["parts"] if part["name"] == "Bidirectional I2C level shifter")
        self.assertTrue(shifter["required"])
        jumper = next(part for part in project["parts"] if "jumper" in part["name"].lower())
        self.assertGreaterEqual(jumper["quantity"], 10)
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / project["project_dir"] / "platformio.ini")
        expected_pins = {
            "esp32-devkit-v1": (21, 22),
            "esp32-c3-devkitm-1": (4, 5),
            "esp32-s3-devkitc-1": (8, 9),
            "esp32-c6-devkitc-1": (6, 7),
        }
        for target in project["targets"]:
            variants = target.get("variants", [])
            self.assertEqual(["lcd1602-i2c"], [variant["id"] for variant in variants])
            variant = variants[0]
            self.assertIn("Inland", variant["name"])
            self.assertIn("Inland", variant["hardware"])
            flags = config[f"env:{variant['environment']}"]["build_flags"]
            sda, scl = expected_pins[target["id"]]
            self.assertIn("-DCLOCK_DISPLAY_LCD1602=1", flags)
            self.assertIn(f"-DCLOCK_SDA_PIN={sda}", flags)
            self.assertIn(f"-DCLOCK_SCL_PIN={scl}", flags)
            self.assertIn("-DCLOCK_LCD_ADDRESS=0x27", flags)
            wiring = json.dumps(variant["wiring"], ensure_ascii=False)
            self.assertIn(f"GPIO{sda}", wiring)
            self.assertIn(f"GPIO{scl}", wiring)
            self.assertIn("3.3 V", wiring)
            svg = (ROOT / "web" / variant["wiring"]["diagram"].removeprefix("./")).read_text()
            self.assertIn("LV1 / 3.3 V", svg)
            self.assertIn("HV1 / 5 V", svg)
            document = ET.fromstring(svg)
            view_height = float(document.attrib["viewBox"].split()[3])
            text_nodes = [node for node in document.iter() if node.tag.endswith("text")]
            lv1 = next(node for node in text_nodes if "".join(node.itertext()) == "LV1 / 3.3 V")
            hv1 = next(node for node in text_nodes if "".join(node.itertext()) == "HV1 / 5 V")
            self.assertLess(float(lv1.attrib["x"]) + 7 * len("LV1 / 3.3 V"), float(hv1.attrib["x"]) - 7 * len("HV1 / 5 V"))
            for node in (node for node in text_nodes if node.attrib.get("class") == "warning"):
                warning = "".join(node.itertext())
                self.assertLessEqual(float(node.attrib["x"]) + 7 * len(warning), 1200)
                self.assertLessEqual(float(node.attrib["y"]), view_height)

        source = (ROOT / project["project_dir"] / "src" / "apps" / "ntp-desk-clock.cpp").read_text()
        self.assertIn("CLOCK_DISPLAY_LCD1602", source)
        self.assertIn("class CheckedLcd1602", source)
        self.assertIn("Wire.begin(CLOCK_SDA_PIN, CLOCK_SCL_PIN)", source)
        self.assertIn("Wire.setTimeOut", source)
        self.assertIn("lcd1602::scanAddresses", source)
        self.assertIn("Wire.beginTransmission(address_)", source)
        self.assertIn("Wire.endTransmission(true) == 0U", source)
        self.assertIn("const uint32_t remaining = timeout_ms - elapsed", source)
        self.assertIn("Wire.setTimeOut(remaining < 25U ? remaining : 25U)", source)

    def test_catalog_rejects_unknown_project_and_target_fields(self):
        catalog = self.load_catalog()
        catalog[0]["private_token"] = "must-not-publish"
        with self.assertRaisesRegex(SystemExit, "unknown=.*private_token"):
            build_site.validate_catalog(catalog, ROOT)

        catalog = self.load_catalog()
        catalog[0]["targets"][0]["private_token"] = "must-not-publish"
        with self.assertRaisesRegex(SystemExit, "target fields are invalid"):
            build_site.validate_catalog(catalog, ROOT)

        catalog = self.load_catalog()
        catalog[0]["setup"]["private_token"] = "must-not-publish"
        with self.assertRaisesRegex(SystemExit, "setup metadata is invalid"):
            build_site.validate_catalog(catalog, ROOT)

    def test_catalog_rejects_invalid_optional_target_metadata(self):
        catalog = self.load_catalog()
        catalog[0]["targets"][0]["configuration_name"] = {"private": "must-not-publish"}
        with self.assertRaisesRegex(SystemExit, "target configuration name must be non-empty"):
            build_site.validate_catalog(catalog, ROOT)

        catalog = self.load_catalog()
        board_only = next(project for project in catalog if project["hardware"] == "Board only")
        board_only["targets"][0]["wiring"] = {"private": "must-not-publish"}
        with self.assertRaisesRegex(SystemExit, "board-only target cannot declare wiring"):
            build_site.validate_catalog(catalog, ROOT)

    def test_catalog_rejects_board_only_hardware_variants(self):
        catalog = self.load_catalog()
        board_only = next(project for project in catalog if project["hardware"] == "Board only" and project["targets"][0]["id"].startswith("esp32"))
        ntp = next(project for project in catalog if project["slug"] == "ntp-desk-clock")
        board_only["targets"][0]["variants"] = [ntp["targets"][0]["variants"][0]]
        with self.assertRaisesRegex(SystemExit, "board-only project cannot declare hardware variants"):
            build_site.validate_catalog(catalog, ROOT)

    def test_catalog_rejects_reused_wiring_diagram_paths(self):
        catalog = self.load_catalog()
        ntp = next(project for project in catalog if project["slug"] == "ntp-desk-clock")
        ntp["targets"][0]["variants"][0]["wiring"]["diagram"] = ntp["targets"][0]["wiring"]["diagram"]
        with self.assertRaisesRegex(SystemExit, "target wiring diagram is reused"):
            build_site.validate_catalog(catalog, ROOT)

    def test_local_portal_labels_the_actual_chip(self):
        source = (ROOT / "firmware" / "no-hardware-lab" / "src" / "app_support.cpp").read_text()
        self.assertNotIn("ESP32-C6 · LOCAL ONLY", source)
        self.assertIn("ESP.getChipModel()", source)

    def test_bme280_recovery_precedes_pending_processing(self):
        source = (ROOT / "firmware" / "bme280-mqtt-sensor" / "src" / "main.cpp").read_text()
        setup = source.split("void setup()", 1)[1].split("void loop()", 1)[0]
        self.assertLess(setup.index("recoveryRequested()"), setup.index("processConfiguredWake(pending"))

    def test_bme280_setup_preflights_i2c_and_spi_before_credentials(self):
        project = next(project for project in self.load_catalog() if project["slug"] == "bme280-mqtt-sensor")
        self.assertEqual("1.3.0", project["version"])
        self.assertIn("I²C/SPI sensor preflight", project["features"])
        source = (ROOT / project["project_dir"] / "src" / "main.cpp").read_text()
        page = source.split("String setupPage", 1)[1].split("void sendHttp", 1)[0]
        self.assertIn("Sensor preflight", source)
        self.assertLess(page.index("appendScanReport(page)"), page.index("Wi-Fi SSID"))
        self.assertIn('std::strcmp(path, "/scan")', source)
        self.assertIn("scanSensorBuses()", source)
        self.assertIn("for (uint8_t address = 1U; address < 127U", source)
        self.assertIn("SPI_MODE0", source)
        self.assertIn("SPI_MODE3", source)

    def test_bme280_targets_define_exact_spi_fallback_pins(self):
        project = next(project for project in self.load_catalog() if project["slug"] == "bme280-mqtt-sensor")
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / project["project_dir"] / "platformio.ini")
        for target in project["targets"]:
            flags = config[f"env:{target['environment']}"]["build_flags"]
            for macro in ("SENSOR_SPI_SCK_PIN", "SENSOR_SPI_MISO_PIN", "SENSOR_SPI_MOSI_PIN", "SENSOR_SPI_CS_PIN"):
                self.assertRegex(flags, rf"-D\s*{macro}\s*=\s*\d+", f"{target['id']} missing {macro}")

    def test_bme280_detection_and_driver_initialization_are_bounded_and_consistent(self):
        source = (ROOT / "firmware" / "bme280-mqtt-sensor" / "src" / "main.cpp").read_text()
        scan = source.split("SensorScanReport scanSensorBuses()", 1)[1].split("void appendScanReport", 1)[0]
        self.assertLess(scan.index("digitalWrite(SENSOR_SPI_CS_PIN, HIGH)"), scan.index("scanI2cClock(report"))
        self.assertIn("spi_mode3_only_bme", scan)
        self.assertNotIn("report.bme_spi_mode = 3U", scan)
        bounded = source.split("Reading readSensorBounded()", 1)[1].split("bool connectWifi", 1)[0]
        self.assertIn("xQueueReceive", bounded)
        self.assertIn("kSensorReadTimeoutMs", bounded)
        self.assertIn("vTaskDelete", bounded)

    def test_protected_local_aps_use_short_unambiguous_passwords(self):
        files = (
            ROOT / "firmware" / "ble-mqtt-scanner" / "src" / "scanner_runtime_config.cpp",
            ROOT / "firmware" / "bme280-mqtt-sensor" / "src" / "main.cpp",
            ROOT / "firmware" / "hardware-lab" / "src" / "ntp_clock_config.cpp",
            ROOT / "firmware" / "pico-lab" / "src" / "apps" / "wifi-surveyor.cpp",
            ROOT / "firmware" / "esp32-diagnostics" / "src" / "main.cpp",
        )
        expected_alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
        for path in files:
            source = path.read_text()
            self.assertIn(expected_alphabet, source, path)
            self.assertIn("kSetupPasswordCharacters = 8U", source, path)
            self.assertIn("makeReadableSetupPassword", source, path)
            self.assertIn("kSetupPasswordCharacters + 1U", source, path)
            self.assertIn("& 31U", source, path)
            self.assertIn("[kSetupPasswordCharacters] = '\\0'", source, path)
        esp_functions = (
            (files[0].read_text(), "bool scannerStartProvisioning()"),
            (files[1].read_text(), "void startProvisioning()"),
            (files[2].read_text(), "bool clockStartProvisioning()"),
            (files[4].read_text(), "bool startProvisioning()"),
        )
        for source, signature in esp_functions:
            provisioning = source.split(signature, 1)[1].split("}\n", 1)[0]
            self.assertLess(provisioning.index("WiFi.mode(WIFI_AP)"), provisioning.index("makeReadableSetupPassword"))
            self.assertLess(provisioning.index("WiFi.mode(WIFI_AP)"), provisioning.index("csrf_token"))
            self.assertIn("WiFi.softAP(ssid, password, 1, false, 1)", provisioning)
        pico_source = files[3].read_text()
        self.assertIn('#include "pico/rand.h"', pico_source)
        self.assertIn("get_rand_64()", pico_source)
        self.assertNotIn("rp2040.hwrand32()", pico_source)
        projects = {project["slug"]: project for project in self.load_catalog()}
        self.assertEqual("3.1.0", projects["ble-mqtt-scanner"]["version"])
        self.assertEqual("1.3.0", projects["bme280-mqtt-sensor"]["version"])
        self.assertEqual("1.3.0", projects["ntp-desk-clock"]["version"])
        self.assertEqual("1.1.0", projects["pico-wifi-surveyor"]["version"])

    def test_reboot_museum_clears_nvs_with_checked_clear(self):
        source = (ROOT / "firmware" / "no-hardware-lab" / "src" / "apps" / "boot-counter.cpp").read_text()
        clear_handler = source.split("void clearHistory()", 1)[1].split("}\n", 1)[0]
        self.assertIn("preferences.clear()", clear_handler)
        self.assertIn("if (!persistence_ready || !preferences.clear())", clear_handler)

    def test_file_drop_blocks_all_file_operations_when_unavailable(self):
        source = (ROOT / "firmware" / "no-hardware-lab" / "src" / "apps" / "littlefs-dropbox.cpp").read_text()
        download_handler = source.split("void downloadFile()", 1)[1].split("}\n", 1)[0]
        delete_handler = source.split("void deleteFile()", 1)[1].split("}\n", 1)[0]
        mount_handler = source.split("void mountFilesystem()", 1)[1].split("}  // namespace", 1)[0]
        self.assertIn("if (!filesystem_ready)", download_handler)
        self.assertIn("if (!filesystem_ready)", delete_handler)
        self.assertIn("LittleFS.end()", mount_handler)

    def test_portal_renders_catalog_and_selected_manifest(self):
        html = (ROOT / "web" / "index.html").read_text()
        javascript = (ROOT / "web" / "app.js").read_text()
        self.assertIn('id="project-list"', html)
        self.assertIn('id="board-select"', html)
        self.assertIn('id="variant-select"', html)
        self.assertIn('id="selected-hardware"', html)
        self.assertIn('id="selected-setup"', html)
        self.assertIn('id="selected-setup-fields"', html)
        self.assertIn('id="selected-wiring"', html)
        self.assertIn('selectedTarget.variants', javascript)
        self.assertIn('project.setup.fields.map', javascript)
        self.assertIn('setupField.textContent = field', javascript)
        self.assertIn('id="selected-connections"', html)
        self.assertIn('id="selected-warnings"', html)
        self.assertIn('id="selected-parts"', html)
        self.assertIn('id="pico-install"', html)
        self.assertIn('id="pico-download"', html)
        self.assertIn('id="pico-size"', html)
        self.assertIn('id="pico-sha"', html)
        self.assertIn('class="filters" role="group"', html)
        self.assertIn("esp-web-install-button", html)
        self.assertIn('slot="activate"', html)
        self.assertIn('slot="unsupported"', html)
        self.assertIn('fetch("./projects.json")', javascript)
        self.assertIn("selectedBuild.manifest", javascript)
        self.assertIn('selectedBuild.method === "uf2"', javascript)
        self.assertIn("selectedBuild.download", javascript)
        self.assertIn("selectedBuild.size", javascript)
        self.assertIn("selectedBuild.sha256", javascript)
        self.assertIn("project.hardware", javascript)
        self.assertIn("project.setup.summary", javascript)
        self.assertIn("selectedBuild.wiring.diagram", javascript)
        self.assertIn("selectedBuild.wiring.connections", javascript)
        self.assertIn("selectedBuild.wiring.warnings", javascript)
        self.assertIn("project.parts", javascript)
        self.assertIn('setAttribute("manifest", selectedBuild.manifest)', javascript)
        self.assertIn('setAttribute("aria-pressed"', javascript)
        self.assertNotIn('id="installer-button" manifest=', html)

    def test_site_builder_packages_pico_uf2_download(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            project_dir = temp / "firmware"
            build = project_dir / ".pio" / "build" / "board-check--pico"
            build.mkdir(parents=True)
            (project_dir / "platformio.ini").write_text("[env:board-check--pico]\nboard = rpipico\n")
            uf2 = make_uf2(0xE48BFF56)
            (build / "firmware.uf2").write_bytes(uf2)
            web = temp / "web"
            web.mkdir()
            (web / "index.html").write_text("<!doctype html><title>Pico</title>")
            project = {
                "slug": "pico-check",
                "name": "Pico Check",
                "version": "1.0.0",
                "category": "Practical",
                "description": "Test",
                "hardware": "Board only",
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
                "setup": {"required": False, "fields": [], "summary": "No setup."},
                "project_dir": "firmware",
                "targets": [{
                    "id": "raspberry-pi-pico",
                    "name": "Raspberry Pi Pico",
                    "chip": "RP2040",
                    "environment": "board-check--pico",
                }],
            }
            catalog = temp / "projects.json"
            catalog.write_text(json.dumps([project]))
            output = temp / "site"
            subprocess.run([
                "python3", str(ROOT / "scripts" / "build_site.py"),
                "--catalog", str(catalog), "--output", str(output),
            ], check=True)
            public = json.loads((output / "projects.json").read_text())[0]["targets"][0]
            self.assertEqual("uf2", public["method"])
            self.assertEqual("./firmware/pico-check/raspberry-pi-pico/firmware.uf2", public["download"])
            self.assertEqual(len(uf2), public["size"])
            release = output / "firmware" / "pico-check" / "raspberry-pi-pico"
            self.assertEqual(uf2, (release / "firmware.uf2").read_bytes())
            manifest = json.loads((release / "uf2-manifest.json").read_text())
            self.assertEqual(hashlib.sha256(uf2).hexdigest(), manifest["sha256"])
            self.assertEqual(len(uf2), manifest["size"])

    def test_uf2_validator_rejects_wrong_chip_family(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "firmware.uf2"
            image.write_bytes(make_uf2(0xE48BFF56))
            with self.assertRaises(SystemExit):
                build_site.validate_uf2_image(image, "RP2350")

    def test_uf2_validator_rejects_malformed_block(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "firmware.uf2"
            image.write_bytes(bytes(512))
            with self.assertRaises(SystemExit):
                build_site.validate_uf2_image(image, "RP2040")

    def test_safety_warning_color_meets_wcag_aa(self):
        css = (ROOT / "web" / "styles.css").read_text()
        match = re.search(r"\.warning-list\{color:(#[0-9a-fA-F]{6})\}", css)
        if match is None:
            self.fail("warning-list color is missing")

        def luminance(color):
            channels = [int(color[index:index + 2], 16) / 255 for index in (1, 3, 5)]
            linear = [value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4 for value in channels]
            return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]

        foreground = luminance(match.group(1))
        background = luminance("#172127")
        contrast = (max(foreground, background) + 0.05) / (min(foreground, background) + 0.05)
        self.assertGreaterEqual(contrast, 4.5)

    def test_amazon_search_url_rejects_tracking_and_invalid_hosts(self):
        self.assertTrue(build_site.is_allowed_amazon_search("https://www.amazon.com/s?k=BME280+sensor"))
        self.assertFalse(build_site.is_allowed_amazon_search("https://www.amazon.com/s?k=BME280&tag=affiliate-20"))
        self.assertFalse(build_site.is_allowed_amazon_search("https://www.amazon.com.evil.example/s?k=BME280"))
        self.assertFalse(build_site.is_allowed_amazon_search("https://www.amazon.com/s?k="))

    def test_firmware_artifact_validator_rejects_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            outside = root / "outside.bin"
            outside.write_bytes(b"firmware")
            image = build / "firmware.bin"
            image.symlink_to(outside)
            with self.assertRaises(SystemExit):
                build_site.validate_firmware_artifact(image, build)

    def test_output_validator_rejects_symlinked_ancestor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            docs = root / "docs"
            existing = docs / "site"
            existing.mkdir(parents=True)
            marker = existing / "KEEP"
            marker.write_text("preserve")
            (root / "alias").symlink_to(docs, target_is_directory=True)
            catalog = root / "projects.json"
            catalog.write_text("[]")
            with self.assertRaises(SystemExit):
                build_site.validate_output(root / "alias" / "site", root, catalog, [])
            self.assertEqual("preserve", marker.read_text())

    def test_static_web_tree_rejects_non_wiring_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            web = root / "web"
            web.mkdir()
            outside = root / "private.txt"
            outside.write_text("not public")
            (web / "favicon.svg").symlink_to(outside)
            with self.assertRaises(SystemExit):
                build_site.validate_static_web_tree(web, root)

    def test_optional_breadboard_projects_include_direct_connector_path(self):
        projects = {project["slug"]: project for project in self.load_catalog()}
        for slug in ("bme280-mqtt-sensor", "ntp-desk-clock"):
            required = [part for part in projects[slug]["parts"] if part["required"]]
            self.assertTrue(any("Female-to-female" in part["name"] for part in required), slug)
            self.assertTrue(any("male header" in part["specification"] for part in required), slug)

    def test_wiring_reproducibility_gate_detects_untracked_assets(self):
        workflow = (ROOT / ".github" / "workflows" / "pages.yml").read_text()
        generator = (ROOT / "scripts" / "generate_wiring_diagrams.py").read_text()
        self.assertIn("git status --porcelain -- web/wiring", workflow)
        self.assertIn("unexpected.unlink()", generator)

    def test_manifest_generator_uses_factory_image_at_offset_zero(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "manifest.json"
            subprocess.run(
                [
                    "python3",
                    str(ROOT / "scripts" / "generate_web_manifest.py"),
                    "--name",
                    "Test Project",
                    "--chip",
                    "ESP32-C6",
                    "--version",
                    "1.2.3",
                    "--output",
                    str(output),
                ],
                check=True,
            )
            manifest = json.loads(output.read_text())
            self.assertEqual("Test Project", manifest["name"])
            self.assertEqual("1.2.3", manifest["version"])
            self.assertEqual("ESP32-C6", manifest["builds"][0]["chipFamily"])
            self.assertEqual(0, manifest["builds"][0]["parts"][0]["offset"])
            self.assertEqual("firmware.factory.bin", manifest["builds"][0]["parts"][0]["path"])

    def test_site_builder_packages_every_catalog_project(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            catalog = []
            for slug in ("alpha", "beta"):
                project_dir = temp / slug
                environment = f"{slug}--esp32-c3-devkitm-1"
                (project_dir / "platformio.ini").parent.mkdir(parents=True)
                (project_dir / "platformio.ini").write_text(f"[env:{environment}]\nboard = esp32-c3-devkitm-1\n")
                build = project_dir / ".pio" / "build" / environment
                build.mkdir(parents=True)
                (build / "firmware.factory.bin").write_bytes(f"factory-{slug}".encode())
                (build / "firmware.bin").write_bytes(f"ota-{slug}".encode())
                catalog.append(
                    {
                        "slug": slug,
                        "name": slug.title(),
                        "version": "2.0.0",
                        "category": "Fun",
                        "description": "Test",
                        "hardware": "Board only",
                        "features": ["Test"],
                        "installable": True,
                        "extra_hardware": False,
                        "setup": {"required": False, "fields": [], "summary": "No setup."},
                        "project_dir": slug,
                        "targets": [{
                            "id": "esp32-c3-devkitm-1",
                            "name": "ESP32-C3-DevKitM-1",
                            "chip": "ESP32-C3",
                            "environment": environment,
                        }],
                    }
                )
            catalog_path = temp / "projects.json"
            catalog_path.write_text(json.dumps(catalog))
            web = temp / "web"
            web.mkdir()
            (web / "index.html").write_text("<!doctype html><title>Custom portal</title>")
            output = temp / "site"

            subprocess.run(
                [
                    "python3",
                    str(ROOT / "scripts" / "build_site.py"),
                    "--catalog",
                    str(catalog_path),
                    "--output",
                    str(output),
                ],
                check=True,
            )

            public_catalog = json.loads((output / "projects.json").read_text())
            self.assertIn("Custom portal", (output / "index.html").read_text())
            self.assertEqual(["alpha", "beta"], [project["slug"] for project in public_catalog])
            self.assertNotIn("project_dir", public_catalog[0])
            self.assertNotIn("environment", public_catalog[0]["targets"][0])
            for slug in ("alpha", "beta"):
                release = output / "firmware" / slug / "esp32-c3-devkitm-1"
                ota = f"ota-{slug}".encode()
                self.assertTrue((release / "firmware.factory.bin").is_file())
                self.assertEqual(ota, (release / "firmware.bin").read_bytes())
                manifest = json.loads((release / "manifest.json").read_text())
                self.assertEqual(f"{slug.title()} — ESP32-C3-DevKitM-1", manifest["name"])
                self.assertEqual("ESP32-C3", manifest["builds"][0]["chipFamily"])
                ota_manifest = json.loads((release / "ota-manifest.json").read_text())
                self.assertEqual(len(ota), ota_manifest["size"])
                self.assertEqual(hashlib.sha256(ota).hexdigest(), ota_manifest["sha256"])

    def test_site_builder_rejects_catalog_path_traversal(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            catalog = [
                {
                    "slug": "../../escape",
                    "name": "Escape",
                    "version": "1.0.0",
                    "category": "Fun",
                    "description": "Test",
                    "hardware": "Board only",
                    "features": ["Test"],
                    "installable": True,
                    "extra_hardware": False,
                        "setup": {"required": False, "fields": [], "summary": "No setup."},
                    "project_dir": "firmware",
                    "targets": [{"id": "esp32-c6-devkitc-1", "name": "C6", "chip": "ESP32-C6", "environment": "escape"}],
                }
            ]
            catalog_path = temp / "projects.json"
            catalog_path.write_text(json.dumps(catalog))
            result = subprocess.run(
                ["python3", str(ROOT / "scripts" / "build_site.py"), "--catalog", str(catalog_path), "--output", str(temp / "site")],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertIn("invalid slug", result.stderr + result.stdout)
            self.assertFalse((temp.parent / "escape").exists())

    def test_site_builder_rejects_output_root(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            project = {
                "slug": "same",
                "name": "Same",
                "version": "1.0.0",
                "category": "Fun",
                "description": "Test",
                "hardware": "Board only",
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
                        "setup": {"required": False, "fields": [], "summary": "No setup."},
                "project_dir": "firmware",
                "targets": [{"id": "esp32-c6-devkitc-1", "name": "C6", "chip": "ESP32-C6", "environment": "same"}],
            }
            catalog_path = temp / "projects.json"
            catalog_path.write_text(json.dumps([project]))
            result = subprocess.run(
                ["python3", str(ROOT / "scripts" / "build_site.py"), "--catalog", str(catalog_path), "--output", str(temp)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertIn("output directory", result.stderr + result.stdout)
            self.assertTrue(catalog_path.exists())

    def test_site_builder_rejects_duplicate_slugs(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            project = {
                "slug": "same",
                "name": "Same",
                "version": "1.0.0",
                "category": "Fun",
                "description": "Test",
                "hardware": "Board only",
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
                        "setup": {"required": False, "fields": [], "summary": "No setup."},
                "project_dir": "firmware",
                "targets": [{"id": "esp32-c6-devkitc-1", "name": "C6", "chip": "ESP32-C6", "environment": "same"}],
            }
            catalog_path = temp / "projects.json"
            catalog_path.write_text(json.dumps([project, project]))
            result = subprocess.run(
                ["python3", str(ROOT / "scripts" / "build_site.py"), "--catalog", str(catalog_path), "--output", str(temp / "site")],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertIn("duplicate slug", result.stderr + result.stdout)

    def test_site_builder_rejects_reused_target_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            project = {
                "slug": "same",
                "name": "Same",
                "version": "1.0.0",
                "category": "Fun",
                "description": "Test",
                "hardware": "Board only",
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
                        "setup": {"required": False, "fields": [], "summary": "No setup."},
                "project_dir": "firmware",
                "targets": [
                    {"id": "esp32-devkit-v1", "name": "ESP32", "chip": "ESP32", "environment": "same"},
                    {"id": "esp32-c3-devkitm-1", "name": "C3", "chip": "ESP32-C3", "environment": "same"},
                ],
            }
            catalog_path = temp / "projects.json"
            catalog_path.write_text(json.dumps([project]))
            result = subprocess.run(
                ["python3", str(ROOT / "scripts" / "build_site.py"), "--catalog", str(catalog_path), "--output", str(temp / "site")],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertIn("duplicate target environment", result.stderr + result.stdout)

    def test_site_builder_rejects_target_chip_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            project = {
                "slug": "mismatch",
                "name": "Mismatch",
                "version": "1.0.0",
                "category": "Fun",
                "description": "Test",
                "hardware": "Board only",
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
                        "setup": {"required": False, "fields": [], "summary": "No setup."},
                "project_dir": "firmware",
                "targets": [{
                    "id": "esp32-c6-devkitc-1",
                    "name": "Wrong chip",
                    "chip": "ESP32-S3",
                    "environment": "mismatch",
                }],
            }
            catalog_path = temp / "projects.json"
            catalog_path.write_text(json.dumps([project]))
            result = subprocess.run(
                ["python3", str(ROOT / "scripts" / "build_site.py"), "--catalog", str(catalog_path), "--output", str(temp / "site")],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertIn("chip does not match target", result.stderr + result.stdout)

    def test_site_builder_rejects_contradictory_hardware_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            project = {
                "slug": "sensor",
                "name": "Sensor",
                "version": "1.0.0",
                "category": "Practical",
                "description": "Test",
                "hardware": "BME280 required",
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
                        "setup": {"required": False, "fields": [], "summary": "No setup."},
                "project_dir": "firmware",
                "targets": [{"id": "esp32-c6-devkitc-1", "name": "C6", "chip": "ESP32-C6", "environment": "sensor"}],
            }
            catalog_path = temp / "projects.json"
            catalog_path.write_text(json.dumps([project]))
            result = subprocess.run(
                ["python3", str(ROOT / "scripts" / "build_site.py"), "--catalog", str(catalog_path), "--output", str(temp / "site")],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertIn("hardware metadata contradicts", result.stderr + result.stdout)

    def test_site_builder_rejects_environment_board_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            project_dir = temp / "firmware"
            project_dir.mkdir()
            (project_dir / "platformio.ini").write_text("[env:mismatch]\nboard = esp32-s3-devkitc-1\n")
            project = {
                "slug": "mismatch",
                "name": "Mismatch",
                "version": "1.0.0",
                "category": "Fun",
                "description": "Test",
                "hardware": "Board only",
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
                        "setup": {"required": False, "fields": [], "summary": "No setup."},
                "project_dir": "firmware",
                "targets": [{
                    "id": "esp32-c6-devkitc-1",
                    "name": "C6",
                    "chip": "ESP32-C6",
                    "environment": "mismatch",
                }],
            }
            catalog_path = temp / "projects.json"
            catalog_path.write_text(json.dumps([project]))
            result = subprocess.run(
                ["python3", str(ROOT / "scripts" / "build_site.py"), "--catalog", str(catalog_path), "--output", str(temp / "site")],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertIn("environment board does not match target", result.stderr + result.stdout)

    def test_site_builder_rejects_wiring_asset_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            wiring_dir = temp / "web" / "wiring" / "sensor"
            wiring_dir.mkdir(parents=True)
            (wiring_dir / "real.svg").write_text("<svg xmlns=\"http://www.w3.org/2000/svg\"/>")
            (wiring_dir / "esp32-c6-devkitc-1.svg").symlink_to("real.svg")
            project = {
                "slug": "sensor",
                "name": "Sensor",
                "version": "1.0.0",
                "category": "Practical",
                "description": "Test",
                "hardware": "Sensor required",
                "features": ["Test"],
                "installable": True,
                "extra_hardware": True,
                "setup": {"required": False, "fields": [], "summary": "No setup."},
                "project_dir": "firmware",
                "parts": [{
                    "quantity": 1,
                    "name": "Sensor",
                    "specification": "3.3 V",
                    "required": True,
                    "url": "https://www.amazon.com/s?k=sensor",
                }],
                "targets": [{
                    "id": "esp32-c6-devkitc-1",
                    "name": "C6",
                    "chip": "ESP32-C6",
                    "environment": "sensor",
                    "wiring": {
                        "diagram": "./wiring/sensor/esp32-c6-devkitc-1.svg",
                        "connections": [{"from": "Sensor OUT", "to": "GPIO6", "wire": "green"}],
                        "warnings": ["Not physically verified."],
                    },
                }],
            }
            catalog_path = temp / "projects.json"
            catalog_path.write_text(json.dumps([project]))
            result = subprocess.run(
                ["python3", str(ROOT / "scripts" / "build_site.py"), "--catalog", str(catalog_path), "--output", str(temp / "site")],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertIn("symlink", result.stderr + result.stdout)

    def test_site_builder_rejects_shared_artifact_source(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            base = {
                "version": "1.0.0",
                "category": "Fun",
                "description": "Test",
                "hardware": "Board only",
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
                        "setup": {"required": False, "fields": [], "summary": "No setup."},
                "project_dir": "firmware",
                "targets": [{"id": "esp32-c6-devkitc-1", "name": "C6", "chip": "ESP32-C6", "environment": "shared"}],
            }
            catalog = [
                {**base, "slug": "first", "name": "First"},
                {**base, "slug": "second", "name": "Second"},
            ]
            catalog_path = temp / "projects.json"
            catalog_path.write_text(json.dumps(catalog))
            result = subprocess.run(
                ["python3", str(ROOT / "scripts" / "build_site.py"), "--catalog", str(catalog_path), "--output", str(temp / "site")],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(0, result.returncode)
            self.assertIn("artifact source reused", result.stderr + result.stdout)

    def test_pages_permissions_are_scoped_to_deployment(self):
        workflow = (ROOT / ".github" / "workflows" / "pages.yml").read_text()
        global_permissions = workflow.split("permissions:", 1)[1].split("concurrency:", 1)[0]
        deploy_job = workflow.split("  deploy:", 1)[1]
        self.assertNotIn("pages: write", global_permissions)
        self.assertNotIn("id-token: write", global_permissions)
        self.assertIn("permissions:\n      pages: write\n      id-token: write", deploy_job)
        self.assertIn("firmware/no-hardware-lab/test/native/test_main.cpp", workflow)
        self.assertIn("pio run -d firmware/no-hardware-lab", workflow)
        self.assertIn("bme280-mqtt-sensor/test/native/test_main.cpp", workflow)
        self.assertIn("pio run -d firmware/bme280-mqtt-sensor", workflow)
        self.assertIn("python scripts/build_site.py --output _site", workflow)


if __name__ == "__main__":
    unittest.main()
