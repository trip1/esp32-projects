#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate an ESP Web Tools manifest")
    parser.add_argument("--name", required=True)
    parser.add_argument("--chip", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    manifest = {
        "name": args.name,
        "version": args.version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "builds": [
            {
                "chipFamily": args.chip,
                "improv": False,
                "parts": [{"path": "firmware.factory.bin", "offset": 0}],
            }
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
