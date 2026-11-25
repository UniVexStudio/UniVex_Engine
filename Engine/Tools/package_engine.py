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
import struct
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
LINUX_LIB_DIRECTORY = "lib"
EXECUTABLE_SUFFIX = {
    "linux": "",
    "windows": ".exe",
    "android": "",
    "android-arm64-v8a": "",
    "android-x86_64": "",
}
# Android: an app runs no second program, so the package carries the editor as a shared library
# loaded via dlopen (engine_install.h, kEditorAndroidEntryPoint), plus the runtime library when
# built alongside it. UVBT names it lib<name>.so; the package keeps the editor's own name so
# installs resolve the same way on every platform.
ANDROID_TARGET = "android"
ANDROID_ABI_TARGETS = {
    "android": "arm64-v8a",
    "android-arm64-v8a": "arm64-v8a",
    "android-x86_64": "x86_64",
}
ANDROID_ELF_MACHINES = {
    "arm64-v8a": 183,  # EM_AARCH64
    "x86_64": 62,      # EM_X86_64
}
ANDROID_EDITOR_LIBRARY = f"lib{EDITOR_STEM}.so"
ANDROID_RUNTIME_LIBRARY = "libunivex_android.so"
# The engine's built-in shader sources, read from files at run time (as the Android APKs ship them).
SHADERS_DIRECTORY = "shaders"
SHADERS_SOURCE = ("Engine", "Runtime", "RHI", "Shader", "built_in")
REPO_ROOT = Path(__file__).resolve().parents[2]
TAG_PATTERN = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)(?:\+build\.(\d+)|\.(\d+))?$")
FILE_MODE = 0o644
EXECUTABLE_MODE = 0o755
# Android 14+/17+ Safer DCL requires dynamically loaded libraries to be non-writable.
ANDROID_LIBRARY_MODE = 0o555
DIRECTORY_MODE = 0o755
CHUNK_BYTES = 1 << 16

# Base Linux OS, C/C++ runtime, display-server, GPU-driver and TLS libraries that must always come
# from the host system rather than being bundled inside a portable Linux archive.
LINUX_SYSTEM_LIB_PREFIXES = (
    "linux-vdso.so",
    "ld-linux",
    "libc.so",
    "libm.so",
    "libdl.so",
    "libpthread.so",
    "librt.so",
    "libresolv.so",
    "libutil.so",
    "libanl.so",
    "libnsl.so",
    "libnss_",
    "libstdc++.so",
    "libgcc_s.so",
    "libGL.so",
    "libGLX.so",
    "libGLdispatch.so",
    "libOpenGL.so",
    "libEGL.so",
    "libGLESv",
    "libvulkan.so",
    "libdrm.so",
    "libgbm.so",
    "libX11.so",
    "libX11-xcb.so",
    "libXext.so",
    "libXau.so",
    "libXdmcp.so",
    "libXrandr.so",
    "libXrender.so",
    "libXfixes.so",
    "libXcursor.so",
    "libXinerama.so",
    "libXi.so",
    "libXxf86vm.so",
    "libxcb.so",
    "libxcb-",
    "libwayland-",
    "libxkbcommon.so",
    "libdbus-1.so",
    "libasound.so",
    "libpulse",
    "libcurl.so",
    "libssl.so",
    "libcrypto.so",
    "libz.so",
)


class PackagingError(RuntimeError):
    """A package cannot be built from the given inputs."""


def is_android_target(target: str) -> bool:
    return target in ANDROID_ABI_TARGETS


def is_elf_file(path: Path) -> bool:
    try:
        with path.open("rb") as handle:
            return handle.read(4) == b"\x7fELF"
    except OSError:
        return False


def validate_android_elf(path: Path, abi: str) -> None:
    """Verifies that a real ELF binary matches the expected Android ABI e_machine."""
    with path.open("rb") as handle:
        header = handle.read(20)
    if len(header) < 20 or header[:4] != b"\x7fELF":
        return
    (machine,) = struct.unpack("<H", header[18:20])
    expected = ANDROID_ELF_MACHINES[abi]
    if machine != expected:
        raise PackagingError(
            f"{path.name} has ELF machine {machine}, expected {expected} for Android ABI {abi}"
        )


