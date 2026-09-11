#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path
from typing import NoReturn
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]
PRIVATE_CATALOG_FIELDS = {"project_dir"}
IDENTIFIER = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
REQUIRED_FIELDS = {
    "slug", "name", "version", "category", "description", "hardware", "features",
    "installable", "extra_hardware", "project_dir", "targets",
}
REQUIRED_TARGET_FIELDS = {"id", "name", "chip", "environment"}
SUPPORTED_TARGETS = {
    "esp32-devkit-v1": {"chip": "ESP32", "board": "esp32dev", "format": "esp-web-tools"},
    "esp32-c3-devkitm-1": {"chip": "ESP32-C3", "board": "esp32-c3-devkitm-1", "format": "esp-web-tools"},
    "esp32-s3-devkitc-1": {"chip": "ESP32-S3", "board": "esp32-s3-devkitc-1", "format": "esp-web-tools"},
    "esp32-c6-devkitc-1": {"chip": "ESP32-C6", "board": "esp32-c6-devkitc-1", "format": "esp-web-tools"},
    "raspberry-pi-pico": {"chip": "RP2040", "board": "rpipico", "format": "uf2"},
    "raspberry-pi-pico-w": {"chip": "RP2040", "board": "rpipicow", "format": "uf2"},
    "raspberry-pi-pico-2": {"chip": "RP2350", "board": "rpipico2", "format": "uf2"},
    "raspberry-pi-pico-2-w": {"chip": "RP2350", "board": "rpipico2w", "format": "uf2"},
}
UF2_FAMILY_IDS = {"RP2040": 0xE48BFF56, "RP2350": 0xE48BFF59}
UF2_MAGIC_START_0 = 0x0A324655
UF2_MAGIC_START_1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000


def fail(message: str) -> NoReturn:
    raise SystemExit(message)


def is_within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def has_symlink_component(path: Path, root: Path) -> bool:
    """Return true when path or any descendant component below root is a symlink."""
    current = path
    while current != root:
        if current.is_symlink():
            return True
        if current.parent == current:
            return True
        current = current.parent
    return root.is_symlink()


def is_allowed_amazon_search(value: str) -> bool:
    try:
        parsed = urlsplit(value)
        parameters = parse_qs(parsed.query, keep_blank_values=True, strict_parsing=True)
        if parsed.port not in (None, 443):
            return False
    except (TypeError, ValueError):
        return False
    return (
        parsed.scheme == "https"
        and parsed.hostname == "www.amazon.com"
        and parsed.username is None
        and parsed.password is None
        and parsed.path == "/s"
        and not parsed.fragment
        and set(parameters) == {"k"}
        and len(parameters["k"]) == 1
        and bool(parameters["k"][0].strip())
    )


def validate_firmware_artifact(image: Path, build: Path, project_dir: Path | None = None) -> None:
    containment_root = project_dir or build
    if has_symlink_component(image, containment_root):
        fail(f"firmware artifact cannot contain a symlink: {image}")
    resolved_project = containment_root.resolve()
    resolved_build = build.resolve()
    resolved_image = image.resolve()
    if (
        not is_within(resolved_build, resolved_project)
        or not is_within(resolved_image, resolved_build)
        or not image.is_file()
        or image.stat().st_size == 0
    ):
        fail(f"required firmware artifact missing, empty, or outside build directory: {image}")


def validate_uf2_image(image: Path, chip: str) -> None:
    expected_family = UF2_FAMILY_IDS.get(chip)
    data = image.read_bytes()
    if expected_family is None or not data or len(data) > 32 * 1024 * 1024 or len(data) % 512 != 0:
        fail(f"UF2 structure is invalid for {image}")
    expected_blocks = len(data) // 512
    seen_blocks = set()
    for offset in range(0, len(data), 512):
        block = data[offset:offset + 512]
        magic0, magic1, flags, _, payload_size, block_number, block_count, family = struct.unpack_from(
            "<IIIIIIII", block
        )
        end_magic = struct.unpack_from("<I", block, 508)[0]
        if (
            magic0 != UF2_MAGIC_START_0
            or magic1 != UF2_MAGIC_START_1
            or end_magic != UF2_MAGIC_END
            or not flags & UF2_FLAG_FAMILY_ID
            or family != expected_family
            or payload_size == 0
            or payload_size > 476
            or block_count != expected_blocks
            or block_number >= block_count
            or block_number in seen_blocks
        ):
            fail(f"UF2 block or chip family is invalid for {image}")
        seen_blocks.add(block_number)
    if seen_blocks != set(range(expected_blocks)):
        fail(f"UF2 block sequence is incomplete for {image}")


