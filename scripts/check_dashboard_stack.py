#!/usr/bin/env python3
"""Fail when the dashboard provisioning loop frame is unsafe."""

from pathlib import Path
import argparse


def stack_bytes(report: str, function: str) -> int:
    matches = []
    for line in report.splitlines():
        columns = line.rsplit("\t", 2)
        if len(columns) == 3 and function in columns[0]:
            matches.append(int(columns[1]))
    if len(matches) != 1:
        raise ValueError(f"expected one stack report for {function}, found {len(matches)}")
    return matches[0]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("--maximum", type=int, default=3072)
    args = parser.parse_args()
    used = stack_bytes(args.report.read_text(), "dashboardHandleProvisioning()")
    if used > args.maximum:
        raise SystemExit(f"dashboardHandleProvisioning stack frame {used} exceeds {args.maximum} bytes")
    print(f"dashboardHandleProvisioning stack frame: {used} bytes (limit {args.maximum})")


if __name__ == "__main__":
    main()
