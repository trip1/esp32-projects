#!/usr/bin/env python3
"""Build, publish, and verify the firmware portal locally."""

from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import hashlib
import json
import os
import re
import secrets
import shutil
import stat
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "_site"


def run(args: list[str], cwd: Path = ROOT, capture: bool = False, check: bool = True) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(args), flush=True)
    return subprocess.run(args, cwd=cwd, check=check, text=True, capture_output=capture)


def output(args: list[str], cwd: Path = ROOT) -> str:
    return run(args, cwd=cwd, capture=True).stdout.strip()


def repository_name(remote: str) -> str:
    scp = re.fullmatch(r"git@github\.com:([^/@:]+)/([^/@:]+?)(?:\.git)?", remote)
    if scp:
        return f"{scp.group(1)}/{scp.group(2)}"
    parsed = urllib.parse.urlsplit(remote)
    if parsed.scheme not in {"https", "ssh"} or parsed.hostname != "github.com":
        raise RuntimeError("origin must be an exact github.com HTTPS or SSH remote")
    if parsed.username not in {None, "git"} or parsed.password is not None or parsed.port is not None:
        raise RuntimeError("credential-bearing or nonstandard GitHub remotes are not allowed")
    parts = parsed.path.removesuffix(".git").strip("/").split("/")
    if len(parts) != 2 or not all(parts) or parsed.query or parsed.fragment:
        raise RuntimeError("origin must identify exactly one GitHub owner/repository")
    return "/".join(parts)


def pio_command() -> str:
    found = shutil.which("pio")
    if found:
        return found
    fallback = Path.home() / ".venvs" / "platformio" / "bin" / "pio"
    if fallback.is_file():
        return str(fallback)
    raise RuntimeError("PlatformIO pio executable not found")


def assert_clean_main() -> tuple[str, str, str]:
    if output(["git", "branch", "--show-current"]) != "main":
        raise RuntimeError("local Pages publishing requires the main branch")
    if output(["git", "status", "--porcelain", "--untracked-files=all"]):
        raise RuntimeError("working tree must be clean before publishing")
    run(["git", "fetch", "origin", "main"])
    head = output(["git", "rev-parse", "HEAD"])
    upstream = output(["git", "rev-parse", "origin/main"])
    if head != upstream:
        raise RuntimeError("HEAD must exactly match origin/main before publishing")
    remote = output(["git", "remote", "get-url", "origin"])
    return head, remote, repository_name(remote)


def revalidate_source(source_commit: str) -> None:
    if output(["git", "branch", "--show-current"]) != "main":
        raise RuntimeError("branch changed during build")
    if output(["git", "status", "--porcelain", "--untracked-files=all"]):
        raise RuntimeError("working tree changed during build")
    run(["git", "fetch", "origin", "main"])
    if output(["git", "rev-parse", "HEAD"]) != source_commit:
        raise RuntimeError("HEAD changed during build")
    if output(["git", "rev-parse", "origin/main"]) != source_commit:
        raise RuntimeError("origin/main changed during build")


def compile_and_run(output_path: str, includes: list[str], sources: list[str]) -> None:
    command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror"]
    command.extend(item for include in includes for item in ("-I", include))
    command.extend(sources)
    command.extend(["-o", output_path])
    run(command)
    run([output_path])