def is_linux_system_lib(soname: str) -> bool:
    return any(soname.startswith(prefix) for prefix in LINUX_SYSTEM_LIB_PREFIXES)


def _readelf_needed(path: Path) -> list[str]:
    output = subprocess.check_output(["readelf", "-d", str(path)], text=True)
    return re.findall(r"\(NEEDED\)\s+Shared library:\s+\[([^\]]+)\]", output)


def _ldd_resolved(path: Path) -> dict[str, Path | None]:
    output = subprocess.check_output(["ldd", str(path)], text=True)
    resolved: dict[str, Path | None] = {}
    for line in output.splitlines():
        match = re.match(r"^\s*(\S+)\s+=>\s+(.+?)(?:\s+\(0x[0-9a-fA-F]+\))?$", line)
        if not match:
            continue
        soname, target = match.group(1), match.group(2).strip()
        if target == "not found":
            resolved[soname] = None
        else:
            resolved[soname] = Path(target)
    return resolved


def copy_linux_shared_libs(stage: Path) -> set[str]:
    """Bundles non-system shared libraries needed by ELF executables in `stage` under `stage/lib`."""
    elf_binaries = [p for p in sorted(stage.iterdir()) if p.is_file() and is_elf_file(p)]
    if not elf_binaries:
        return set()
    readelf = shutil.which("readelf")
    ldd = shutil.which("ldd")
    if readelf is None or ldd is None:
        raise PackagingError("readelf and ldd are needed to bundle Linux shared libraries")

    lib_dir = stage / LINUX_LIB_DIRECTORY
    bundled: dict[str, Path] = {}
    pending: list[Path] = list(elf_binaries)
    while pending:
        binary = pending.pop()
        needed = _readelf_needed(binary)
        resolved = _ldd_resolved(binary)
        for soname in needed:
            if is_linux_system_lib(soname) or soname in bundled:
                continue
            if soname not in resolved or resolved[soname] is None:
                raise PackagingError(f"{binary.name} requires {soname}, which could not be resolved via ldd")
            source = resolved[soname].resolve()
            if not source.is_file():
                raise PackagingError(f"{binary.name} resolved {soname} to missing file {source}")
            lib_dir.mkdir(exist_ok=True)
            dest = lib_dir / soname
            shutil.copy2(source, dest, follow_symlinks=True)
            bundled[soname] = dest
            pending.append(dest)

    patchelf = shutil.which("patchelf")
    if bundled and patchelf is not None:
        for dest in bundled.values():
            subprocess.run([patchelf, "--force-rpath", "--set-rpath", "$ORIGIN", str(dest)], check=True)
        for binary in elf_binaries:
            subprocess.run([patchelf, "--force-rpath", "--set-rpath", "$ORIGIN/lib:$ORIGIN", str(binary)], check=True)
    return set(bundled.keys())


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
    """Copies the named executables and directories of a build into `stage`, with runtime libs on Windows/Linux."""
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
    elif target == "linux":
        copy_linux_shared_libs(stage)


def stage_shaders(stage: Path) -> None:
    shaders_src = REPO_ROOT.joinpath(*SHADERS_SOURCE)
    if not shaders_src.is_dir():
        raise PackagingError(f"missing built-in shaders directory {shaders_src}")
    shutil.copytree(shaders_src, stage / SHADERS_DIRECTORY, symlinks=False)


