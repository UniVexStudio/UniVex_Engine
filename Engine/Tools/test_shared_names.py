#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Checks that the names the Python tools write match the names the engine reads.

The packaging tools, the build tool and the release workflow lay files out for the Hub, the setup
program and the editor to find. Each side keeps its own constant; these tests read the C++ headers,
the build targets and the engine feed, and fail when the two sides drift apart.
"""

import json
import re
import sys
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parents[1]
sys.path.insert(0, str(TOOLS_DIR / "UVBT"))

import package_engine  # noqa: E402
import uvbt  # noqa: E402

HEADERS = (
    "Engine/Runtime/Online/Expose/engine_release_feed.h",
    "Engine/Runtime/Online/Expose/engine_library.h",
    "Engine/Runtime/Online/Expose/tar_gz_archive.h",
    "Engine/Runtime/Pack/Expose/engine_install.h",
    "Engine/Runtime/Pack/Expose/project_template.h",
    "Engine/Runtime/Platform/Expose/editor_project_package.h",
)
TARGETS_DIR = "Engine/Source/Targets"
ENGINE_FEED = "Engine/Distribution/engine_feed.json"
ANDROID_ABI_CONTRACT_HEADER = "contracts/v2/univex_android_editor_abi.h"

# `inline constexpr <type> kName = <value>;` where the value is a string, an unsigned integer, or
# the name of another constant.
CONSTANT_PATTERN = re.compile(
    r"inline\s+constexpr\s+[\w:<>]+\s+(k[A-Z]\w*)\s*=\s*(\"(?:[^\"\\]|\\.)*\"|\d+U?|k[A-Z]\w*)\s*;"
)
OUTPUT_NAME_PATTERN = re.compile(r"OutputName\s*=\s*\"([^\"]+)\"")


def read_cpp_constants() -> dict[str, object]:
    raw: dict[str, str] = {}
    for header in HEADERS:
        text = (REPO_ROOT / header).read_text(encoding="utf-8")
        for name, value in CONSTANT_PATTERN.findall(text):
            raw[name] = value

    def resolve(name: str, seen: frozenset[str] = frozenset()) -> object:
        if name in seen:
            raise ValueError(f"{name} refers to itself")
        value = raw[name]
        if value.startswith('"'):
            return json.loads(value)
        if value.startswith("k"):
            return resolve(value, seen | {name})
        return int(value.rstrip("U"))

    return {name: resolve(name) for name in raw}


def read_target_outputs() -> dict[str, str]:
    outputs: dict[str, str] = {}
    for target in sorted((REPO_ROOT / TARGETS_DIR).glob("*.Target.cs")):
        match = OUTPUT_NAME_PATTERN.search(target.read_text(encoding="utf-8"))
        if match:
            outputs[target.name.removesuffix(".Target.cs")] = match.group(1)
    return outputs


class SharedNamesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cpp = read_cpp_constants()
        cls.outputs = read_target_outputs()

    def assert_cpp(self, name: str, python_value: object) -> None:
        self.assertIn(name, self.cpp, f"{name} is no longer declared in the headers this test reads")
        self.assertEqual(self.cpp[name], python_value, f"{name} differs between C++ and Python")

    def test_engine_package_names(self) -> None:
        self.assert_cpp("kEnginePackagePrefix", package_engine.PACKAGE_PREFIX)
        self.assert_cpp("kEnginePackageExtension", package_engine.PACKAGE_EXTENSION)
        self.assert_cpp("kEngineChecksumSuffix", package_engine.CHECKSUM_SUFFIX)
        self.assert_cpp("kBetaTagSuffix", package_engine.BETA_TAG_SUFFIX)

    def test_engine_manifest(self) -> None:
        self.assert_cpp("kEngineManifestFileName", package_engine.MANIFEST_NAME)
        self.assert_cpp("kEngineManifestFormat", package_engine.MANIFEST_FORMAT)
        self.assert_cpp("kCurrentEngineManifestSchemaVersion", package_engine.MANIFEST_SCHEMA_VERSION)

    def test_executables(self) -> None:
        self.assert_cpp("kEditorExecutableStem", package_engine.EDITOR_STEM)
        built = set(self.outputs.values())
        for stem in package_engine.EXECUTABLE_STEMS:
            self.assertIn(stem, built, f"no build target produces {stem}")

    def test_data_directories(self) -> None:
        self.assertEqual(package_engine.PRISM_ASSETS_DIRECTORY, uvbt.PRISM_ASSETS_DIRECTORY)
        editor = (REPO_ROOT / "Engine/App/Android/editor/editor_android.cpp").read_text(encoding="utf-8")
        self.assertIn(f'kPrismAssetsDirectoryName = "{package_engine.PRISM_ASSETS_DIRECTORY}"', editor)
        self.assert_cpp("kProjectTemplatesDirectoryName", uvbt.PROJECT_TEMPLATES_DIRECTORY)
        self.assertTrue(REPO_ROOT.joinpath(*uvbt.PRISM_ASSETS_SOURCE).is_dir())
        self.assertTrue(REPO_ROOT.joinpath(*uvbt.PROJECT_TEMPLATES_SOURCE).is_dir())

    def test_engine_feed(self) -> None:
        self.assert_cpp("kEngineFeedFileName", uvbt.ENGINE_FEED_FILE)
        feed_path = REPO_ROOT.joinpath(*uvbt.DISTRIBUTION_SOURCE, uvbt.ENGINE_FEED_FILE)
        self.assertEqual(feed_path, REPO_ROOT / ENGINE_FEED)
        feed = json.loads(feed_path.read_text(encoding="utf-8"))
        self.assertEqual(feed["format"], self.cpp["kEngineFeedFormat"])
        self.assertEqual(feed["schemaVersion"], self.cpp["kCurrentEngineFeedSchemaVersion"])
        self.assertTrue(feed["owner"] and feed["repository"], "the feed must name the releases repository")

    def test_workflow_reads_the_feed(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        self.assertIn(f"ENGINE_FEED_FILE: {ENGINE_FEED}", workflow)
        feed = json.loads((REPO_ROOT / ENGINE_FEED).read_text(encoding="utf-8"))
        # The whole name only: a development repository may share its prefix.
        public = re.escape(f"{feed['owner']}/{feed['repository']}") + r"(?![\w.-])"
        self.assertIsNone(re.search(public, workflow),
                          "the workflow must read the releases repository from the feed, not repeat it")

    def test_android_shaders_and_abi_contract(self) -> None:
        # The Android runtime APK and the Android engine package ship the same built-in shader folder.
        sys.path.insert(0, str(TOOLS_DIR / "android"))
        import build_apk  # noqa: E402

        self.assertEqual(package_engine.SHADERS_SOURCE, build_apk.BUILT_IN_SHADERS)
        self.assertTrue(REPO_ROOT.joinpath(*package_engine.SHADERS_SOURCE).is_dir())
        editor = (REPO_ROOT / "Engine/App/Android/editor/editor_android.cpp").read_text(encoding="utf-8")
        abi_header = (REPO_ROOT / ANDROID_ABI_CONTRACT_HEADER).read_text(encoding="utf-8")
        self.assertIn(f'kShadersDirectory = "{package_engine.SHADERS_DIRECTORY}"', editor)
        self.assertIn('#define UVE_ANDROID_EDITOR_ABI_VERSION 2U', abi_header)
        self.assertIn('#define UVE_ANDROID_EDITOR_ENTRY_SYMBOL "UveEditorRun"', abi_header)
        self.assertIn('#define UVE_ANDROID_EDITOR_ABI_VERSION_SYMBOL "UveEditorAbiVersion"', abi_header)
        self.assertIn('kEditorAndroidEntryPoint = "UveEditorRun"',
                      (REPO_ROOT / "Engine/Runtime/Pack/Expose/engine_install.h").read_text(encoding="utf-8"))
        self.assertIn("UveEditorRun(", editor)
        self.assertIn("UveEditorAbiVersion(", editor)
        self.assertEqual(uvbt.ANDROID_EDITOR_ENTRY, "UveEditorRun")
        self.assertEqual(uvbt.ANDROID_EDITOR_ABI_VERSION_ENTRY, "UveEditorAbiVersion")

    def test_android_icon(self) -> None:
        # Every manifest's launcher icon must be among the resources build_apk.py compiles.
        sys.path.insert(0, str(TOOLS_DIR / "android"))
        import build_apk  # noqa: E402

        resources = REPO_ROOT.joinpath(*build_apk.RESOURCES)
        for app in build_apk.APPS.values():
            manifest = REPO_ROOT.joinpath(*app.manifest).read_text(encoding="utf-8")
            for reference in re.findall(r'android:(?:round)?[iI]con="@mipmap/([\w]+)"', manifest):
                found = list(resources.glob(f"mipmap-*/{reference}.*"))
                self.assertTrue(found, f"{app.target}: no mipmap resource named {reference}")
            self.assertRegex(manifest, r'android:icon="@mipmap/', f"{app.target} has no launcher icon")

    def test_android_apk_versions(self) -> None:
        sys.path.insert(0, str(TOOLS_DIR / "android"))
        import build_apk  # noqa: E402

        version = build_apk.read_source_version("runtime")
        code, name = build_apk.android_version_metadata(version)
        self.assertGreaterEqual(code, 1)
        self.assertTrue(name)
        code_b1, name_b1 = build_apk.android_version_metadata(version, build_number=101)
        code_b2, name_b2 = build_apk.android_version_metadata(version, build_number=102)
        self.assertEqual(name_b1, name)
        self.assertEqual(name_b2, name)
        self.assertGreater(code_b2, code_b1)
        self.assertGreater(build_apk.android_version_metadata((0, 2, 0, 0))[0],
                           build_apk.android_version_metadata((0, 1, 0, 1))[0])
        self.assertGreater(build_apk.android_version_metadata((0, 1, 1, 0), build_number=2)[0],
                           build_apk.android_version_metadata((0, 1, 1, 0), build_number=1)[0])

    def test_android_manifests_lock_landscape(self) -> None:
        sys.path.insert(0, str(TOOLS_DIR / "android"))
        import build_apk  # noqa: E402

        for app in build_apk.APPS.values():
            manifest = REPO_ROOT.joinpath(*app.manifest).read_text(encoding="utf-8")
            self.assertIn('android:screenOrientation="sensorLandscape"', manifest,
                          f"{app.target} manifest must lock orientation to sensorLandscape")


if __name__ == "__main__":
    unittest.main()
