#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description="Assemble the GitHub Pages firmware portal")
    parser.add_argument("--firmware-build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    factory_image = args.firmware_build / "firmware.factory.bin"
    ota_image = args.firmware_build / "firmware.bin"
    for image in (factory_image, ota_image):
        if not image.is_file() or image.stat().st_size == 0:
            raise SystemExit(f"required firmware artifact missing or empty: {image}")

    if args.output.exists():
        shutil.rmtree(args.output)
    shutil.copytree(ROOT / "web", args.output)

    release = args.output / "firmware" / "ble-mqtt-scanner"
    release.mkdir(parents=True)
    shutil.copy2(factory_image, release / factory_image.name)
    shutil.copy2(ota_image, release / ota_image.name)
    subprocess.run(
        [
            "python3",
            str(ROOT / "scripts" / "generate_web_manifest.py"),
            "--version",
            args.version,
            "--output",
            str(release / "manifest.json"),
        ],
        check=True,
    )
    ota_bytes = ota_image.read_bytes()
    ota_manifest = {
        "product": "ds9-ble-mqtt-scanner",
        "chip": "ESP32-C6",
        "version": args.version,
        "firmware": "firmware.bin",
        "size": len(ota_bytes),
        "sha256": hashlib.sha256(ota_bytes).hexdigest(),
    }
    (release / "ota-manifest.json").write_text(json.dumps(ota_manifest, indent=2) + "\n")
    (args.output / ".nojekyll").touch()


if __name__ == "__main__":
    main()
