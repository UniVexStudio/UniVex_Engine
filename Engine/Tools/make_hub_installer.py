#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Builds the single-file UniVex Hub installer.

On Windows the installer is a standard setup wizard: Inno Setup's compiler (ISCC) builds it from
Engine/Distribution/windows/UniVexHub.iss and the staged Hub, with every name passed in from here.
On Linux it is the uve_hub_setup executable followed by a payload and a footer, the layout
Engine/Runtime/Online/Expose/hub_setup.h reads:

    <setup executable> <payload: gzip-compressed tar> <SHA-256 of payload> <payload size, 8 bytes LE> UVXSETUP

The payload is the Hub alone: the Hub executable, the PRISM assets, the project templates, the
engine feed configuration and the brand icons. Engines are not included; the Hub downloads the
versions a user needs. Hub releases are tagged hub-v<version>, separately from engine releases.

    python3 Engine/Tools/make_hub_installer.py --bin .uvbt/bin/Linux/Release --target linux \\
        --tag hub-v0.1.0 --output dist
    python Engine/Tools/make_hub_installer.py --bin .uvbt/bin/Windows/Release --target windows \\
        --tag hub-v0.1.0 --output dist --iscc "C:/Program Files (x86)/Inno Setup 6/ISCC.exe"
"""

from __future__ import annotations

import argparse
import hashlib
import os
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
# What the installed Hub is called and registers; the same values as hub_setup.h and
# editor_project_package.h (test_shared_names.py compares them).
HUB_DISPLAY_NAME = "UniVex Hub"
HUB_DESCRIPTION = "Create, open and manage UniVex projects and engine versions"
HUB_INSTALL_FOLDER = "UniVex Hub"
PUBLISHER = "UniVex Studios"
PROJECT_EXTENSION = ".uvproject"
PROJECT_PROGRAM_ID = "UniVex.Project"
PROJECT_TYPE_NAME = "UniVex Project"
# Windows wizard (Inno Setup). The AppId never changes, so a newer installer upgrades the same
# installation and Apps & features lists one entry.
WINDOWS_INSTALLER_SCRIPT = DISTRIBUTION_DIR / "windows" / "UniVexHub.iss"
WINDOWS_APP_ID = "2E511CB6-710E-49B1-A004-500E20F0A7B7"
ISCC_ENVIRONMENT = "UVE_ISCC"
ISCC_NAMES = ("ISCC", "iscc")


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
    linux_lib_dir = stage / package_engine.LINUX_LIB_DIRECTORY
    if linux_lib_dir.is_dir():
        executables.update(path.name for path in linux_lib_dir.iterdir() if path.is_file())
    return executables


def numeric_version(version: str) -> str:
    """"MAJOR.MINOR.PATCH.BUILD", the four-number form Windows file versions take."""
    core, _, build = version.partition("+build.")
    return f"{core}.{build or 0}"


def find_iscc(explicit: Path | None) -> Path:
    """Inno Setup's command-line compiler: --iscc, then $UVE_ISCC, then the PATH."""
    candidates = [explicit, Path(os.environ[ISCC_ENVIRONMENT]) if os.environ.get(ISCC_ENVIRONMENT) else None]
    candidates += [Path(found) for found in (shutil.which(name) for name in ISCC_NAMES) if found]
    for candidate in candidates:
        if candidate is not None and candidate.is_file():
            return candidate
    raise package_engine.PackagingError(f"Inno Setup's ISCC was not found; pass --iscc or set {ISCC_ENVIRONMENT}")


def build_windows_wizard(bin_dir: Path, version: str, output: Path, iscc: Path) -> Path:
    """Compiles the Inno Setup wizard around the staged Hub."""
    installer = output / installer_name(version, "windows")
    with tempfile.TemporaryDirectory() as scratch:
        stage = Path(scratch) / "hub"
        stage.mkdir()
        stage_payload(bin_dir, "windows", stage)
        hub = f"{HUB_STEM}{package_engine.EXECUTABLE_SUFFIX['windows']}"
        defines = {
            "AppId": WINDOWS_APP_ID,
            "AppName": HUB_DISPLAY_NAME,
            "AppVersion": package_engine.display_version(version),
            "NumericVersion": numeric_version(version),
            "Publisher": PUBLISHER,
            "Description": HUB_DESCRIPTION,
            "InstallFolder": HUB_INSTALL_FOLDER,
            "HubExe": hub,
            "IconFile": str(BRAND_DIR / WINDOWS_ICON_FILE),
            "WindowsIcon": f"{ICONS_DIRECTORY}\\{WINDOWS_ICON_FILE}",
            "ProjectExtension": PROJECT_EXTENSION,
            "ProgramId": PROJECT_PROGRAM_ID,
            "ProjectTypeName": PROJECT_TYPE_NAME,
            "StageDir": str(stage),
            "OutputDir": str(output.resolve()),
            "OutputName": installer.stem,
        }
        subprocess.run([str(iscc), "/Qp", *(f"/D{name}={value}" for name, value in defines.items()),
                        str(WINDOWS_INSTALLER_SCRIPT)], check=True)
    if not installer.is_file():
        raise package_engine.PackagingError(f"ISCC did not write {installer}")
    return installer


