import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

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
}
EXPECTED_TARGETS = {
    "esp32-devkit-v1": "ESP32",
    "esp32-c3-devkitm-1": "ESP32-C3",
    "esp32-s3-devkitc-1": "ESP32-S3",
    "esp32-c6-devkitc-1": "ESP32-C6",
}
LED_PROJECTS = {"rgb-lamp", "pomodoro-light", "morse-beacon"}


class FirmwarePortalTests(unittest.TestCase):
    def load_catalog(self):
        return json.loads((ROOT / "projects.json").read_text())

    def test_catalog_contains_fourteen_board_only_projects(self):
        catalog = self.load_catalog()
        slugs = {project["slug"] for project in catalog}
        self.assertEqual(EXPECTED_NEW_PROJECTS, slugs - {"ble-mqtt-scanner"})
        self.assertEqual(15, len(catalog))
        for project in catalog:
            self.assertTrue(project["installable"])
            self.assertFalse(project["extra_hardware"])

    def test_catalog_targets_four_supported_boards(self):
        catalog = self.load_catalog()
        for project in catalog:
            targets = {target["id"]: target["chip"] for target in project["targets"]}
            expected = dict(EXPECTED_TARGETS)
            if project["slug"] in LED_PROJECTS:
                expected.pop("esp32-devkit-v1")
            self.assertEqual(expected, targets, project["slug"])
            for target in project["targets"]:
                self.assertTrue(target["name"])
                self.assertTrue(target["environment"])
        self.assertEqual(57, sum(len(project["targets"]) for project in catalog))

    def test_catalog_has_practical_and_fun_projects(self):
        categories = {project["category"] for project in self.load_catalog()}
        self.assertEqual({"Practical", "Fun"}, categories)

    def test_multiboard_scanner_preserves_c6_identity_prefix(self):
        source = (ROOT / "firmware" / "ble-mqtt-scanner" / "src" / "main.cpp").read_text()
        self.assertIn("CONFIG_IDF_TARGET_ESP32C6", source)
        self.assertIn('"esp32c6"', source)
        self.assertIn('"esp32"', source)

    def test_local_portal_labels_the_actual_chip(self):
        source = (ROOT / "firmware" / "no-hardware-lab" / "src" / "app_support.cpp").read_text()
        self.assertNotIn("ESP32-C6 · LOCAL ONLY", source)
        self.assertIn("ESP.getChipModel()", source)

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
        self.assertIn('class="filters" role="group"', html)
        self.assertIn("esp-web-install-button", html)
        self.assertIn('slot="activate"', html)
        self.assertIn('slot="unsupported"', html)
        self.assertIn('fetch("./projects.json")', javascript)
        self.assertIn("selectedTarget.manifest", javascript)
        self.assertIn('setAttribute("manifest", selectedTarget.manifest)', javascript)
        self.assertIn('setAttribute("aria-pressed"', javascript)
        self.assertNotIn('id="installer-button" manifest=', html)

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
                        "features": ["Test"],
                        "installable": True,
                        "extra_hardware": False,
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
                    "features": ["Test"],
                    "installable": True,
                    "extra_hardware": False,
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
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
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
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
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
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
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
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
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
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
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

    def test_site_builder_rejects_shared_artifact_source(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            base = {
                "version": "1.0.0",
                "category": "Fun",
                "description": "Test",
                "features": ["Test"],
                "installable": True,
                "extra_hardware": False,
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
        self.assertIn("python scripts/build_site.py --output _site", workflow)


if __name__ == "__main__":
    unittest.main()
