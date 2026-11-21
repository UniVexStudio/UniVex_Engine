#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Packages a built engine for the Hub's engine feed.

The package is a gzip-compressed ustar archive holding the engine's executables and the PRISM
assets, with an engine.json manifest at its root. Project templates ship with the Hub, not here. Beside it goes a .sha256 file with
the archive's digest. The names and the manifest match what the Hub reads (Engine/Runtime/Online):

    univex-engine-<version>-<target>.uvpackage          (a gzip-compressed tar archive)
    univex-engine-<version>-<target>.uvpackage.sha256

Only regular files and directories are archived, because the Hub refuses anything else.

    python3 Engine/Tools/package_engine.py --bin .uvbt/bin/Linux/Release --target linux \\
        --tag v0.2.0 --output dist
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

PACKAGE_PREFIX = "univex-engine-"
PACKAGE_EXTENSION = ".uvpackage"  # a gzip-compressed tar archive
CHECKSUM_SUFFIX = ".sha256"
MANIFEST_NAME = "engine.json"
MANIFEST_FORMAT = "uvengine"
MANIFEST_SCHEMA_VERSION = 1
BETA_TAG_SUFFIX = "-beta"
EDITOR_STEM = "uve_editor"
RUNTIME_STEM = "uve_runtime"
PACK_STEM = "uve_pack"
EXECUTABLE_STEMS = (EDITOR_STEM, RUNTIME_STEM, PACK_STEM)
PRISM_ASSETS_DIRECTORY = "prism_assets"
DATA_DIRECTORIES = (PRISM_ASSETS_DIRECTORY,)
EXECUTABLE_SUFFIX = {"linux": "", "windows": ".exe"}
TAG_PATTERN = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)(?:\+build\.(\d+)|\.(\d+))?$")
FILE_MODE = 0o644
EXECUTABLE_MODE = 0o755
DIRECTORY_MODE = 0o755
CHUNK_BYTES = 1 << 16


class PackagingError(RuntimeError):
    """A package cannot be built from the given inputs."""


def parse_tag(tag: str) -> tuple[str, str]:
    """Returns the engine version string ("MAJOR.MINOR.PATCH+build.BUILD") and channel a tag names."""
    channel = "stable"
    if tag.endswith(BETA_TAG_SUFFIX):
        channel = "beta"
        tag = tag[: -len(BETA_TAG_SUFFIX)]
    match = TAG_PATTERN.match(tag)
    if not match:
        raise PackagingError(f"'{tag}' is not a release tag such as v1.2.3, v1.2.3+build.4 or v1.2.3-beta")
    major, minor, patch, build_a, build_b = match.groups()
    build = build_a or build_b or "0"
    return f"{int(major)}.{int(minor)}.{int(patch)}+build.{int(build)}", channel


def display_version(version: str) -> str:
    """"MAJOR.MINOR.PATCH", keeping "+build.N" only when N is not 0 (as the Hub shows versions)."""
    core, _, build = version.partition("+build.")
    return core if build in ("", "0") else version


def package_name(version: str, target: str) -> str:
    return f"{PACKAGE_PREFIX}{version}-{target}{PACKAGE_EXTENSION}"


