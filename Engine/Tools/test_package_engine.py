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
            self.assertEqual(archive.name, "univex-engine-0.3.0+build.0-linux.tar.gz")
            with tarfile.open(archive, "r:gz") as tar:
                members = {member.name: member for member in tar.getmembers()}
                manifest = json.loads(tar.extractfile("engine.json").read())
            self.assertEqual(manifest["version"], "0.3.0+build.0")
            self.assertEqual(manifest["channel"], "beta")
            self.assertEqual(manifest["target"], "linux")
            self.assertTrue(members["uve_editor"].mode & 0o111)
            self.assertFalse(members["prism_assets/nested/file.txt"].mode & 0o111)
            self.assertNotIn("project_templates", {name.split("/")[0] for name in members})
            self.assertTrue(all(m.isfile() or m.isdir() for m in members.values()))
            checksum = archive.with_name(archive.name + ".sha256").read_text().split()[0]
            self.assertEqual(checksum, hashlib.sha256(archive.read_bytes()).hexdigest())

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