def write_checksum(installer: Path) -> None:
    checksum = installer.with_name(installer.name + package_engine.CHECKSUM_SUFFIX)
    checksum.write_text(f"{package_engine.sha256_of(installer)}  {installer.name}\n", encoding="utf-8")


def build_installer(bin_dir: Path, target: str, tag: str, output: Path, check_version: bool = True,
                    iscc: Path | None = None) -> Path:
    version = parse_hub_tag(tag)
    suffix = package_engine.EXECUTABLE_SUFFIX[target]
    if check_version:
        reported = package_engine.built_version(bin_dir / f"{HUB_STEM}{suffix}")
        if reported != version:
            raise package_engine.PackagingError(f"tag {tag} names Hub {version}, but the build is Hub {reported}")
    output.mkdir(parents=True, exist_ok=True)
    if target == "windows":
        installer = build_windows_wizard(bin_dir, version, output, find_iscc(iscc))
        write_checksum(installer)
        return installer
    setup = bin_dir / f"{SETUP_STEM}{suffix}"
    if not setup.is_file():
        raise package_engine.PackagingError(f"missing executable {setup}")
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
    write_checksum(installer)
    return installer


DEB_PACKAGE_NAME = "univex-hub"
DEB_INSTALL_ROOT = "opt/univex-hub"
DEB_ARCHITECTURE = "amd64"
DEB_MAINTAINER = "UniVex Studios <support@univexstudios.com>"
DEB_MIME_TYPE = "application/x-univex-project"
DEB_ICON_NAME = "univex-hub"
DEB_MIME_ICON_NAME = "application-x-univex-project"
DEB_ICON_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)


def deb_installer_name(version: str, architecture: str = DEB_ARCHITECTURE) -> str:
    return f"UniVexHub-{package_engine.display_version(version)}-linux-{architecture}.deb"


def deb_package_version(version: str) -> str:
    """Valid Debian package version (MAJOR.MINOR.PATCH[~buildN])."""
    core, _, build = version.partition("+build.")
    return core if build in ("", "0") else f"{core}.0.{build}"