def validate_static_web_tree(web: Path, catalog_root: Path) -> None:
    if has_symlink_component(web, catalog_root):
        fail(f"static web root cannot contain a symlink: {web}")
    resolved_web = web.resolve()
    if not web.is_dir() or not is_within(resolved_web, catalog_root.resolve()):
        fail(f"static web root is invalid: {web}")
    for entry in web.rglob("*"):
        if entry.is_symlink():
            fail(f"static web assets cannot contain symlinks: {entry}")
        if not (entry.is_file() or entry.is_dir()) or not is_within(entry.resolve(), resolved_web):
            fail(f"static web asset is invalid: {entry}")


def validate_catalog(projects: object, catalog_root: Path) -> list[dict]:
    if not isinstance(projects, list) or not projects:
        fail("project catalog must be a non-empty array")

    validated = []
    slugs = set()
    artifact_sources = set()
    for index, project in enumerate(projects):
        if not isinstance(project, dict):
            fail(f"catalog entry {index} must be an object")
        missing = REQUIRED_FIELDS - project.keys()
        if missing:
            fail(f"catalog entry {index} is missing: {', '.join(sorted(missing))}")

        slug = project["slug"]
        if not isinstance(slug, str) or not IDENTIFIER.fullmatch(slug):
            fail(f"invalid slug: {slug!r}")
        if slug in slugs:
            fail(f"duplicate slug: {slug}")
        slugs.add(slug)
        targets = project["targets"]
        if not isinstance(targets, list) or not targets:
            fail(f"targets must be a non-empty array for {slug}")
        target_ids = set()
        target_environments = set()
        validated_targets = []
        for target in targets:
            if not isinstance(target, dict) or REQUIRED_TARGET_FIELDS - target.keys():
                fail(f"target fields are invalid for {slug}")
            target_id = target["id"]
            environment = target["environment"]
            if not isinstance(target_id, str) or not IDENTIFIER.fullmatch(target_id):
                fail(f"invalid target id for {slug}: {target_id!r}")
            if target_id not in SUPPORTED_TARGETS:
                fail(f"unsupported target for {slug}: {target_id}")
            if target_id in target_ids:
                fail(f"duplicate target id for {slug}: {target_id}")
            target_ids.add(target_id)
            if not isinstance(environment, str) or not IDENTIFIER.fullmatch(environment):
                fail(f"invalid environment: {environment!r}")
            if environment in target_environments:
                fail(f"duplicate target environment for {slug}: {environment}")
            target_environments.add(environment)
            if target["chip"] != SUPPORTED_TARGETS[target_id]["chip"]:
                fail(f"chip does not match target for {slug}: {target['chip']!r}")
            if not isinstance(target["name"], str) or not target["name"].strip():
                fail(f"target name must be non-empty for {slug}")
            validated_targets.append(dict(target))

        project_value = project["project_dir"]
        if not isinstance(project_value, str) or Path(project_value).is_absolute():
            fail(f"project_dir must be a relative path for {slug}")
        project_dir = (catalog_root / project_value).resolve()
        if not is_within(project_dir, catalog_root):
            fail(f"project_dir escapes catalog root for {slug}")
        for target in validated_targets:
            artifact_source = (project_dir, target["environment"])
            if artifact_source in artifact_sources:
                fail(f"artifact source reused for {slug}: {target['environment']}")
            artifact_sources.add(artifact_source)
        if not isinstance(project["features"], list) or not project["features"] or not all(
            isinstance(feature, str) and feature.strip() for feature in project["features"]
        ):
            fail(f"features must be a non-empty string array for {slug}")
        if project["category"] not in {"Practical", "Fun"}:
            fail(f"invalid category for {slug}")
        for field in ("name", "version", "description", "hardware"):
            if not isinstance(project[field], str) or not project[field].strip():
                fail(f"{field} must be a non-empty string for {slug}")
        if not isinstance(project["installable"], bool) or not isinstance(project["extra_hardware"], bool):
            fail(f"boolean catalog flags are invalid for {slug}")
        hardware_is_external = project["hardware"] != "Board only"
        if project["extra_hardware"] != hardware_is_external:
            fail(f"hardware metadata contradicts extra_hardware for {slug}")
        parts = project.get("parts")
        if project["extra_hardware"]:
            if not isinstance(parts, list) or not parts:
                fail(f"extra-hardware project must declare parts for {slug}")
            for part in parts:
                if not isinstance(part, dict) or set(part) != {"quantity", "name", "specification", "required", "url"}:
                    fail(f"part metadata is invalid for {slug}")
                if not isinstance(part["quantity"], int) or isinstance(part["quantity"], bool) or part["quantity"] < 1:
                    fail(f"part quantity is invalid for {slug}")
                if not all(isinstance(part[field], str) and part[field].strip() for field in ("name", "specification", "url")):
                    fail(f"part values are invalid for {slug}")
                if not isinstance(part["required"], bool):
                    fail(f"part required flag is invalid for {slug}")
                if not is_allowed_amazon_search(part["url"]):
                    fail(f"part URL must use an Amazon HTTPS search for {slug}")
            wiring_root_path = catalog_root / "web" / "wiring"
            wiring_root = wiring_root_path.resolve()
            for target in validated_targets:
                wiring = target.get("wiring")
                if not isinstance(wiring, dict) or set(wiring) != {"diagram", "connections", "warnings"}:
                    fail(f"target wiring metadata is invalid for {slug}: {target['id']}")
                diagram_value = wiring["diagram"]
                if not isinstance(diagram_value, str) or not diagram_value.startswith("./wiring/") or not diagram_value.endswith(".svg"):
                    fail(f"target wiring diagram is invalid for {slug}: {target['id']}")
                connections = wiring["connections"]
                if not isinstance(connections, list) or not connections:
                    fail(f"target wiring connections are invalid for {slug}: {target['id']}")
                for connection in connections:
                    if not isinstance(connection, dict) or set(connection) != {"from", "to", "wire"}:
                        fail(f"target wiring connection is invalid for {slug}: {target['id']}")
                    if not all(isinstance(value, str) and value.strip() for value in connection.values()):
                        fail(f"target wiring connection values are invalid for {slug}: {target['id']}")
                warnings = wiring["warnings"]
                if not isinstance(warnings, list) or not warnings or not all(
                    isinstance(warning, str) and warning.strip() for warning in warnings
                ):
                    fail(f"target wiring warnings are invalid for {slug}: {target['id']}")
                diagram_path = catalog_root / "web" / diagram_value.removeprefix("./")
                if has_symlink_component(diagram_path, catalog_root):
                    fail(f"target wiring asset cannot contain a symlink for {slug}: {target['id']}")
                diagram = diagram_path.resolve()
                if not is_within(diagram, wiring_root) or not diagram.is_file():
                    fail(f"target wiring asset is invalid for {slug}: {target['id']}")
        elif parts not in (None, []):
            fail(f"board-only project cannot declare external parts for {slug}")
        setup = project.get("setup")
        if not isinstance(setup, dict) or not isinstance(setup.get("required"), bool):
            fail(f"setup metadata is invalid for {slug}")
        if not isinstance(setup.get("summary"), str) or not setup["summary"].strip():
            fail(f"setup summary must be non-empty for {slug}")
        if not isinstance(setup.get("fields"), list) or not all(
            isinstance(field, str) and field.strip() for field in setup["fields"]
        ):
            fail(f"setup fields must be a string array for {slug}")
        if setup["required"] != bool(setup["fields"]):
            fail(f"required setup must declare fields and optional setup must not for {slug}")

        validated.append({**project, "targets": validated_targets, "_resolved_project_dir": project_dir})
    return validated