def verify_and_build(pio: str) -> None:
    run(["python3", "-m", "unittest", "discover", "-s", "tests", "-v"])
    run(["python3", "scripts/generate_wiring_diagrams.py"])
    run(["git", "diff", "--exit-code", "--", "web/wiring"])
    if output(["git", "status", "--porcelain", "--", "web/wiring"]):
        raise RuntimeError("wiring generation created untracked or deleted assets")

    run([pio, "test", "-d", "firmware/ble-mqtt-scanner", "-e", "native"])
    compile_and_run("/tmp/no-hardware-lab-tests", ["firmware/no-hardware-lab/include"], ["firmware/no-hardware-lab/src/lab_logic.cpp", "firmware/no-hardware-lab/test/native/test_main.cpp"])
    compile_and_run("/tmp/bme280-config-tests", ["firmware/bme280-mqtt-sensor/include"], ["firmware/bme280-mqtt-sensor/src/sensor_config_logic.cpp", "firmware/bme280-mqtt-sensor/test/native/test_main.cpp"])
    compile_and_run("/tmp/hardware-lab-tests", ["firmware/hardware-lab/include"], ["firmware/hardware-lab/src/hardware_logic.cpp", "firmware/hardware-lab/test/native/test_main.cpp"])
    compile_and_run("/tmp/ntp-clock-display-tests", ["firmware/common", "firmware/hardware-lab/include"], ["firmware/hardware-lab/src/ntp_clock_display.cpp", "firmware/hardware-lab/test/clock_native/test_main.cpp"])
    compile_and_run("/tmp/pico-lab-tests", ["firmware/pico-lab/include"], ["firmware/pico-lab/src/pico_logic.cpp", "firmware/pico-lab/test/native/test_main.cpp"])
    compile_and_run("/tmp/esp32-diagnostics-tests", ["firmware/esp32-diagnostics/include"], ["firmware/esp32-diagnostics/src/diagnostics_logic.cpp", "firmware/esp32-diagnostics/test/native/test_main.cpp"])
    compile_and_run("/tmp/lcd-panel-tests", ["firmware/common", "firmware/lcd-panels/include"], ["firmware/lcd-panels/src/panel_logic.cpp", "firmware/lcd-panels/test/native/test_main.cpp"])
    compile_and_run("/tmp/dashboard-tests", ["firmware/lcd-panels/include"], ["firmware/lcd-panels/src/dashboard_logic.cpp", "firmware/lcd-panels/test/dashboard_native/test_main.cpp"])

    for project in ("ble-mqtt-scanner", "no-hardware-lab", "bme280-mqtt-sensor", "hardware-lab", "pico-lab", "esp32-diagnostics", "lcd-panels"):
        # Keep downloaded PlatformIO packages cached, but never trust reusable
        # compiled outputs as release artifacts.
        run([pio, "run", "-d", f"firmware/{project}", "-t", "clean"])
        run([pio, "run", "-d", f"firmware/{project}"])

    for environment in ("smart-dashboard--esp32", "smart-dashboard--esp32-c3", "smart-dashboard--esp32-s3", "smart-dashboard--esp32-c6"):
        run(["python3", "scripts/check_dashboard_stack.py", f"firmware/lcd-panels/.pio/build/{environment}/src/dashboard_config.cpp.su"])

    arduino_json = "firmware/lcd-panels/.pio/libdeps/smart-dashboard--esp32/ArduinoJson/src"
    compile_and_run("/tmp/dashboard-space-tests", ["firmware/lcd-panels/include", arduino_json], ["firmware/lcd-panels/src/dashboard_space.cpp", "firmware/lcd-panels/test/dashboard_space_native/test_main.cpp"])
    run(["python3", "scripts/build_site.py", "--output", "_site"])
    if output(["git", "status", "--porcelain", "--untracked-files=all"]):
        raise RuntimeError("build changed the tracked source tree")


def copy_tree_no_links(source_fd: int, destination: Path) -> None:
    with os.scandir(source_fd) as entries:
        for entry in entries:
            if entry.name == ".git":
                raise RuntimeError("generated site must not contain .git at any depth")
            source_stat = os.stat(entry.name, dir_fd=source_fd, follow_symlinks=False)
            target = destination / entry.name
            if stat.S_ISDIR(source_stat.st_mode):
                child_fd = os.open(entry.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=source_fd)
                try:
                    opened = os.fstat(child_fd)
                    if (opened.st_dev, opened.st_ino) != (source_stat.st_dev, source_stat.st_ino):
                        raise RuntimeError(f"site directory changed while copying: {entry.name}")
                    target.mkdir()
                    copy_tree_no_links(child_fd, target)
                finally:
                    os.close(child_fd)
            elif stat.S_ISREG(source_stat.st_mode):
                file_fd = os.open(entry.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=source_fd)
                try:
                    opened = os.fstat(file_fd)
                    if (opened.st_dev, opened.st_ino) != (source_stat.st_dev, source_stat.st_ino):
                        raise RuntimeError(f"site file changed while copying: {entry.name}")
                    with os.fdopen(file_fd, "rb", closefd=False) as source_file, target.open("xb") as target_file:
                        shutil.copyfileobj(source_file, target_file, length=1024 * 1024)
                finally:
                    os.close(file_fd)
            else:
                raise RuntimeError(f"generated site contains a link or special file: {entry.name}")


def copy_site(destination: Path, source_commit: str, portal_version: str, deployment_id: str) -> None:
    if not SITE.is_dir():
        raise RuntimeError("_site does not exist")
    if any(destination.iterdir()):
        raise RuntimeError("publication destination must be empty")
    source_fd = os.open(SITE, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        copy_tree_no_links(source_fd, destination)
    finally:
        os.close(source_fd)
    (destination / ".nojekyll").write_text("")
    (destination / "SOURCE_COMMIT").write_text(source_commit + "\n")
    (destination / "DEPLOYMENT_ID").write_text(deployment_id + "\n")
    files = []
    for path in sorted(item for item in destination.rglob("*") if item.is_file()):
        content = path.read_bytes()
        files.append({
            "path": path.relative_to(destination).as_posix(),
            "size": len(content),
            "sha256": hashlib.sha256(content).hexdigest(),
        })
    build_info = {
        "schema": 1,
        "source_commit": source_commit,
        "deployment_id": deployment_id,
        "portal_version": portal_version,
        "file_count": len(files),
        "total_bytes": sum(item["size"] for item in files),
        "files": files,
    }
    (destination / "BUILD_INFO.json").write_text(json.dumps(build_info, indent=2, sort_keys=True) + "\n")


def configure_pages(repo: str) -> str:
    payload = json.dumps({"build_type": "legacy", "source": {"branch": "gh-pages", "path": "/"}, "https_enforced": True})
    subprocess.run(["gh", "api", "--method", "PUT", f"repos/{repo}/pages", "--input", "-"], input=payload, text=True, check=True, capture_output=True)
    info = json.loads(output(["gh", "api", f"repos/{repo}/pages"]))
    if (info.get("build_type") != "legacy"
            or info.get("source") != {"branch": "gh-pages", "path": "/"}
            or info.get("https_enforced") is not True):
        raise RuntimeError(f"Pages source did not update: {info}")
    subprocess.run(["gh", "api", "--method", "POST", f"repos/{repo}/pages/builds"], text=True, check=True, capture_output=True)
    return info["html_url"].rstrip("/") + "/"


def wait_for_marker(base_url: str, marker_name: str, marker_value: str) -> None:
    url = urllib.parse.urljoin(base_url, marker_name)
    deadline = time.monotonic() + 600
    while time.monotonic() < deadline:
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "local-pages-publisher/1"})
            with urllib.request.urlopen(request, timeout=20) as response:
                if response.read().decode().strip() == marker_value:
                    return
        except Exception:
            pass
        time.sleep(5)
    raise RuntimeError(f"timed out waiting for published {marker_name}")