def build_linux_deb(bin_dir: Path, version: str, output: Path, architecture: str = DEB_ARCHITECTURE) -> Path:
    """Builds a double-clickable Debian/Ubuntu .deb package for UniVex Hub (zero terminal needed)."""
    dpkg_deb = shutil.which("dpkg-deb")
    if dpkg_deb is None:
        raise package_engine.PackagingError("dpkg-deb is needed to build the Linux .deb installer")
    output.mkdir(parents=True, exist_ok=True)
    deb_path = output / deb_installer_name(version, architecture)
    display_ver = package_engine.display_version(version)
    deb_ver = deb_package_version(version)
    with tempfile.TemporaryDirectory() as scratch:
        pkg_root = Path(scratch) / "pkg"
        opt_dir = pkg_root / DEB_INSTALL_ROOT
        opt_dir.mkdir(parents=True)
        executables = stage_payload(bin_dir, "linux", opt_dir)
        for path in opt_dir.rglob("*"):
            if path.is_dir():
                path.chmod(package_engine.DIRECTORY_MODE)
            elif path.is_file():
                mode = package_engine.EXECUTABLE_MODE if path.name in executables else package_engine.FILE_MODE
                path.chmod(mode)

        usr_bin = pkg_root / "usr" / "bin"
        usr_bin.mkdir(parents=True)
        (usr_bin / HUB_STEM).symlink_to(f"/{DEB_INSTALL_ROOT}/{HUB_STEM}")

        apps_dir = pkg_root / "usr" / "share" / "applications"
        apps_dir.mkdir(parents=True)
        desktop_entry = (
            "[Desktop Entry]\n"
            "Type=Application\n"
            f"Name={HUB_DISPLAY_NAME}\n"
            f"Comment={HUB_DESCRIPTION}\n"
            "Version=1.5\n"
            f"X-UniVex-Version={display_ver}\n"
            f"Exec=/{DEB_INSTALL_ROOT}/{HUB_STEM} %f\n"
            f"Path=/{DEB_INSTALL_ROOT}\n"
            f"Icon={DEB_ICON_NAME}\n"
            f"StartupWMClass={HUB_DISPLAY_NAME}\n"
            f"MimeType={DEB_MIME_TYPE};\n"
            "Terminal=false\n"
            "Categories=Development;IDE;\n"
        )
        desktop_file = apps_dir / "univex-hub.desktop"
        desktop_file.write_text(desktop_entry, encoding="utf-8")
        desktop_file.chmod(package_engine.FILE_MODE)

        mime_dir = pkg_root / "usr" / "share" / "mime" / "packages"
        mime_dir.mkdir(parents=True)
        mime_xml = (
            '<?xml version="1.0" encoding="UTF-8"?>\n'
            '<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">\n'
            f'  <mime-type type="{DEB_MIME_TYPE}">\n'
            f"    <comment>{PROJECT_TYPE_NAME}</comment>\n"
            f'    <icon name="{DEB_MIME_ICON_NAME}"/>\n'
            f'    <glob pattern="*{PROJECT_EXTENSION}"/>\n'
            "  </mime-type>\n"
            "</mime-info>\n"
        )
        mime_file = mime_dir / "univex-project.xml"
        mime_file.write_text(mime_xml, encoding="utf-8")
        mime_file.chmod(package_engine.FILE_MODE)

        for size in DEB_ICON_SIZES:
            src_png = BRAND_DIR / BRAND_PNG_DIRECTORY / f"univex-logo-{size}.png"
            if not src_png.is_file():
                continue
            for ctx, icon_stem in (("apps", DEB_ICON_NAME), ("mimetypes", DEB_MIME_ICON_NAME)):
                dst_dir = pkg_root / "usr" / "share" / "icons" / "hicolor" / f"{size}x{size}" / ctx
                dst_dir.mkdir(parents=True, exist_ok=True)
                dst_png = dst_dir / f"{icon_stem}.png"
                shutil.copy2(src_png, dst_png)
                dst_png.chmod(package_engine.FILE_MODE)

        installed_kb = max(1, sum(p.stat().st_size for p in pkg_root.rglob("*") if p.is_file()) // 1024)
        debian_dir = pkg_root / "DEBIAN"
        debian_dir.mkdir()
        debian_dir.chmod(package_engine.DIRECTORY_MODE)
        control = (
            f"Package: {DEB_PACKAGE_NAME}\n"
            f"Version: {deb_ver}\n"
            "Section: devel\n"
            "Priority: optional\n"
            f"Architecture: {architecture}\n"
            f"Installed-Size: {installed_kb}\n"
            "Depends: libc6, libgl1, libx11-6, libcurl4 | libcurl4t64\n"
            f"Maintainer: {DEB_MAINTAINER}\n"
            f"Description: {HUB_DISPLAY_NAME}\n"
            f" {HUB_DESCRIPTION}.\n"
        )
        control_file = debian_dir / "control"
        control_file.write_text(control, encoding="utf-8")
        control_file.chmod(package_engine.FILE_MODE)

        refresh_script = (
            "#!/bin/sh\n"
            "set -e\n"
            "command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true\n"
            "command -v update-mime-database >/dev/null 2>&1 && update-mime-database /usr/share/mime || true\n"
            "command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true\n"
            "exit 0\n"
        )
        for script_name in ("postinst", "postrm"):
            script_path = debian_dir / script_name
            script_path.write_text(refresh_script, encoding="utf-8")
            script_path.chmod(package_engine.EXECUTABLE_MODE)

        subprocess.run([dpkg_deb, "--root-owner-group", "--build", str(pkg_root), str(deb_path)], check=True)
    write_checksum(deb_path)
    return deb_path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", required=True, type=Path, help="UVBT output directory of the release build")
    parser.add_argument("--target", required=True, choices=sorted(package_engine.EXECUTABLE_SUFFIX))
    parser.add_argument("--tag", required=True, help="Hub release tag, e.g. hub-v0.1.0")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--skip-version-check", action="store_true")
    parser.add_argument("--deb", action="store_true",
                        help="Also build a double-clickable Debian/Ubuntu .deb package (Linux target)")
    parser.add_argument("--iscc", type=Path, help=f"Inno Setup's ISCC (Windows; default: ${ISCC_ENVIRONMENT} or the PATH)")
    args = parser.parse_args(argv)
    try:
        installer = build_installer(args.bin, args.target, args.tag, args.output, not args.skip_version_check,
                                    args.iscc)
        print(installer)
        if args.deb:
            if args.target != "linux":
                raise package_engine.PackagingError("--deb is only supported with --target linux")
            deb = build_linux_deb(args.bin, parse_hub_tag(args.tag), args.output)
            print(deb)
    except (package_engine.PackagingError, subprocess.CalledProcessError) as error:
        print(f"make_hub_installer: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
