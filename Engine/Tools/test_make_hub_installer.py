#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Unit tests for Engine/Tools/make_hub_installer.py."""

import hashlib
import io
import json
import struct
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import make_hub_installer
import package_engine


# Stands in for Inno Setup's ISCC: records the /D defines and the staged files, writes the installer.
FAKE_ISCC = """#!{python}
import json, os, sys
from pathlib import Path
defines = dict(arg[2:].split("=", 1) for arg in sys.argv[1:] if arg.startswith("/D"))
stage = Path(defines["StageDir"])
defines["staged"] = sorted(p.relative_to(stage).as_posix() for p in stage.rglob("*") if p.is_file())
defines["script"] = sys.argv[-1]
output = Path(defines["OutputDir"]) / (defines["OutputName"] + ".exe")
output.write_bytes(b"WIZARD")
(Path(defines["OutputDir"]) / "iscc.json").write_text(json.dumps(defines))
"""


def make_hub_build(root: Path, suffix: str = "") -> Path:
    bin_dir = root / "bin"
    bin_dir.mkdir()
    (bin_dir / f"uve_hub{suffix}").write_text("hub")
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

    def test_windows_installer_is_the_inno_setup_wizard(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            iscc = root / "iscc"
            iscc.write_text(FAKE_ISCC.format(python=sys.executable))
            iscc.chmod(0o755)
            # The DLL scan needs MinGW's objdump; the staging around it is what this test checks.
            with mock.patch.object(package_engine, "copy_windows_dlls"):
                installer = make_hub_installer.build_installer(make_hub_build(root, ".exe"), "windows", "hub-v1.2.0",
                                                               root / "dist", check_version=False, iscc=iscc)
            self.assertEqual(installer.name, "UniVexHubSetup-1.2.0-windows.exe")
            self.assertEqual(installer.read_bytes(), b"WIZARD")
            self.assertTrue(installer.with_name(installer.name + package_engine.CHECKSUM_SUFFIX).is_file())
            defines = json.loads((root / "dist" / "iscc.json").read_text())
            self.assertEqual(Path(defines["script"]), make_hub_installer.WINDOWS_INSTALLER_SCRIPT)
            self.assertEqual(defines["AppVersion"], "1.2.0")
            self.assertEqual(defines["NumericVersion"], "1.2.0.0")
            self.assertEqual(defines["HubExe"], "uve_hub.exe")
            self.assertEqual(defines["ProjectExtension"], ".uvproject")
            for expected in ("uve_hub.exe", "engine_feed.json", "project_templates/file.txt", "icons/univex-logo.ico"):
                self.assertIn(expected, defines["staged"])
            self.assertNotIn("uve_hub_setup.exe", defines["staged"])

    def test_wizard_script_uses_only_defines_it_is_given(self) -> None:
        import re
        script = make_hub_installer.WINDOWS_INSTALLER_SCRIPT.read_text(encoding="utf-8")
        used = set(re.findall(r"\{#(\w+)\}", script))
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            iscc = root / "iscc"
            iscc.write_text(FAKE_ISCC.format(python=sys.executable))
            iscc.chmod(0o755)
            with mock.patch.object(package_engine, "copy_windows_dlls"):
                make_hub_installer.build_installer(make_hub_build(root, ".exe"), "windows", "hub-v1.2.0",
                                                   root / "dist", check_version=False, iscc=iscc)
            given = set(json.loads((root / "dist" / "iscc.json").read_text()))
        self.assertFalse(used - given, f"the script uses defines make_hub_installer does not pass: {used - given}")


if __name__ == "__main__":
    unittest.main()
