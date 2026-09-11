import configparser
import hashlib
import json
import re
import struct
import subprocess
import tempfile
import unittest
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
EXTRA_HARDWARE_PROJECTS = {"bme280-mqtt-sensor", "hc-sr04-parking", "pir-occupancy-timer", "ntp-desk-clock"}


class FirmwarePortalTests(unittest.TestCase):
    def load_catalog(self):
        return json.loads((ROOT / "projects.json").read_text())

    def test_catalog_contains_twenty_two_projects(self):
        catalog = self.load_catalog()
        slugs = {project["slug"] for project in catalog}
        self.assertEqual(EXPECTED_NEW_PROJECTS, slugs - {"ble-mqtt-scanner"})
        self.assertEqual(22, len(catalog))
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
        self.assertEqual(83, sum(len(project["targets"]) for project in catalog))

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
        self.assertIn("rp2040.hwrand32()", source)
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
        setup_required = {"ble-mqtt-scanner", "bme280-mqtt-sensor", "ntp-desk-clock"}
        for project in catalog:
            setup = project["setup"]
            self.assertIsInstance(setup["required"], bool)
            self.assertEqual(project["slug"] in setup_required, setup["required"])
            self.assertTrue(setup["summary"])
            self.assertIsInstance(setup["fields"], list)
            if setup["required"]:
                self.assertIn("Wi-Fi", setup["fields"])
                if project["slug"] != "ntp-desk-clock":
                    self.assertIn("MQTT", setup["fields"])

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
        self.assertEqual(16, len(diagrams))

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

    def test_ntp_clock_requires_a_fresh_sync_before_promotion(self):
        source = (ROOT / "firmware" / "hardware-lab" / "src" / "apps" / "ntp-desk-clock.cpp").read_text()
        validation = source.split("bool connectAndSynchronize", 1)[1].split("void showUnavailable", 1)[0]
        self.assertIn("fresh_ntp_sync", validation)
        self.assertIn("sntp_set_time_sync_notification_cb", source)
        self.assertIn("std::atomic<bool> fresh_ntp_sync", source)

    def test_local_portal_labels_the_actual_chip(self):
        source = (ROOT / "firmware" / "no-hardware-lab" / "src" / "app_support.cpp").read_text()
        self.assertNotIn("ESP32-C6 · LOCAL ONLY", source)
        self.assertIn("ESP.getChipModel()", source)

    def test_bme280_recovery_precedes_pending_processing(self):
        source = (ROOT / "firmware" / "bme280-mqtt-sensor" / "src" / "main.cpp").read_text()
        setup = source.split("void setup()", 1)[1].split("void loop()", 1)[0]
        self.assertLess(setup.index("recoveryRequested()"), setup.index("processConfiguredWake(pending"))

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
        self.assertIn('id="selected-hardware"', html)
        self.assertIn('id="selected-setup"', html)
        self.assertIn('id="selected-wiring"', html)
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
        self.assertIn("selectedTarget.manifest", javascript)
        self.assertIn('selectedTarget.method === "uf2"', javascript)
        self.assertIn("selectedTarget.download", javascript)
        self.assertIn("selectedTarget.size", javascript)
        self.assertIn("selectedTarget.sha256", javascript)
        self.assertIn("project.hardware", javascript)
        self.assertIn("project.setup.summary", javascript)
        self.assertIn("selectedTarget.wiring.diagram", javascript)
        self.assertIn("selectedTarget.wiring.connections", javascript)
        self.assertIn("selectedTarget.wiring.warnings", javascript)
        self.assertIn("project.parts", javascript)
        self.assertIn('setAttribute("manifest", selectedTarget.manifest)', javascript)
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
