#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Unit tests for Engine/Tools/make_hub_installer.py."""

import hashlib
import io
import struct
import tarfile
import tempfile
import unittest
from pathlib import Path

import make_hub_installer
import package_engine


def make_hub_build(root: Path) -> Path:
    bin_dir = root / "bin"
    bin_dir.mkdir()
    (bin_dir / "uve_hub").write_text("hub")
    (bin_dir / "uve_hub_setup").write_bytes(b"SETUP-PROGRAM")
    (bin_dir / "engine_feed.json").write_text("{}")
    for directory in make_hub_installer.HUB_DIRECTORIES:
        (bin_dir / directory).mkdir()
        (bin_dir / directory / "file.txt").write_text("data")
    return bin_dir


class MakeHubInstallerTest(unittest.TestCase):
    def test_parse_hub_tag(self) -> None:
        self.assertEqual(make_hub_installer.parse_hub_tag("hub-v1.2.0"), "1.2.0+build.0")
        with self.assertRaises(package_engine.PackagingError):
            make_hub_installer.parse_hub_tag("v1.2.0")

    def test_payload_is_the_hub_alone_with_footer(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            installer = make_hub_installer.build_installer(make_hub_build(root), "linux", "hub-v1.0.0", root / "dist",
                                                           check_version=False)
            self.assertEqual(installer.name, "UniVexHubSetup-1.0.0-linux")
            data = installer.read_bytes()
            self.assertTrue(data.startswith(b"SETUP-PROGRAM"))
            self.assertEqual(data[-8:], make_hub_installer.PAYLOAD_MAGIC)
            (size,) = struct.unpack(make_hub_installer.SIZE_FORMAT, data[-16:-8])
            digest = data[-48:-16]
            payload = data[-48 - size:-48]
            self.assertEqual(hashlib.sha256(payload).digest(), digest)
            with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as tar:
                names = set(tar.getnames())
            for expected in ("uve_hub", "engine_feed.json", "project_templates/file.txt", "icons/univex-logo.ico",
                             "icons/univex-logo-256.png"):
                self.assertIn(expected, names)
            self.assertNotIn("uve_editor", names)
            self.assertNotIn("engine.json", names)


if __name__ == "__main__":
    unittest.main()
