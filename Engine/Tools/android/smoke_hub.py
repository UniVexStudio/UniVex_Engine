#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Installs the UniVex Hub APK on a running emulator or device and proves it starts and draws.

    python3 Engine/Tools/android/smoke_hub.py --adb adb --serial emulator-5554 --apk UniVexHub-x86_64.apk

The Hub logs markers under the "UniVex" tag (hub_main.cpp): UVE_HUB_STARTED, UVE_HUB_RENDERED once
the first frame is on screen, UVE_HUB_FAILED or UVE_HUB_STOPPED when it ends. The check starts the
app, waits for a drawn frame, sends it to the background and back, and confirms it is still alive.
On failure it prints the whole device log, crash buffer included, so a native crash's backtrace
(logged by the system, not by the app) is in the job log.
"""
import argparse
import json
import os
import subprocess
import sys
import tarfile
import time
from pathlib import Path

PACKAGE = "com.univex.hub"
# Engine/App/Android/hub/AndroidManifest.xml
ACTIVITY = f"{PACKAGE}/com.univex.app.UniVexActivity"
LOG_TAG = "UniVex"
STARTED = "UVE_HUB_STARTED"
RENDERED = "UVE_HUB_RENDERED"
EDITOR_STARTED = "UVE_EDITOR_STARTED"
EDITOR_RENDERED = "UVE_EDITOR_RENDERED"
ENDED = ("UVE_HUB_FAILED", "UVE_HUB_STOPPED", "UVE_HUB_EDITOR_FAILED", "UVE_EDITOR_FAILED")
DEFAULT_TIMEOUT_SECONDS = 120.0
POLL_SECONDS = 1.0
# How long the Hub must stay alive after coming back to the foreground.
SETTLE_SECONDS = 5.0
ANNOTATION_LIMIT = 3500


def _read_package_version(package: Path) -> tuple[str, tuple[int, int, int, int]]:
    with tarfile.open(package, "r:gz") as tar:
        manifest = json.loads(tar.extractfile("engine.json").read().decode("utf-8"))
    version = manifest["version"]
    core, _, build = version.partition("+build.")
    major, minor, patch = (int(part) for part in core.split("."))
    return version, (major, minor, patch, int(build or "0"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--engine-package", type=Path,
                        help="Optional ABI-matching .uvpackage to install and open in the Android editor")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    def adb(*command: str, timeout: float | None = None) -> str:
        return subprocess.run([args.adb, "-s", args.serial, *command], check=True, capture_output=True,
                              timeout=args.timeout if timeout is None else timeout).stdout.decode(errors="replace")

    def app_log() -> str:
        return adb("logcat", "-d", "-s", f"{LOG_TAG}:I", "*:S")

    def alive() -> bool:
        try:
            return bool(adb("shell", "pidof", PACKAGE).strip())
        except (subprocess.SubprocessError, OSError):
            return False

    def wait_for(marker: str, count: int = 1, allowed_ended: tuple[str, ...] = ()) -> None:
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            text = app_log()
            for ended in ENDED:
                if ended in allowed_ended:
                    continue
                if ended in text:
                    line = next(entry for entry in text.splitlines() if ended in entry)
                    raise RuntimeError(f"the app ended before {marker}: {line.strip()}")
            if text.count(marker) >= count:
                print(marker, flush=True)
                return
            if (STARTED in text or EDITOR_STARTED in text) and not alive():
                raise RuntimeError(f"the app process died before {marker}")
            time.sleep(POLL_SECONDS)
        raise RuntimeError(f"no {marker} within {args.timeout:.0f} s")

    def verify_engine_package(package: Path) -> None:
        version, (major, minor, patch, build) = _read_package_version(package)
        adb("shell", "am", "force-stop", PACKAGE)
        remote_pkg = "/data/local/tmp/smoke_engine.uvpackage"
        remote_proj = "/data/local/tmp/smoke.uvproject"
        adb("push", str(package.resolve()), remote_pkg)
        project_json = json.dumps({
            "format": "uvproject",
            "schemaVersion": 1,
            "revision": 1,
            "projectId": "smoke-project",
            "displayName": "Smoke Project",
            "engineVersion": {"major": major, "minor": minor, "patch": patch, "build": build},
            "contentRoot": "Content",
            "assetDatabasePath": ".uvassetdb",
            "settingsPath": "Config/project_settings.json",
            "startupScenePath": "Content/Viewport/Viewport.uvscene",
        })
        eng_rel = f"files/UniVex/Engines/{version}"
        adb("shell", f"run-as {PACKAGE} rm -rf {eng_rel} files/smoke")
        adb("shell", f"run-as {PACKAGE} mkdir -p {eng_rel} files/smoke/Content/Viewport files/smoke/Config")
        # Pipe from shell so SELinux does not block run-as from opening /data/local/tmp directly.
        adb("shell", f"cat {remote_pkg} | run-as {PACKAGE} tar -xzf - -C {eng_rel}")
        adb("shell", f"run-as {PACKAGE} chmod 0555 {eng_rel}/uve_editor")
        subprocess.run(
            [args.adb, "-s", args.serial, "shell", f"run-as {PACKAGE} tee files/smoke/smoke.uvproject >/dev/null"],
            input=project_json, text=True, check=True,
        )
        adb("shell", "rm", "-f", remote_pkg, remote_proj)
        adb("logcat", "-c")
        project_path = f"/data/data/{PACKAGE}/files/smoke/smoke.uvproject"
        adb("shell", "am", "start", "-W", "-n", ACTIVITY, "--es", "project", project_path)
        wait_for(EDITOR_STARTED, allowed_ended=("UVE_HUB_STOPPED",))
        wait_for(EDITOR_RENDERED, allowed_ended=("UVE_HUB_STOPPED",))
        time.sleep(SETTLE_SECONDS)
        if not alive():
            raise RuntimeError("the Android editor died after rendering its first frame")
        # Verify full PBR/VFX GLES 3.0 shader compilation with zero errors on Android.
        shader_logs = adb("logcat", "-d")
        try:
            shader_logs += "\n" + adb("shell", f"run-as {PACKAGE} cat files/uve_editor.log")
        except (subprocess.SubprocessError, OSError):
            pass
        shader_errors = [
            line.strip()
            for line in shader_logs.splitlines()
            if "CreateShaderUVE compilation failed" in line
        ]
        if shader_errors:
            sample = " | ".join(shader_errors[:5])
            raise RuntimeError(f"Android GLES 3.0 shader compilation failed ({len(shader_errors)} error(s)): {sample}")
        print("smoke_hub: Android editor loaded via dlopen and rendered cleanly with zero shader errors", flush=True)

    try:
        if adb("shell", "getprop", "sys.boot_completed").strip() != "1":
            raise RuntimeError("the device has not finished booting")
        adb("install", "-r", str(args.apk.resolve()))
        adb("logcat", "-c")
        adb("shell", "am", "start", "-W", "-n", ACTIVITY)
        wait_for(STARTED)
        wait_for(RENDERED)
        adb("shell", "input", "keyevent", "KEYCODE_HOME")
        adb("shell", "am", "start", "-W", "-n", ACTIVITY)
        time.sleep(SETTLE_SECONDS)
        if not alive():
            raise RuntimeError("the Hub died after returning to the foreground")
        print("smoke_hub: the Hub started, drew and resumed", flush=True)
        if args.engine_package is not None:
            verify_engine_package(args.engine_package)
        return 0
    except (subprocess.SubprocessError, OSError, RuntimeError) as error:
        print(f"smoke_hub: FAILED: {error}", file=sys.stderr, flush=True)
        try:
            device_log = adb("logcat", "-d", "-b", "main,system,crash", timeout=20.0)
        except (subprocess.SubprocessError, OSError) as log_error:
            device_log = f"(no device log: {log_error})"
        print(device_log, flush=True)
        if os.environ.get("GITHUB_ACTIONS"):
            crash = [line for line in device_log.splitlines() if "F DEBUG" in line or "FATAL" in line or LOG_TAG in line]
            text = " ".join(f"{error} | {' / '.join(crash[-20:])}".split())[:ANNOTATION_LIMIT]
            print(f"::error title=Hub on Android::{text}", flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