def stage_android_editor(bin_dir: Path, target: str, stage: Path, require_runtime: bool = False) -> None:
    """The Android package: the editor library under the editor's name, optional runtime library, PRISM assets and shaders."""
    abi = ANDROID_ABI_TARGETS[target]
    library = bin_dir / ANDROID_EDITOR_LIBRARY
    if not library.is_file():
        raise PackagingError(f"missing editor library {library}")
    validate_android_elf(library, abi)
    shutil.copy2(library, stage / EDITOR_STEM)

    runtime_lib = bin_dir / ANDROID_RUNTIME_LIBRARY
    if runtime_lib.is_file():
        validate_android_elf(runtime_lib, abi)
        shutil.copy2(runtime_lib, stage / ANDROID_RUNTIME_LIBRARY)
    elif require_runtime:
        raise PackagingError(f"missing Android runtime library {runtime_lib}")

    for directory in DATA_DIRECTORIES:
        source = bin_dir / directory
        if not source.is_dir():
            raise PackagingError(f"missing directory {source}")
        shutil.copytree(source, stage / directory, symlinks=False)
    stage_shaders(stage)


def stage_engine(bin_dir: Path, target: str, version: str, channel: str, stage: Path,
                 require_runtime: bool = False) -> None:
    if is_android_target(target):
        stage_android_editor(bin_dir, target, stage, require_runtime=require_runtime)
    else:
        stage_files(bin_dir, target, EXECUTABLE_STEMS, DATA_DIRECTORIES, stage)
        stage_shaders(stage)
    manifest = {
        "format": MANIFEST_FORMAT,
        "schemaVersion": MANIFEST_SCHEMA_VERSION,
        "version": version,
        "channel": channel,
        "target": target,
    }
    (stage / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def write_archive(stage: Path, archive: Path, executables: set[str],
                  readonly_executables: set[str] | None = None) -> None:
    readonly = readonly_executables or set()

    def normalise(info: tarfile.TarInfo) -> tarfile.TarInfo:
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        if info.isdir():
            info.mode = DIRECTORY_MODE
        else:
            name = Path(info.name).name
            if name in readonly:
                info.mode = ANDROID_LIBRARY_MODE
            elif name in executables:
                info.mode = EXECUTABLE_MODE
            else:
                info.mode = FILE_MODE
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


def build_package(bin_dir: Path, target: str, tag: str, output: Path, check_version: bool = True,
                  require_runtime: bool = False) -> Path:
    version, channel = parse_tag(tag)
    editor = bin_dir / f"{EDITOR_STEM}{EXECUTABLE_SUFFIX[target]}"
    if check_version and is_android_target(target):
        raise PackagingError("an Android editor library cannot report its version here; pass --skip-version-check")
    if check_version:
        reported = built_version(editor)
        if reported != version:
            raise PackagingError(f"tag {tag} names engine {version}, but the build is engine {reported}")
    output.mkdir(parents=True, exist_ok=True)
    archive = output / package_name(version, target)
    executables = {f"{stem}{EXECUTABLE_SUFFIX[target]}" for stem in EXECUTABLE_STEMS}
    readonly_executables = {EDITOR_STEM, ANDROID_RUNTIME_LIBRARY} if is_android_target(target) else set()
    with tempfile.TemporaryDirectory() as scratch:
        stage = Path(scratch) / "engine"
        stage.mkdir()
        stage_engine(bin_dir, target, version, channel, stage, require_runtime=require_runtime)
        executables.update(p.name for p in stage.glob("*.dll"))
        if (stage / LINUX_LIB_DIRECTORY).is_dir():
            executables.update(p.name for p in (stage / LINUX_LIB_DIRECTORY).iterdir() if p.is_file())
        write_archive(stage, archive, executables, readonly_executables)
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
    parser.add_argument("--require-runtime", action="store_true",
                        help="fail if libunivex_android.so is absent from an Android build directory")
    args = parser.parse_args(argv)
    try:
        archive = build_package(args.bin, args.target, args.tag, args.output, not args.skip_version_check,
                                require_runtime=args.require_runtime)
    except (PackagingError, subprocess.CalledProcessError) as error:
        print(f"package_engine: {error}", file=sys.stderr)
        return 1
    print(archive)
    return 0


if __name__ == "__main__":
    sys.exit(main())
