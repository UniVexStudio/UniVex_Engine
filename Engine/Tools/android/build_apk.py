#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""Builds a UniVex Android app's native library with UVBT and packages a signed APK, without Gradle.

    python3 Engine/Tools/android/build_apk.py --sdk "$ANDROID_HOME" --build-dir build/android

Each app in APPS names its UVBT target, manifest and the data it ships; every app runs in the
UniVexActivity compiled from JAVA_SOURCES. The APK is signed with a
local debug key (created on first use) unless --keystore is given.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "Engine" / "Tools" / "UVBT"))

import uvbt  # noqa: E402  (shared Android toolchain settings)

# SDK pieces the APK is built against. The platform is the targetSdk in the manifests.
ANDROID_PLATFORM = "android-35"
ANDROID_BUILD_TOOLS = "35.0.0"
NATIVE_CONFIGURATION = "Release"
# zipalign: 4-byte alignment for resources, and page alignment for uncompressed native libraries.
ZIP_ALIGNMENT = "4"
ZIP_PAGE_ALIGNMENT_KB = str(uvbt.ANDROID_PAGE_SIZE_BYTES // 1024)
DEBUG_KEYSTORE = "debug.keystore"
DEBUG_KEY_ALIAS = "androiddebugkey"
DEBUG_KEY_PASSWORD = "android"
DEBUG_KEY_SUBJECT = "CN=Android Debug,O=UniVex,C=US"
DEBUG_KEY_VALIDITY_DAYS = "10000"
DEBUG_KEY_BITS = "2048"
# Cooked content limits, matching the runtime's extraction bounds.
MAX_COOKED_FILES = 100000
MAX_COOKED_FILE_BYTES = 512 * 1024 * 1024
MAX_COOKED_TOTAL_BYTES = 1024 * 1024 * 1024
COOKED_INDEX = "project-files.txt"
COOKED_ASSETS = "project"
BUILT_IN_SHADERS = ("Engine", "Runtime", "RHI", "Shader", "built_in")
# Lists every file under assets/ for apps that unpack their data at startup (AAssetManager cannot
# list folders); read by the app's android_main.
ASSET_INDEX = "asset-index.txt"
# The activity every app runs in (UniVexActivity: full screen, display cutouts), compiled to dex.
JAVA_SOURCES = ("Engine", "App", "Android", "java")
JAVA_RELEASE = "11"
DEX_FILE = "classes.dex"


@dataclass(frozen=True)
class AndroidApp:
    target: str
    library: str
    manifest: tuple
    apk_prefix: str
    # Assets: (source path under the repository, file or folder name inside the APK's assets).
    assets: tuple = field(default_factory=tuple)
    accepts_cooked_project: bool = False
    index_assets: bool = False


APPS = {
    "runtime": AndroidApp(
        target="UniVexAndroid", library="univex_android", manifest=("Engine", "App", "Android", "AndroidManifest.xml"),
        apk_prefix="univex-runtime", assets=((BUILT_IN_SHADERS, "shaders"),), accepts_cooked_project=True),
    "hub": AndroidApp(
        target="UniVexHubAndroid", library="univex_hub", manifest=("Engine", "App", "Android", "hub", "AndroidManifest.xml"),
        apk_prefix="UniVexHub", index_assets=True,
        assets=((uvbt.PRISM_ASSETS_SOURCE, uvbt.PRISM_ASSETS_DIRECTORY),
                (uvbt.PROJECT_TEMPLATES_SOURCE, uvbt.PROJECT_TEMPLATES_DIRECTORY),
                ((*uvbt.DISTRIBUTION_SOURCE, uvbt.ENGINE_FEED_FILE), uvbt.ENGINE_FEED_FILE),
                (BUILT_IN_SHADERS, "shaders"))),
}


def run(*command):
    subprocess.run([str(part) for part in command], check=True)


def stage_cooked_project(manifest: Path, assets: Path, parser) -> list:
    """Copies a cooked project's runtime files under assets/project and writes the index."""
    source_root = manifest.parent
    if not manifest.is_file() or not (source_root / "cooked.json").is_file():
        parser.error("--cooked-project must name a cooked project manifest")
    metadata = json.loads((source_root / "cooked.json").read_text(encoding="utf-8"))
    names = metadata.get("runtimeFiles", [])
    if not names or len(names) != len(set(names)) or len(names) > MAX_COOKED_FILES:
        parser.error("Missing or invalid cooked runtime file index; recook the project")
    total = 0
    lines = []
    for name in sorted(names):
        relative = Path(name)
        if relative.is_absolute() or not relative.parts or any(part in {".", ".."} for part in relative.parts):
            parser.error("Invalid cooked runtime file path")
        path = source_root / relative
        if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(source_root.resolve()):
            parser.error("Missing or escaping cooked runtime file")
        if "\n" in name or "\r" in name:
            parser.error("Invalid cooked asset path")
        size = path.stat().st_size
        total += size
        if size > MAX_COOKED_FILE_BYTES or total > MAX_COOKED_TOTAL_BYTES:
            parser.error("Cooked APK content exceeds extraction bounds")
        destination = assets / COOKED_ASSETS / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
        lines.append(relative.as_posix())
    (assets / COOKED_INDEX).write_text(manifest.name + "\n" + "\n".join(lines) + "\n", encoding="utf-8")
    return lines


def compile_java(build: Path, tools: Path, platform_jar: Path) -> Path:
    """Compiles the activity's Java sources against the platform and converts them to one dex file."""
    classes = build / "classes"
    dex_dir = build / "dex"
    for folder in (classes, dex_dir):
        if folder.exists():
            shutil.rmtree(folder)
        folder.mkdir()
    sources = sorted(REPO_ROOT.joinpath(*JAVA_SOURCES).rglob("*.java"))
    if not sources:
        raise RuntimeError("No Java sources for the activity under " + "/".join(JAVA_SOURCES))
    run("javac", "--release", JAVA_RELEASE, "-Xlint:all", "-Werror", "-classpath", platform_jar, "-d", classes, *sources)
    run(tools / "d8", "--release", "--min-api", uvbt.ANDROID_MIN_API_LEVEL, "--lib", platform_jar, "--output", dex_dir,
        *sorted(classes.rglob("*.class")))
    return dex_dir / DEX_FILE


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sdk", type=Path, default=os.environ.get("ANDROID_HOME"), help="Android SDK (default: $ANDROID_HOME)")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--app", choices=sorted(APPS), default="runtime")
    parser.add_argument("--abi", choices=sorted(uvbt.ANDROID_ABI_TRIPLES), default=uvbt.ANDROID_DEFAULT_ABI)
    parser.add_argument("--native-lib", type=Path, help="Use this prebuilt native library instead of building it")
    parser.add_argument("--cooked-project", type=Path, help="Cooked .uvproject to ship (runtime app)")
    parser.add_argument("--keystore", type=Path, help="Release keystore; its passwords come from the "
                        "UVE_ANDROID_KEYSTORE_PASSWORD and UVE_ANDROID_KEY_ALIAS environment variables")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args()
    if args.sdk is None:
        parser.error("--sdk is required when ANDROID_HOME is not set")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    app = APPS[args.app]
    if args.cooked_project and not app.accepts_cooked_project:
        parser.error(f"the {args.app} app does not ship a cooked project")

    sdk = args.sdk.resolve()
    tools = sdk / "build-tools" / ANDROID_BUILD_TOOLS
    platform_jar = sdk / "platforms" / ANDROID_PLATFORM / "android.jar"
    ndk_bin = sdk / "ndk" / uvbt.ANDROID_NDK_VERSION / "toolchains" / "llvm" / "prebuilt" / uvbt.ANDROID_NDK_HOST_TAG / "bin"
    for required in (tools / "aapt2", tools / "d8", platform_jar, ndk_bin / "llvm-strip"):
        if not required.is_file():
            parser.error(f"Missing Android tool: {required}")

    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    if args.native_lib:
        library = args.native_lib.resolve()
        if not library.is_file():
            parser.error(f"Missing --native-lib: {library}")
    else:
        env = dict(os.environ, ANDROID_HOME=str(sdk), UVBT_ANDROID_ABI=args.abi)
        subprocess.run([sys.executable, str(REPO_ROOT / "Engine" / "Tools" / "UVBT" / "uvbt.py"), "build", app.target,
                        "Android", NATIVE_CONFIGURATION, f"-j{args.jobs}"], check=True, env=env)
        library = (REPO_ROOT / ".uvbt" / "bin" / "Android" / args.abi / NATIVE_CONFIGURATION /
                   f"lib{app.library}.so")

    # Fresh assets every time, so nothing from an earlier build or project ships by accident.
    assets = build / "assets"
    if assets.exists():
        shutil.rmtree(assets)
    assets.mkdir()
    for source, name in app.assets:
        path = REPO_ROOT.joinpath(*source)
        if path.is_dir():
            shutil.copytree(path, assets / name)
        else:
            shutil.copy2(path, assets / name)
    cooked = stage_cooked_project(args.cooked_project.resolve(), assets, parser) if args.cooked_project else []
    if app.index_assets:
        names = sorted(f.relative_to(assets).as_posix() for f in assets.rglob("*") if f.is_file())
        (assets / ASSET_INDEX).write_text("\n".join(names) + "\n", encoding="utf-8")

    dex = compile_java(build, tools, platform_jar)

    unsigned = build / "unsigned.apk"
    run(tools / "aapt2", "link", "-I", platform_jar, "--manifest", REPO_ROOT.joinpath(*app.manifest), "-A", assets,
        "-o", unsigned)
    stripped = build / f"lib{app.library}.so"
    shutil.copy2(library, stripped)
    run(ndk_bin / "llvm-strip", "--strip-unneeded", stripped)
    with zipfile.ZipFile(unsigned, "a", compression=zipfile.ZIP_STORED) as package:
        package.write(dex, DEX_FILE)
        # The C++ runtime is linked statically, so the library is the only native file.
        package.write(stripped, f"lib/{args.abi}/lib{app.library}.so")
        # aapt2 leaves out dotfiles (such as the .uvassetdb catalog); add whatever it skipped.
        existing = set(package.namelist())
        for asset in sorted(assets.rglob("*")):
            if asset.is_file():
                archive_name = "assets/" + asset.relative_to(assets).as_posix()
                if archive_name not in existing:
                    package.write(asset, archive_name)
                    existing.add(archive_name)
        missing = [name for name in cooked if f"assets/{COOKED_ASSETS}/{name}" not in existing]
        if missing:
            raise RuntimeError("Cooked assets missing from the APK: " + ", ".join(missing))

    aligned = build / "aligned.apk"
    run(tools / "zipalign", "-f", "-P", ZIP_PAGE_ALIGNMENT_KB, ZIP_ALIGNMENT, unsigned, aligned)
    if args.keystore:
        keystore = args.keystore.resolve()
        password = os.environ.get("UVE_ANDROID_KEYSTORE_PASSWORD")
        alias = os.environ.get("UVE_ANDROID_KEY_ALIAS")
        if not password or not alias:
            parser.error("--keystore needs UVE_ANDROID_KEYSTORE_PASSWORD and UVE_ANDROID_KEY_ALIAS")
        signing = ["--ks", keystore, "--ks-key-alias", alias, "--ks-pass", "env:UVE_ANDROID_KEYSTORE_PASSWORD"]
    else:
        keystore = build / DEBUG_KEYSTORE
        if not keystore.exists():
            run("keytool", "-genkeypair", "-keystore", keystore, "-storepass", DEBUG_KEY_PASSWORD, "-keypass",
                DEBUG_KEY_PASSWORD, "-alias", DEBUG_KEY_ALIAS, "-dname", DEBUG_KEY_SUBJECT, "-keyalg", "RSA",
                "-keysize", DEBUG_KEY_BITS, "-validity", DEBUG_KEY_VALIDITY_DAYS)
        signing = ["--ks", keystore, "--ks-pass", f"pass:{DEBUG_KEY_PASSWORD}", "--key-pass", f"pass:{DEBUG_KEY_PASSWORD}"]
    output = build / f"{app.apk_prefix}-{args.abi}.apk"
    run(tools / "apksigner", "sign", *signing, "--out", output, aligned)
    run(tools / "apksigner", "verify", output)
    run(tools / "zipalign", "-c", "-P", ZIP_PAGE_ALIGNMENT_KB, ZIP_ALIGNMENT, output)
    print(f"APK: {output}")


if __name__ == "__main__":
    main()
