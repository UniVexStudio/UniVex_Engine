#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Unit tests for the platform rules in Engine/Tools/UVBT/uvbt.py."""

import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "UVBT"))

import uvbt  # noqa: E402


class SelectForPlatformTest(unittest.TestCase):
    ENTRIES = ["z", "linux:X11", "win:ole32", "android:log", "desktop:UVE_HAS_DESKTOP_GL=1", "Internal/a:b.cpp"]

    def test_each_platform_keeps_its_own_entries(self) -> None:
        self.assertEqual(uvbt.select_for_platform(self.ENTRIES, "Linux"),
                         ["z", "X11", "UVE_HAS_DESKTOP_GL=1", "Internal/a:b.cpp"])
        self.assertEqual(uvbt.select_for_platform(self.ENTRIES, "Windows"),
                         ["z", "ole32", "UVE_HAS_DESKTOP_GL=1", "Internal/a:b.cpp"])
        self.assertEqual(uvbt.select_for_platform(self.ENTRIES, "Android"), ["z", "log", "Internal/a:b.cpp"])

    def test_build_cs_reads_platform_rules(self) -> None:
        window = Path(__file__).resolve().parents[1] / "Runtime" / "Window" / "Window.Build.cs"
        linux = {p.name for p in uvbt.CSharpParser.parse_build_cs(window, "Linux").source_files}
        android = {p.name for p in uvbt.CSharpParser.parse_build_cs(window, "Android").source_files}
        self.assertIn("window_manager.cpp", linux)
        self.assertNotIn("android_window_manager.cpp", linux)
        self.assertIn("android_window_manager.cpp", android)
        self.assertNotIn("window_manager.cpp", android)


class FindNdkTest(unittest.TestCase):
    def make_ndk(self, root: Path, revision: str) -> Path:
        ndk = root / revision
        ndk.mkdir(parents=True)
        (ndk / "source.properties").write_text(f"Pkg.Desc = Android NDK\nPkg.Revision = {revision}\n")
        return ndk

    def test_ignores_another_release_named_by_the_environment(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            other = self.make_ndk(Path(scratch) / "other", "27.3.13750724")
            sdk = Path(scratch) / "sdk"
            pinned = self.make_ndk(sdk / "ndk", uvbt.ANDROID_NDK_VERSION)
            saved = dict(os.environ)
            try:
                os.environ.update(ANDROID_NDK_HOME=str(other), ANDROID_HOME=str(sdk))
                os.environ.pop("ANDROID_NDK_ROOT", None)
                os.environ.pop("ANDROID_SDK_ROOT", None)
                self.assertEqual(uvbt.Toolchain._find_ndk(), pinned)
                os.environ["ANDROID_HOME"] = str(Path(scratch) / "empty")
                with self.assertRaises(RuntimeError):
                    uvbt.Toolchain._find_ndk()
            finally:
                os.environ.clear()
                os.environ.update(saved)


if __name__ == "__main__":
    unittest.main()