def served_files(publish_root: Path) -> list[Path]:
    return sorted(
        path for path in publish_root.rglob("*")
        if path.is_file() and path.name != ".nojekyll" and ".git" not in path.relative_to(publish_root).parts
    )


def verify_live(publish_root: Path, base_url: str) -> None:
    files = served_files(publish_root)

    def verify(path: Path) -> tuple[str, str | None]:
        relative = path.relative_to(publish_root).as_posix()
        url = base_url + "/".join(urllib.parse.quote(part) for part in relative.split("/"))
        expected = path.read_bytes()
        error: Exception | None = None
        actual = b""
        for attempt in range(4):
            try:
                request = urllib.request.Request(url, headers={"User-Agent": "local-pages-publisher/1"})
                with urllib.request.urlopen(request, timeout=45) as response:
                    actual = response.read()
                if actual == expected:
                    return relative, None
            except Exception as caught:
                error = caught
            time.sleep(2 ** attempt)
        if error is not None and not actual:
            return relative, f"fetch failed: {error}"
        return relative, f"mismatch expected={hashlib.sha256(expected).hexdigest()} actual={hashlib.sha256(actual).hexdigest()}"

    errors = []
    with ThreadPoolExecutor(max_workers=8) as pool:
        for future in as_completed([pool.submit(verify, path) for path in files]):
            relative, error = future.result()
            if error:
                errors.append((relative, error))
    if errors:
        raise RuntimeError(f"live verification failed for {len(errors)} files: {errors[:10]}")
    print(f"Verified {len(files)} live files ({sum(path.stat().st_size for path in files)} bytes)")


def publish(publish_root: Path, source_commit: str, deployment_id: str, remote: str, repo: str) -> None:
    old = output(["git", "ls-remote", "--heads", "origin", "gh-pages"])
    old_sha = old.split()[0] if old else ""
    run(["git", "init", "--initial-branch=gh-pages"], cwd=publish_root)
    # Never print the remote argument. repository_name() has already rejected
    # userinfo, but keeping URLs out of logs prevents future credential leaks.
    subprocess.run(["git", "remote", "add", "origin", remote], cwd=publish_root, check=True, text=True)
    run(["git", "add", "-A"], cwd=publish_root)
    run(["git", "-c", "user.name=DS9 Local Pages Publisher", "-c", "user.email=pages@ds9labs.local", "commit", "-m", f"deploy: pages from {source_commit}"], cwd=publish_root)
    push = ["git", "push"]
    if old_sha:
        push.append(f"--force-with-lease=refs/heads/gh-pages:{old_sha}")
    push.extend(["origin", "HEAD:refs/heads/gh-pages"])
    run(push, cwd=publish_root)
    deployment_commit = output(["git", "rev-parse", "HEAD"], cwd=publish_root)
    base_url = configure_pages(repo)
    wait_for_marker(base_url, "DEPLOYMENT_ID", deployment_id)
    verify_live(publish_root, base_url)
    remote_after = output(["git", "ls-remote", "--heads", "origin", "gh-pages"], cwd=publish_root)
    if not remote_after or remote_after.split()[0] != deployment_commit:
        raise RuntimeError("gh-pages changed during live verification")
    wait_for_marker(base_url, "DEPLOYMENT_ID", deployment_id)
    print(f"Published {source_commit} as deployment {deployment_id} to {base_url}")


def main() -> None:
    source_commit, remote, repo = assert_clean_main()
    verify_and_build(pio_command())
    portal_version = output(["git", "show", f"{source_commit}:VERSION"])
    deployment_id = secrets.token_hex(16)
    with tempfile.TemporaryDirectory(prefix="esp32-pages-snapshot-") as temporary:
        publish_root = Path(temporary)
        # Snapshot generated/ignored outputs before the final source validation;
        # all later publication and verification uses only this protected copy.
        copy_site(publish_root, source_commit, portal_version, deployment_id)
        revalidate_source(source_commit)
        publish(publish_root, source_commit, deployment_id, remote, repo)


if __name__ == "__main__":
    main()
