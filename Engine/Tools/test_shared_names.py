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

import make_hub_installer  # noqa: E402
import package_engine  # noqa: E402
import uvbt  # noqa: E402

HEADERS = (
    "Engine/Runtime/Online/Expose/engine_release_feed.h",
    "Engine/Runtime/Online/Expose/engine_library.h",
    "Engine/Runtime/Online/Expose/hub_setup.h",
    "Engine/Runtime/Online/Expose/tar_gz_archive.h",
    "Engine/Runtime/Pack/Expose/engine_install.h",
    "Engine/Runtime/Pack/Expose/project_template.h",
    "Engine/App/Internal/launcher_ui/launcher_window.h",
)
TARGETS_DIR = "Engine/Source/Targets"
ENGINE_FEED = "Engine/Distribution/engine_feed.json"

# `inline constexpr <type> kName = <value>;` where the value is a string, an unsigned integer, or
# the name of another constant.
CONSTANT_PATTERN = re.compile(
    r"inline\s+constexpr\s+[\w:<>]+\s+(k\w+UVE)\s*=\s*(\"(?:[^\"\\]|\\.)*\"|\d+U?|k\w+UVE)\s*;"
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
        self.assert_cpp("kEnginePackagePrefixUVE", package_engine.PACKAGE_PREFIX)
        self.assert_cpp("kEnginePackageExtensionUVE", package_engine.PACKAGE_EXTENSION)
        self.assert_cpp("kTarGzFileExtensionUVE", package_engine.PACKAGE_EXTENSION)
        self.assert_cpp("kEngineChecksumSuffixUVE", package_engine.CHECKSUM_SUFFIX)
        self.assert_cpp("kBetaTagSuffixUVE", package_engine.BETA_TAG_SUFFIX)

    def test_engine_manifest(self) -> None:
        self.assert_cpp("kEngineManifestFileNameUVE", package_engine.MANIFEST_NAME)
        self.assert_cpp("kEngineManifestFormatUVE", package_engine.MANIFEST_FORMAT)
        self.assert_cpp("kCurrentEngineManifestSchemaVersionUVE", package_engine.MANIFEST_SCHEMA_VERSION)

    def test_executables(self) -> None:
        self.assert_cpp("kEditorExecutableStemUVE", package_engine.EDITOR_STEM)
        self.assert_cpp("kHubExecutableStemUVE", make_hub_installer.HUB_STEM)
        self.assert_cpp("kSetupExecutableStemUVE", make_hub_installer.SETUP_STEM)
        built = set(self.outputs.values())
        for stem in (*package_engine.EXECUTABLE_STEMS, make_hub_installer.HUB_STEM, make_hub_installer.SETUP_STEM):
            self.assertIn(stem, built, f"no build target produces {stem}")

    def test_data_directories(self) -> None:
        for directory in (package_engine.PRISM_ASSETS_DIRECTORY, uvbt.PRISM_ASSETS_DIRECTORY):
            self.assert_cpp("kPrismAssetsDirectoryNameUVE", directory)
        for directory in (make_hub_installer.PROJECT_TEMPLATES_DIRECTORY, uvbt.PROJECT_TEMPLATES_DIRECTORY):
            self.assert_cpp("kProjectTemplatesDirectoryNameUVE", directory)
        self.assertTrue(REPO_ROOT.joinpath(*uvbt.PRISM_ASSETS_SOURCE).is_dir())
        self.assertTrue(REPO_ROOT.joinpath(*uvbt.PROJECT_TEMPLATES_SOURCE).is_dir())

    def test_hub_installer(self) -> None:
        self.assert_cpp("kSetupPayloadMagicUVE", make_hub_installer.PAYLOAD_MAGIC.decode("ascii"))
        self.assert_cpp("kHubIconsDirectoryNameUVE", make_hub_installer.ICONS_DIRECTORY)
        self.assert_cpp("kHubWindowsIconFileNameUVE", make_hub_installer.WINDOWS_ICON_FILE)
        brand_pngs = make_hub_installer.BRAND_DIR / make_hub_installer.BRAND_PNG_DIRECTORY
        self.assertTrue((brand_pngs / str(self.cpp["kHubIconFileNameUVE"])).is_file(),
                        "the icon the Hub registers is not among the brand images the installer ships")

    def test_engine_feed(self) -> None:
        for name in (make_hub_installer.FEED_FILE, uvbt.ENGINE_FEED_FILE):
            self.assert_cpp("kEngineFeedFileNameUVE", name)
        feed_path = REPO_ROOT.joinpath(*uvbt.DISTRIBUTION_SOURCE, uvbt.ENGINE_FEED_FILE)
        self.assertEqual(feed_path, REPO_ROOT / ENGINE_FEED)
        feed = json.loads(feed_path.read_text(encoding="utf-8"))
        self.assertEqual(feed["format"], self.cpp["kEngineFeedFormatUVE"])
        self.assertEqual(feed["schemaVersion"], self.cpp["kCurrentEngineFeedSchemaVersionUVE"])
        self.assertTrue(feed["owner"] and feed["repository"], "the feed must name the releases repository")

    def test_release_workflow_reads_the_feed(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        self.assertIn(f"ENGINE_FEED_FILE: {ENGINE_FEED}", workflow)
        feed = json.loads((REPO_ROOT / ENGINE_FEED).read_text(encoding="utf-8"))
        self.assertNotIn(f"{feed['owner']}/{feed['repository']}", workflow,
                         "the workflow must read the releases repository from the feed, not repeat it")


if __name__ == "__main__":
    unittest.main()
