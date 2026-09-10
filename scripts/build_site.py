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
PRIVATE_CATALOG_FIELDS = {"project_dir", "environment"}
IDENTIFIER = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
REQUIRED_FIELDS = {
    "slug", "name", "chip", "version", "category", "description", "features",
    "installable", "extra_hardware", "project_dir", "environment",
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
    for index, project in enumerate(projects):
        if not isinstance(project, dict):
            fail(f"catalog entry {index} must be an object")
        missing = REQUIRED_FIELDS - project.keys()
        if missing:
            fail(f"catalog entry {index} is missing: {', '.join(sorted(missing))}")

        slug = project["slug"]
        environment = project["environment"]
        if not isinstance(slug, str) or not IDENTIFIER.fullmatch(slug):
            fail(f"invalid slug: {slug!r}")
        if slug in slugs:
            fail(f"duplicate slug: {slug}")
        slugs.add(slug)
        if not isinstance(environment, str) or not IDENTIFIER.fullmatch(environment):
            fail(f"invalid environment: {environment!r}")

        project_value = project["project_dir"]
        if not isinstance(project_value, str) or Path(project_value).is_absolute():
            fail(f"project_dir must be a relative path for {slug}")
        project_dir = (catalog_root / project_value).resolve()
        if not is_within(project_dir, catalog_root):
            fail(f"project_dir escapes catalog root for {slug}")
        if not isinstance(project["features"], list) or not project["features"] or not all(
            isinstance(feature, str) and feature.strip() for feature in project["features"]
        ):
            fail(f"features must be a non-empty string array for {slug}")
        if project["category"] not in {"Practical", "Fun"}:
            fail(f"invalid category for {slug}")
        for field in ("name", "chip", "version", "description"):
            if not isinstance(project[field], str) or not project[field].strip():
                fail(f"{field} must be a non-empty string for {slug}")
        if not isinstance(project["installable"], bool) or not isinstance(project["extra_hardware"], bool):
            fail(f"boolean catalog flags are invalid for {slug}")

        validated.append({**project, "_resolved_project_dir": project_dir})
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

    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(ROOT / "web", output)

    public_catalog = []
    for project in projects:
        slug = project["slug"]
        build = project["_resolved_project_dir"] / ".pio" / "build" / project["environment"]
        factory_image = build / "firmware.factory.bin"
        ota_image = build / "firmware.bin"
        for image in (factory_image, ota_image):
            if not image.is_file() or image.stat().st_size == 0:
                fail(f"required firmware artifact missing or empty: {image}")

        release = output / "firmware" / slug
        release.mkdir(parents=True)
        shutil.copy2(factory_image, release / factory_image.name)
        shutil.copy2(ota_image, release / ota_image.name)
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "generate_web_manifest.py"),
                "--name", project["name"],
                "--chip", project["chip"],
                "--version", project["version"],
                "--output", str(release / "manifest.json"),
            ],
            check=True,
        )

        ota_bytes = ota_image.read_bytes()
        ota_manifest = {
            "product": slug,
            "chip": project["chip"],
            "version": project["version"],
            "firmware": "firmware.bin",
            "size": len(ota_bytes),
            "sha256": hashlib.sha256(ota_bytes).hexdigest(),
        }
        (release / "ota-manifest.json").write_text(json.dumps(ota_manifest, indent=2) + "\n")

        public = {
            key: value for key, value in project.items()
            if key not in PRIVATE_CATALOG_FIELDS and not key.startswith("_")
        }
        public["manifest"] = f"./firmware/{slug}/manifest.json"
        public_catalog.append(public)

    (output / "projects.json").write_text(json.dumps(public_catalog, indent=2) + "\n")
    (output / ".nojekyll").touch()


if __name__ == "__main__":
    main()
