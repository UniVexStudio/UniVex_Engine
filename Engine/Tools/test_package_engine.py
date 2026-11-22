#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Unit tests for Engine/Tools/package_engine.py."""

import hashlib
import json
import tarfile
import tempfile
import unittest
from pathlib import Path

import package_engine


def make_build(root: Path) -> Path:
    bin_dir = root / "bin"
    bin_dir.mkdir()
    for stem in package_engine.EXECUTABLE_STEMS:
        (bin_dir / stem).write_text("binary")
    for directory in package_engine.DATA_DIRECTORIES:
        (bin_dir / directory / "nested").mkdir(parents=True)
        (bin_dir / directory / "nested" / "file.txt").write_text("data")
    return bin_dir


class PackageEngineTest(unittest.TestCase):
    def test_parse_tag(self) -> None:
        self.assertEqual(package_engine.parse_tag("v1.2.3"), ("1.2.3+build.0", "stable"))
        self.assertEqual(package_engine.parse_tag("v1.2.3+build.4"), ("1.2.3+build.4", "stable"))
        self.assertEqual(package_engine.parse_tag("v1.2.3.4-beta"), ("1.2.3+build.4", "beta"))
        with self.assertRaises(package_engine.PackagingError):
            package_engine.parse_tag("release-1")

    def test_builds_archive_manifest_and_checksum(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            archive = package_engine.build_package(make_build(root), "linux", "v0.3.0-beta", root / "dist",
                                                   check_version=False)
            self.assertEqual(archive.name, "univex-engine-0.3.0+build.0-linux.uvpackage")
            with tarfile.open(archive, "r:gz") as tar:
                members = {member.name: member for member in tar.getmembers()}
                manifest = json.loads(tar.extractfile("engine.json").read())
            self.assertEqual(manifest["version"], "0.3.0+build.0")
            self.assertEqual(manifest["channel"], "beta")
            self.assertEqual(manifest["target"], "linux")
            self.assertTrue(members["uve_editor"].mode & 0o111)
            self.assertFalse(members["prism_assets/nested/file.txt"].mode & 0o111)
            self.assertTrue(any(name.startswith("shaders/") for name in members))
            self.assertNotIn("project_templates", {name.split("/")[0] for name in members})
            self.assertTrue(all(m.isfile() or m.isdir() for m in members.values()))
            checksum = archive.with_name(archive.name + ".sha256").read_text().split()[0]
            self.assertEqual(checksum, hashlib.sha256(archive.read_bytes()).hexdigest())

    def test_android_package_is_the_editor_library(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            bin_dir = root / "bin"
            bin_dir.mkdir()
            (bin_dir / package_engine.ANDROID_EDITOR_LIBRARY).write_bytes(b"\x7fELF")
            for directory in package_engine.DATA_DIRECTORIES:
                (bin_dir / directory).mkdir()
                (bin_dir / directory / "file.txt").write_text("data")
            with self.assertRaises(package_engine.PackagingError):
                package_engine.build_package(bin_dir, "android", "v0.3.0-beta", root / "dist")
            archive = package_engine.build_package(bin_dir, "android", "v0.3.0-beta", root / "dist",
                                                   check_version=False)
            self.assertEqual(archive.name, "univex-engine-0.3.0+build.0-android.uvpackage")
            with tarfile.open(archive, "r:gz") as tar:
                members = {member.name: member for member in tar.getmembers()}
                manifest = json.loads(tar.extractfile("engine.json").read())
            self.assertEqual(manifest["target"], "android")
            self.assertIn("uve_editor", members)
            self.assertEqual(members["uve_editor"].mode, package_engine.ANDROID_LIBRARY_MODE)
            self.assertIn("prism_assets/file.txt", members)
            self.assertTrue(any(name.startswith("shaders/") for name in members))
            self.assertNotIn("uve_runtime", members)
            self.assertNotIn(package_engine.ANDROID_EDITOR_LIBRARY, members)

    def test_android_abi_targets_validate_elf_and_bundle_runtime(self) -> None:
        import struct

        def fake_elf(machine: int) -> bytes:
            return b"\x7fELF" + (b"\x00" * 14) + struct.pack("<H", machine) + (b"\x00" * 44)

        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            bin_dir = root / "bin"
            bin_dir.mkdir()
            (bin_dir / package_engine.ANDROID_EDITOR_LIBRARY).write_bytes(fake_elf(183))  # EM_AARCH64
            for directory in package_engine.DATA_DIRECTORIES:
                (bin_dir / directory).mkdir()
                (bin_dir / directory / "file.txt").write_text("data")

            # Missing runtime fails when require_runtime=True.
            with self.assertRaises(package_engine.PackagingError):
                package_engine.build_package(bin_dir, "android-arm64-v8a", "v0.3.0-beta", root / "dist",
                                             check_version=False, require_runtime=True)

            # Wrong ABI ELF is rejected.
            with self.assertRaises(package_engine.PackagingError):
                package_engine.build_package(bin_dir, "android-x86_64", "v0.3.0-beta", root / "dist",
                                             check_version=False)

            (bin_dir / package_engine.ANDROID_RUNTIME_LIBRARY).write_bytes(fake_elf(183))
            archive = package_engine.build_package(bin_dir, "android-arm64-v8a", "v0.3.0-beta", root / "dist",
                                                   check_version=False, require_runtime=True)
            self.assertEqual(archive.name, "univex-engine-0.3.0+build.0-android-arm64-v8a.uvpackage")
            with tarfile.open(archive, "r:gz") as tar:
                members = {member.name: member for member in tar.getmembers()}
                manifest = json.loads(tar.extractfile("engine.json").read())
            self.assertEqual(manifest["target"], "android-arm64-v8a")
            self.assertEqual(members["uve_editor"].mode, 0o555)
            self.assertEqual(members[package_engine.ANDROID_RUNTIME_LIBRARY].mode, 0o555)

    def test_linux_system_lib_filter(self) -> None:
        for system_lib in ("libc.so.6", "libGL.so.1", "libX11.so.6", "libstdc++.so.6", "libcurl.so.4", "libz.so.1"):
            self.assertTrue(package_engine.is_linux_system_lib(system_lib), system_lib)
        for bundle_lib in ("libglfw.so.3", "libGLEW.so.2.2", "libjpeg.so.8", "libjpeg.so.62", "libfreetype.so.6",
                           "libpng16.so.16"):
            self.assertFalse(package_engine.is_linux_system_lib(bundle_lib), bundle_lib)

    def test_display_version(self) -> None:
        self.assertEqual(package_engine.display_version("1.2.3+build.0"), "1.2.3")
        self.assertEqual(package_engine.display_version("1.2.3+build.4"), "1.2.3+build.4")

    def test_missing_executable_fails(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            bin_dir = make_build(root)
            (bin_dir / "uve_pack").unlink()
            with self.assertRaises(package_engine.PackagingError):
                package_engine.build_package(bin_dir, "linux", "v1.0.0", root / "dist", check_version=False)


if __name__ == "__main__":
    unittest.main()
