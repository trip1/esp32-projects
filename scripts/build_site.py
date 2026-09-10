#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NoReturn

ROOT = Path(__file__).resolve().parents[1]
PRIVATE_CATALOG_FIELDS = {"project_dir"}
IDENTIFIER = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
REQUIRED_FIELDS = {
    "slug", "name", "version", "category", "description", "hardware", "features",
    "installable", "extra_hardware", "project_dir", "targets",
}
REQUIRED_TARGET_FIELDS = {"id", "name", "chip", "environment"}
SUPPORTED_TARGETS = {
    "esp32-devkit-v1": {"chip": "ESP32", "board": "esp32dev"},
    "esp32-c3-devkitm-1": {"chip": "ESP32-C3", "board": "esp32-c3-devkitm-1"},
    "esp32-s3-devkitc-1": {"chip": "ESP32-S3", "board": "esp32-s3-devkitc-1"},
    "esp32-c6-devkitc-1": {"chip": "ESP32-C6", "board": "esp32-c6-devkitc-1"},
}


def fail(message: str) -> NoReturn:
    raise SystemExit(message)


def is_within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


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
    if output.is_symlink():
        fail("output directory cannot be a symlink")
    resolved = output.resolve()
    if output.name not in {"_site", "site"}:
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

    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(ROOT / "web", output)

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
            factory_image = build / "firmware.factory.bin"
            ota_image = build / "firmware.bin"
            for image in (factory_image, ota_image):
                if not image.is_file() or image.stat().st_size == 0:
                    fail(f"required firmware artifact missing or empty: {image}")

            release = output / "firmware" / slug / target["id"]
            release.mkdir(parents=True)
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
            public_targets.append({
                key: value for key, value in target.items() if key != "environment"
            } | {"manifest": f"./firmware/{slug}/{target['id']}/manifest.json"})
        public["targets"] = public_targets
        public_catalog.append(public)

    (output / "projects.json").write_text(json.dumps(public_catalog, indent=2) + "\n")
    (output / ".nojekyll").touch()


if __name__ == "__main__":
    main()
