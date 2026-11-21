#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Builds the single-file UniVex Hub installer.

The installer is the uve_hub_setup executable followed by a payload and a footer, the layout
Engine/Runtime/Online/Expose/hub_setup.h reads:

    <setup executable> <payload: gzip-compressed tar> <SHA-256 of payload> <payload size, 8 bytes LE> UVXSETUP

The payload is the Hub alone: the Hub executable, the PRISM assets, the project templates, the
engine feed configuration and the brand icons. Engines are not included; the Hub downloads the
versions a user needs. Hub releases are tagged hub-v<version>, separately from engine releases.

    python3 Engine/Tools/make_hub_installer.py --bin .uvbt/bin/Linux/Release --target linux \\
        --tag hub-v0.1.0 --output dist
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import package_engine

REPO_ROOT = Path(__file__).resolve().parents[2]
DISTRIBUTION_DIR = REPO_ROOT / "Engine" / "Distribution"
BRAND_DIR = DISTRIBUTION_DIR / "brand"
HUB_STEM = "uve_hub"
SETUP_STEM = "uve_hub_setup"
PROJECT_TEMPLATES_DIRECTORY = "project_templates"
HUB_DIRECTORIES = (package_engine.PRISM_ASSETS_DIRECTORY, PROJECT_TEMPLATES_DIRECTORY)
FEED_FILE = "engine_feed.json"
# The brand icons the installed Hub registers: the PNG sizes, and the icon file Windows uses.
ICONS_DIRECTORY = "icons"
BRAND_PNG_DIRECTORY = "png"
WINDOWS_ICON_FILE = "univex-logo.ico"
HUB_TAG_PREFIX = "hub-"
INSTALLER_PREFIX = "UniVexHubSetup-"
PAYLOAD_MAGIC = b"UVXSETUP"
SIZE_FORMAT = "<Q"


def parse_hub_tag(tag: str) -> str:
    """The Hub version a hub-v<version> tag names."""
    if not tag.startswith(HUB_TAG_PREFIX):
        raise package_engine.PackagingError(f"'{tag}' is not a Hub release tag such as hub-v1.0.0")
    version, _channel = package_engine.parse_tag(tag[len(HUB_TAG_PREFIX):])
    return version


def installer_name(version: str, target: str) -> str:
    return f"{INSTALLER_PREFIX}{package_engine.display_version(version)}-{target}{package_engine.EXECUTABLE_SUFFIX[target]}"


def stage_payload(bin_dir: Path, target: str, stage: Path) -> set[str]:
    """Stages the installed files; returns the names that must be executable."""
    package_engine.stage_files(bin_dir, target, (HUB_STEM,), HUB_DIRECTORIES, stage)
    feed = bin_dir / FEED_FILE
    if not feed.is_file():
        raise package_engine.PackagingError(f"missing {feed}")
    shutil.copy2(feed, stage / FEED_FILE)
    icons = stage / ICONS_DIRECTORY
    icons.mkdir()
    for source in sorted((BRAND_DIR / BRAND_PNG_DIRECTORY).glob("*.png")) + [BRAND_DIR / WINDOWS_ICON_FILE]:
        if not source.is_file():
            raise package_engine.PackagingError(f"missing {source}")
        shutil.copy2(source, icons / source.name)
    executables = {f"{HUB_STEM}{package_engine.EXECUTABLE_SUFFIX[target]}"}
    executables.update(path.name for path in stage.glob("*.dll"))
    return executables


def build_installer(bin_dir: Path, target: str, tag: str, output: Path, check_version: bool = True) -> Path:
    version = parse_hub_tag(tag)
    suffix = package_engine.EXECUTABLE_SUFFIX[target]
    if check_version:
        reported = package_engine.built_version(bin_dir / f"{HUB_STEM}{suffix}")
        if reported != version:
            raise package_engine.PackagingError(f"tag {tag} names Hub {version}, but the build is Hub {reported}")
    setup = bin_dir / f"{SETUP_STEM}{suffix}"
    if not setup.is_file():
        raise package_engine.PackagingError(f"missing executable {setup}")
    output.mkdir(parents=True, exist_ok=True)
    installer = output / installer_name(version, target)
    with tempfile.TemporaryDirectory() as scratch:
        stage = Path(scratch) / "hub"
        stage.mkdir()
        executables = stage_payload(bin_dir, target, stage)
        payload = Path(scratch) / "payload.tar.gz"
        package_engine.write_archive(stage, payload, executables)
        data = payload.read_bytes()
    with installer.open("wb") as handle:
        handle.write(setup.read_bytes())
        handle.write(data)
        handle.write(hashlib.sha256(data).digest())
        handle.write(struct.pack(SIZE_FORMAT, len(data)))
        handle.write(PAYLOAD_MAGIC)
    installer.chmod(package_engine.EXECUTABLE_MODE)
    checksum = installer.with_name(installer.name + package_engine.CHECKSUM_SUFFIX)
    checksum.write_text(f"{package_engine.sha256_of(installer)}  {installer.name}\n", encoding="utf-8")
    return installer


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", required=True, type=Path, help="UVBT output directory of the release build")
    parser.add_argument("--target", required=True, choices=sorted(package_engine.EXECUTABLE_SUFFIX))
    parser.add_argument("--tag", required=True, help="Hub release tag, e.g. hub-v0.1.0")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--skip-version-check", action="store_true")
    args = parser.parse_args(argv)
    try:
        installer = build_installer(args.bin, args.target, args.tag, args.output, not args.skip_version_check)
    except (package_engine.PackagingError, subprocess.CalledProcessError) as error:
        print(f"make_hub_installer: {error}", file=sys.stderr)
        return 1
    print(installer)
    return 0


if __name__ == "__main__":
    sys.exit(main())
