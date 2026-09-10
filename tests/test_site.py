import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class FirmwarePortalTests(unittest.TestCase):
    def test_portal_embeds_esp_web_tools_with_relative_manifest(self):
        html = (ROOT / "web" / "index.html").read_text()
        self.assertIn("esp-web-install-button", html)
        self.assertIn('manifest="./firmware/ble-mqtt-scanner/manifest.json"', html)
        self.assertIn('slot="activate"', html)
        self.assertIn('slot="unsupported"', html)

    def test_manifest_generator_uses_factory_image_at_offset_zero(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "manifest.json"
            subprocess.run(
                [
                    "python3",
                    str(ROOT / "scripts" / "generate_web_manifest.py"),
                    "--version",
                    "1.2.3",
                    "--output",
                    str(output),
                ],
                check=True,
            )
            manifest = json.loads(output.read_text())
            self.assertEqual("DS9 BLE MQTT Scanner", manifest["name"])
            self.assertEqual("1.2.3", manifest["version"])
            self.assertEqual("ESP32-C6", manifest["builds"][0]["chipFamily"])
            self.assertEqual(0, manifest["builds"][0]["parts"][0]["offset"])
            self.assertEqual("firmware.factory.bin", manifest["builds"][0]["parts"][0]["path"])

    def test_project_catalog_describes_ble_scanner(self):
        catalog = json.loads((ROOT / "web" / "projects.json").read_text())
        scanner = next(project for project in catalog if project["slug"] == "ble-mqtt-scanner")
        self.assertEqual("ESP32-C6", scanner["chip"])
        self.assertTrue(scanner["installable"])

    def test_pages_permissions_are_scoped_to_deployment(self):
        workflow = (ROOT / ".github" / "workflows" / "pages.yml").read_text()
        global_permissions = workflow.split("permissions:", 1)[1].split("concurrency:", 1)[0]
        deploy_job = workflow.split("  deploy:", 1)[1]

        self.assertNotIn("pages: write", global_permissions)
        self.assertNotIn("id-token: write", global_permissions)
        self.assertIn("permissions:\n      pages: write\n      id-token: write", deploy_job)
        self.assertIn("actions/configure-pages@v6", deploy_job)

    def test_site_builder_packages_both_factory_and_ota_images(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            firmware_build = temp / "build"
            firmware_build.mkdir()
            (firmware_build / "firmware.factory.bin").write_bytes(b"factory")
            (firmware_build / "firmware.bin").write_bytes(b"ota")
            output = temp / "site"

            subprocess.run(
                [
                    "python3",
                    str(ROOT / "scripts" / "build_site.py"),
                    "--firmware-build",
                    str(firmware_build),
                    "--output",
                    str(output),
                    "--version",
                    "2.0.0",
                ],
                check=True,
            )

            release = output / "firmware" / "ble-mqtt-scanner"
            self.assertEqual(b"factory", (release / "firmware.factory.bin").read_bytes())
            self.assertEqual(b"ota", (release / "firmware.bin").read_bytes())
            self.assertEqual("2.0.0", json.loads((release / "manifest.json").read_text())["version"])
            ota_manifest = json.loads((release / "ota-manifest.json").read_text())
            self.assertEqual("2.0.0", ota_manifest["version"])
            self.assertEqual("firmware.bin", ota_manifest["firmware"])
            self.assertEqual(3, ota_manifest["size"])
            self.assertEqual(
                "b08bcc8b8e779d1e6476417faa59ea2424e18bbfbbf5b44e6a047e8917cc6f8a",
                ota_manifest["sha256"],
            )


if __name__ == "__main__":
    unittest.main()