def built_version(program: Path) -> str:
    """The version a built program reports, so a package can never disagree with its binaries."""
    result = subprocess.run([str(program), "--version"], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise PackagingError(f"{program} --version failed: {result.stderr.strip()}")
    return result.stdout.strip()


def copy_windows_dlls(stage: Path) -> None:
    """Copies every non-system DLL the executables import, found beside the compiler."""
    compiler = shutil.which("g++")
    if compiler is None:
        raise PackagingError("g++ is needed to locate the DLLs a Windows package carries")
    library_dir = Path(compiler).parent
    pending = list(stage.glob("*.exe"))
    copied: set[str] = set()
    while pending:
        binary = pending.pop()
        imports = subprocess.check_output(["objdump", "-p", str(binary)], text=True)
        for name in re.findall(r"DLL Name:\s*(\S+)", imports):
            source = library_dir / name
            if name.lower() in copied or not source.is_file():
                continue
            copied.add(name.lower())
            target = stage / name
            shutil.copy2(source, target)
            pending.append(target)


def stage_files(bin_dir: Path, target: str, stems: tuple[str, ...], directories: tuple[str, ...], stage: Path) -> None:
    """Copies the named executables and directories of a build into `stage`, with their DLLs on Windows."""
    suffix = EXECUTABLE_SUFFIX[target]
    for stem in stems:
        source = bin_dir / f"{stem}{suffix}"
        if not source.is_file():
            raise PackagingError(f"missing executable {source}")
        shutil.copy2(source, stage / source.name)
    for directory in directories:
        source = bin_dir / directory
        if not source.is_dir():
            raise PackagingError(f"missing directory {source}")
        shutil.copytree(source, stage / directory, symlinks=False)
    if target == "windows":
        copy_windows_dlls(stage)


def stage_engine(bin_dir: Path, target: str, version: str, channel: str, stage: Path) -> None:
    stage_files(bin_dir, target, EXECUTABLE_STEMS, DATA_DIRECTORIES, stage)
    manifest = {
        "format": MANIFEST_FORMAT,
        "schemaVersion": MANIFEST_SCHEMA_VERSION,
        "version": version,
        "channel": channel,
        "target": target,
    }
    (stage / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def write_archive(stage: Path, archive: Path, executables: set[str]) -> None:
    def normalise(info: tarfile.TarInfo) -> tarfile.TarInfo:
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        if info.isdir():
            info.mode = DIRECTORY_MODE
        else:
            info.mode = EXECUTABLE_MODE if Path(info.name).name in executables else FILE_MODE
        return info

    with tarfile.open(archive, "w:gz", format=tarfile.USTAR_FORMAT) as tar:
        for path in sorted(stage.rglob("*")):
            if path.is_symlink() or not (path.is_file() or path.is_dir()):
                raise PackagingError(f"{path} is neither a regular file nor a directory")
            tar.add(path, arcname=path.relative_to(stage).as_posix(), recursive=False, filter=normalise)


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_package(bin_dir: Path, target: str, tag: str, output: Path, check_version: bool = True) -> Path:
    version, channel = parse_tag(tag)
    editor = bin_dir / f"{EDITOR_STEM}{EXECUTABLE_SUFFIX[target]}"
    if check_version:
        reported = built_version(editor)
        if reported != version:
            raise PackagingError(f"tag {tag} names engine {version}, but the build is engine {reported}")
    output.mkdir(parents=True, exist_ok=True)
    archive = output / package_name(version, target)
    executables = {f"{stem}{EXECUTABLE_SUFFIX[target]}" for stem in EXECUTABLE_STEMS}
    with tempfile.TemporaryDirectory() as scratch:
        stage = Path(scratch) / "engine"
        stage.mkdir()
        stage_engine(bin_dir, target, version, channel, stage)
        executables.update(p.name for p in stage.glob("*.dll"))
        write_archive(stage, archive, executables)
    checksum = archive.with_name(archive.name + CHECKSUM_SUFFIX)
    checksum.write_text(f"{sha256_of(archive)}  {archive.name}\n", encoding="utf-8")
    return archive


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", required=True, type=Path, help="UVBT output directory of the release build")
    parser.add_argument("--target", required=True, choices=sorted(EXECUTABLE_SUFFIX), help="platform of the build")
    parser.add_argument("--tag", required=True, help="release tag, e.g. v0.2.0 or v0.2.0-beta")
    parser.add_argument("--output", required=True, type=Path, help="directory for the package and checksum")
    parser.add_argument("--skip-version-check", action="store_true",
                        help="do not run the editor to compare its version with the tag (cross builds)")
    args = parser.parse_args(argv)
    try:
        archive = build_package(args.bin, args.target, args.tag, args.output, not args.skip_version_check)
    except (PackagingError, subprocess.CalledProcessError) as error:
        print(f"package_engine: {error}", file=sys.stderr)
        return 1
    print(archive)
    return 0


if __name__ == "__main__":
    sys.exit(main())