def validate_output(output: Path, catalog_root: Path, catalog_path: Path, project_dirs: list[Path]) -> Path:
    candidate = output.absolute()
    if has_symlink_component(candidate, catalog_root):
        fail("output directory cannot contain a symlink component")
    resolved = candidate.resolve()
    if candidate.name not in {"_site", "site"}:
        fail("output directory must be named _site or site")
    if resolved == catalog_root or not is_within(resolved, catalog_root):
        fail("output directory must be a child of the catalog root")
    protected = [catalog_path, *project_dirs]
    for path in protected:
        if path == resolved or is_within(path, resolved) or is_within(resolved, path):
            fail(f"output directory overlaps protected path: {path}")
    return resolved


def platformio_command() -> str:
    command = shutil.which("pio")
    if command:
        return command
    local = Path.home() / ".venvs" / "platformio" / "bin" / "pio"
    if local.is_file():
        return str(local)
    fail("PlatformIO pio command is required to verify target environments")


def resolved_environment_boards(project_dir: Path) -> dict[str, str]:
    result = subprocess.run(
        [platformio_command(), "project", "config", "--json-output", "-d", str(project_dir)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        fail(f"could not resolve PlatformIO configuration for {project_dir}: {result.stderr.strip()}")
    try:
        sections = json.loads(result.stdout)
        return {
            section.removeprefix("env:"): dict(options)["board"]
            for section, options in sections
            if section.startswith("env:") and "board" in dict(options)
        }
    except (KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        fail(f"invalid PlatformIO configuration output for {project_dir}: {error}")


def verify_environment_boards(projects: list[dict]) -> None:
    cache: dict[Path, dict[str, str]] = {}
    for project in projects:
        project_dir = project["_resolved_project_dir"]
        if project_dir not in cache:
            cache[project_dir] = resolved_environment_boards(project_dir)
        boards = cache[project_dir]
        for target in project["targets"]:
            actual = boards.get(target["environment"])
            expected = SUPPORTED_TARGETS[target["id"]]["board"]
            if actual != expected:
                fail(
                    f"environment board does not match target for {project['slug']}: "
                    f"{target['environment']} resolves to {actual!r}, expected {expected!r}"
                )


def main() -> None:
    parser = argparse.ArgumentParser(description="Assemble the GitHub Pages firmware portal")
    parser.add_argument("--catalog", type=Path, default=ROOT / "projects.json")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    catalog_path = args.catalog.resolve(strict=True)
    catalog_root = ROOT if catalog_path == (ROOT / "projects.json").resolve() else catalog_path.parent
    projects = validate_catalog(json.loads(catalog_path.read_text()), catalog_root)
    output = validate_output(
        args.output,
        catalog_root,
        catalog_path,
        [project["_resolved_project_dir"] for project in projects],
    )
    verify_environment_boards(projects)

    web_source = catalog_root / "web"
    validate_static_web_tree(web_source, catalog_root)
    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(web_source, output)

    public_catalog = []
    for project in projects:
        slug = project["slug"]
        public = {
            key: value for key, value in project.items()
            if key not in PRIVATE_CATALOG_FIELDS and not key.startswith("_")
        }
        public_targets = []
        for target in project["targets"]:
            build = project["_resolved_project_dir"] / ".pio" / "build" / target["environment"]
            release = output / "firmware" / slug / target["id"]
            release.mkdir(parents=True)
            target_public = {key: value for key, value in target.items() if key != "environment"}
            if SUPPORTED_TARGETS[target["id"]]["format"] == "uf2":
                uf2_image = build / "firmware.uf2"
                validate_firmware_artifact(uf2_image, build, project["_resolved_project_dir"])
                validate_uf2_image(uf2_image, target["chip"])
                shutil.copy2(uf2_image, release / uf2_image.name)
                uf2_bytes = uf2_image.read_bytes()
                uf2_manifest = {
                    "product": slug,
                    "target": target["id"],
                    "chip": target["chip"],
                    "version": project["version"],
                    "firmware": "firmware.uf2",
                    "size": len(uf2_bytes),
                    "sha256": hashlib.sha256(uf2_bytes).hexdigest(),
                }
                (release / "uf2-manifest.json").write_text(json.dumps(uf2_manifest, indent=2) + "\n")
                public_targets.append(target_public | {
                    "method": "uf2",
                    "download": f"./firmware/{slug}/{target['id']}/firmware.uf2",
                    "size": uf2_manifest["size"],
                    "sha256": uf2_manifest["sha256"],
                })
                continue

            factory_image = build / "firmware.factory.bin"
            ota_image = build / "firmware.bin"
            for image in (factory_image, ota_image):
                validate_firmware_artifact(image, build, project["_resolved_project_dir"])

            shutil.copy2(factory_image, release / factory_image.name)
            shutil.copy2(ota_image, release / ota_image.name)
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "generate_web_manifest.py"),
                    "--name", f"{project['name']} — {target['name']}",
                    "--chip", target["chip"],
                    "--version", project["version"],
                    "--output", str(release / "manifest.json"),
                ],
                check=True,
            )

            ota_bytes = ota_image.read_bytes()
            ota_manifest = {
                "product": slug,
                "target": target["id"],
                "chip": target["chip"],
                "version": project["version"],
                "firmware": "firmware.bin",
                "size": len(ota_bytes),
                "sha256": hashlib.sha256(ota_bytes).hexdigest(),
            }
            (release / "ota-manifest.json").write_text(json.dumps(ota_manifest, indent=2) + "\n")
            public_targets.append(target_public | {
                "method": "esp-web-tools",
                "manifest": f"./firmware/{slug}/{target['id']}/manifest.json",
            })
        public["targets"] = public_targets
        public_catalog.append(public)

    (output / "projects.json").write_text(json.dumps(public_catalog, indent=2) + "\n")
    (output / ".nojekyll").touch()


if __name__ == "__main__":
    main()
