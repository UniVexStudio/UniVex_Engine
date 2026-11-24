#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Unit tests for the platform rules in Engine/Tools/UVBT/uvbt.py."""

import io
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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


class GuiAppTargetTest(unittest.TestCase):
    def test_gui_targets_opt_into_windows_subsystem(self) -> None:
        targets_dir = Path(__file__).resolve().parents[1] / "Source" / "Targets"
        for gui_target in ("UniVexHub", "UniVexHubSetup", "UniVexEditor", "UniVexRuntime"):
            info = uvbt.CSharpParser.parse_target_cs(targets_dir / f"{gui_target}.Target.cs")
            self.assertTrue(info.is_gui_app, f"{gui_target}.Target.cs should set IsGuiApp = true")
        for cli_target in ("UniVexPack", "UniVexAssetWorkload"):
            info = uvbt.CSharpParser.parse_target_cs(targets_dir / f"{cli_target}.Target.cs")
            self.assertFalse(info.is_gui_app, f"{cli_target}.Target.cs must stay a console tool")


class GitClonePublicTest(unittest.TestCase):
    def test_clears_github_extraheader_and_runs_in_tempdir(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            dest = Path(scratch) / "googletest"
            with mock.patch.object(uvbt.subprocess, "run") as run_mock:
                uvbt.git_clone_public("https://github.com/google/googletest.git", "v1.15.2", dest)
            run_mock.assert_called_once()
            args, kwargs = run_mock.call_args
            cmd = args[0]
            self.assertIn("http.https://github.com/.extraheader=", cmd)
            self.assertIn("credential.helper=", cmd)
            self.assertEqual(kwargs["cwd"], tempfile.gettempdir())
            self.assertEqual(kwargs["env"]["GIT_TERMINAL_PROMPT"], "0")

    def test_falls_back_to_github_tag_tarball_on_clone_403(self) -> None:
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tar:
            payload = b"#pragma once\n"
            info = tarfile.TarInfo("googletest-1.15.2/googletest/include/gtest/gtest.h")
            info.size = len(payload)
            tar.addfile(info, io.BytesIO(payload))
        tar_bytes = buf.getvalue()

        class FakeResponse(io.BytesIO):
            def __enter__(self):
                return self

            def __exit__(self, *exc):
                self.close()

        with tempfile.TemporaryDirectory() as scratch:
            dest = Path(scratch) / "googletest"
            err = subprocess.CalledProcessError(128, ["git", "clone"])
            with mock.patch.object(uvbt.subprocess, "run", side_effect=err), \
                 mock.patch.object(uvbt.urllib.request, "urlopen", return_value=FakeResponse(tar_bytes)) as open_mock:
                uvbt.git_clone_public("https://github.com/google/googletest.git", "v1.15.2", dest, retries=2)
            req = open_mock.call_args[0][0]
            self.assertEqual(req.full_url, "https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz")
            self.assertTrue((dest / "googletest" / "include" / "gtest" / "gtest.h").is_file())


if __name__ == "__main__":
    unittest.main()
