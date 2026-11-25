#!/usr/bin/env python3
# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
"""UniVexBuildTool (UVBT) - Standalone C# Engine Build System Runner.

Discovers *.Build.cs and *.Target.cs files, resolves the module dependency DAG,
builds parallel compiler action graphs, manages incremental header caching (-MMD),
and directly invokes native toolchains (GCC / Clang / MSVC) to produce engine binaries.
"""

import argparse
import concurrent.futures
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
from pathlib import Path


def _download_github_tag_tarball(url: str, tag: str, dest: Path) -> bool:
    """Downloads and extracts a public GitHub tag archive into `dest`.

    Uses GitHub's codeload CDN, which is independent of git-upload-pack rate limits and
    never consumes repository git credentials. Returns True on success.
    """
    match = re.fullmatch(r"https://github\.com/([^/]+/[^/]+?)(?:\.git)?/?", url)
    if not match:
        return False
    archive_url = f"https://github.com/{match.group(1)}/archive/refs/tags/{tag}.tar.gz"
    req = urllib.request.Request(archive_url, headers={"User-Agent": "UVBT/1.0"})
    try:
        with tempfile.TemporaryDirectory(dir=str(dest.parent), prefix=".uvbt_fetch_") as tmp:
            tmp_dir = Path(tmp)
            archive_path = tmp_dir / "source.tar.gz"
            with urllib.request.urlopen(req, timeout=60) as response, archive_path.open("wb") as out:
                shutil.copyfileobj(response, out)
            staging = tmp_dir / "extract"
            staging.mkdir(parents=True, exist_ok=True)
            with tarfile.open(archive_path, "r:gz") as tar:
                for member in tar.getmembers():
                    member_path = Path(member.name)
                    if member_path.is_absolute() or ".." in member_path.parts:
                        raise RuntimeError(f"Unsafe archive entry: {member.name}")
                if hasattr(tarfile, "tar_filter"):
                    tar.extractall(staging, filter="tar")
                else:
                    tar.extractall(staging)
            roots = [p for p in staging.iterdir() if p.is_dir()]
            if len(roots) != 1:
                return False
            if dest.exists():
                shutil.rmtree(dest, ignore_errors=True)
            shutil.move(str(roots[0]), str(dest))
            return True
    except Exception as exc:
        print(f"[UVBT] Warning: archive fallback {archive_url} failed: {exc}", flush=True)
        return False


def git_clone_public(url: str, tag: str, dest: Path, retries: int = 4) -> None:
    """Fetches a public third-party repository at `tag` into `dest`.

    Runs outside the repository root with any inherited GitHub extraheader cleared so a
    repo-scoped CI token never triggers HTTP 403 against public upstream repositories.
    Falls back to the GitHub tag tarball CDN if git-upload-pack is throttled, and retries
    transient failures with jittered exponential backoff.
    """
    dest = dest.resolve()
    dest.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["GIT_TERMINAL_PROMPT"] = "0"
    cmd = [
        "git",
        "-c", "http.https://github.com/.extraheader=",
        "-c", "credential.helper=",
        "clone", "--depth", "1", "-b", tag, url, str(dest),
    ]
    last_error = None
    for attempt in range(1, retries + 1):
        if dest.exists():
            shutil.rmtree(dest, ignore_errors=True)
        try:
            subprocess.run(cmd, cwd=tempfile.gettempdir(), env=env, check=True)
            return
        except subprocess.CalledProcessError as exc:
            last_error = exc
            if dest.exists():
                shutil.rmtree(dest, ignore_errors=True)
            print(
                f"[UVBT] Warning: git clone {url} ({tag}) failed (attempt {attempt}/{retries}); "
                "trying tag archive fallback...",
                flush=True,
            )
            if _download_github_tag_tarball(url, tag, dest):
                return
            if attempt < retries:
                delay = 3 * (2 ** (attempt - 1)) + random.uniform(0.0, 2.0)
                print(
                    f"[UVBT] Warning: fetch {url} ({tag}) failed (attempt {attempt}/{retries}); "
                    f"retrying in {delay:.1f}s...",
                    flush=True,
                )
                time.sleep(delay)
    raise last_error


# Data that ships beside a built executable. The directory and file names are the ones the engine
# looks for (Engine/Tools/test_shared_names.py checks them against the C++ constants).
PRISM_ASSETS_SOURCE = ("Engine", "Runtime", "Prism", "assets")
PRISM_ASSETS_DIRECTORY = "prism_assets"
PROJECT_TEMPLATES_SOURCE = ("Engine", "Templates", "Projects")
PROJECT_TEMPLATES_DIRECTORY = "project_templates"
DISTRIBUTION_SOURCE = ("Engine", "Distribution")
ENGINE_FEED_FILE = "engine_feed.json"

# Build.cs entries may be qualified with a platform: "linux:X11" applies on Linux only. "desktop:"
# covers the desktop platforms. Unqualified entries apply everywhere. This works for dependencies,
# definitions, system libraries and excluded files alike.
PLATFORM_QUALIFIERS = {
    "linux": ("Linux",),
    "win": ("Windows",),
    "android": ("Android",),
    "desktop": ("Linux", "Windows"),
}

# Android: the NDK release the engine is built and tested with, the oldest Android version it runs on
# (the manifests' minSdk), and the ABIs it is built for (the first is the default).
ANDROID_NDK_VERSION = "29.0.14206865"
ANDROID_MIN_API_LEVEL = 28
ANDROID_ABI_TRIPLES = {"arm64-v8a": "aarch64-linux-android", "x86_64": "x86_64-linux-android"}
ANDROID_DEFAULT_ABI = "arm64-v8a"
ANDROID_NDK_HOST_TAG = "linux-x86_64"
# Android 15 devices may use 16 KB memory pages; native libraries must be aligned for them.
ANDROID_PAGE_SIZE_BYTES = 16384
ANDROID_SYSTEM_LIBRARIES = ["android", "log", "EGL", "GLESv3", "m", "dl"]
# The entry points Android loads dynamically (NativeActivity glue and Hub's dlopen editor entry).
ANDROID_ACTIVITY_ENTRY = "ANativeActivity_onCreate"
ANDROID_EDITOR_ENTRY = "UveEditorRun"
ANDROID_EDITOR_ABI_VERSION_ENTRY = "UveEditorAbiVersion"
# Optional static Linux UI libraries built when system packages are absent or UVBT_STATIC_LINUX_UI=1.
LINUX_GLFW_REPO = ("glfw", "https://github.com/glfw/glfw.git", "3.4")
LINUX_GLEW_REPO = ("glew", "https://github.com/Perlmint/glew-cmake.git", "glew-cmake-2.2.0")


def select_for_platform(entries, platform):
    """The entries that apply on `platform`, with any platform qualifier removed."""
    selected = []
    for entry in entries:
        qualifier, sep, value = entry.partition(":")
        if sep and qualifier in PLATFORM_QUALIFIERS:
            if platform in PLATFORM_QUALIFIERS[qualifier]:
                selected.append(value)
        else:
            selected.append(entry)
    return selected


class Toolchain:
    """The compilers and flags for one platform. Android cross-compiles with the NDK's clang."""

    def __init__(self, platform: str):
        self.platform = platform
        self.target_flags = []
        self.is_android = platform == "Android"
        # The NDK's libc++ implements std::format; the desktop toolchains fall back to {fmt}.
        self.has_std_format = self.is_android
        if self.is_android:
            self.abi = os.environ.get("UVBT_ANDROID_ABI", ANDROID_DEFAULT_ABI)
            if self.abi not in ANDROID_ABI_TRIPLES:
                raise RuntimeError(f"[UVBT] Unknown Android ABI '{self.abi}'; use one of {sorted(ANDROID_ABI_TRIPLES)}")
            self.ndk = self._find_ndk()
            tools = self.ndk / "toolchains" / "llvm" / "prebuilt" / ANDROID_NDK_HOST_TAG / "bin"
            self.cxx = str(tools / "clang++")
            self.cc = str(tools / "clang")
            self.ar = str(tools / "llvm-ar")
            self.triple = f"{ANDROID_ABI_TRIPLES[self.abi]}{ANDROID_MIN_API_LEVEL}"
            self.target_flags = [f"--target={self.triple}"]
            self.output_dir = f"Android/{self.abi}"
        else:
            self.cxx = os.environ.get("CXX", "g++")
            self.cc = os.environ.get("CC", "gcc")
            self.ar = os.environ.get("AR", "ar")
            self.output_dir = platform

    @staticmethod
    def ndk_revision(ndk: Path) -> str:
        """The NDK release an install is, from its source.properties ("" when it has none)."""
        properties = ndk / "source.properties"
        if not properties.is_file():
            return ""
        for line in properties.read_text(encoding="utf-8", errors="ignore").splitlines():
            key, _, value = line.partition("=")
            if key.strip() == "Pkg.Revision":
                return value.strip()
        return ""

    @staticmethod
    def _find_ndk() -> Path:
        # Only the release the engine is built with: hosts often point ANDROID_NDK_HOME at another one.
        candidates = [os.environ.get("ANDROID_NDK_HOME"), os.environ.get("ANDROID_NDK_ROOT")]
        for sdk_var in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
            sdk = os.environ.get(sdk_var)
            if sdk:
                candidates.append(str(Path(sdk) / "ndk" / ANDROID_NDK_VERSION))
        for candidate in candidates:
            if candidate and Toolchain.ndk_revision(Path(candidate)) == ANDROID_NDK_VERSION:
                return Path(candidate)
        raise RuntimeError(f"[UVBT] Android NDK {ANDROID_NDK_VERSION} not found: install it in the SDK that ANDROID_HOME "
                           "names, or point ANDROID_NDK_HOME at it")

    def cmake_toolchain_args(self):
        """CMake arguments for building a third-party library with this toolchain."""
        if not self.is_android:
            return [f"-DCMAKE_C_COMPILER={self.cc}", f"-DCMAKE_CXX_COMPILER={self.cxx}"]
        return [f"-DCMAKE_TOOLCHAIN_FILE={self.ndk / 'build' / 'cmake' / 'android.toolchain.cmake'}",
                f"-DANDROID_ABI={self.abi}", f"-DANDROID_PLATFORM=android-{ANDROID_MIN_API_LEVEL}"]


class ModuleInfo:
    def __init__(self, name, directory, module_type="StaticLibrary"):
        self.name = name
        self.directory = Path(directory).resolve()
        self.module_type = module_type
        self.public_deps = []
        self.private_deps = []
        self.public_includes = []
        self.private_includes = []
        self.public_defs = []
        self.private_defs = []
        self.public_sys_libs = []
        self.private_sys_libs = []
        self.source_files = []
        # Windows resource scripts (.rc): icons and version data, compiled and linked on Windows only.
        self.resource_files = []
        self.excluded_files = []

    def __repr__(self):
        return f"<Module {self.name} ({self.module_type}) @ {self.directory}>"


class TargetInfo:
    def __init__(self, name, filepath, target_type="Program"):
        self.name = name
        self.filepath = Path(filepath).resolve()
        self.target_type = target_type
        self.extra_modules = []
        self.global_defs = []
        self.output_name = name
        self.is_gui_app = False

    def __repr__(self):
        return f"<Target {self.name} ({self.target_type})>"


class CSharpParser:
    """Parses *.Build.cs and *.Target.cs files without requiring Roslyn or .NET SDK."""

    @staticmethod
    def parse_build_cs(filepath: Path, platform: str = "Linux") -> ModuleInfo:
        text = filepath.read_text(encoding="utf-8", errors="ignore")
        mod_dir = filepath.parent

        # Extract class name or module name
        match_cls = re.search(r"class\s+([A-Za-z0-9_]+)\s*:\s*ModuleRules", text)
        raw_name = match_cls.group(1) if match_cls else filepath.stem.replace(".Build", "")
        # Remove "Module" suffix if present
        mod_name = raw_name[:-6] if raw_name.endswith("Module") and len(raw_name) > 6 else raw_name

        # Extract ModuleType
        mod_type = "StaticLibrary"
        match_type = re.search(r"Type\s*=\s*ModuleType\.([A-Za-z]+)", text)
        if match_type:
            mod_type = match_type.group(1)

        mod = ModuleInfo(mod_name, mod_dir, mod_type)

        def extract_strings(field_name):
            pattern = re.compile(rf"{field_name}\s*\.\s*(?:AddRange|Add)\s*\(\s*(?:new\s*(?:string\[\]|List<string>)\s*\{{)?([^}};\)]+)", re.DOTALL)
            m = pattern.search(text)
            if not m:
                return []
            content = m.group(1)
            raw_matches = re.findall(r'"((?:\\.|[^"\\])*)"', content)
            return select_for_platform([s.replace('\\"', '"').replace('\\\\', '\\') for s in raw_matches], platform)

        mod.public_deps = extract_strings("PublicDependencyModuleNames")
        mod.private_deps = extract_strings("PrivateDependencyModuleNames")
        
        custom_pub_inc = extract_strings("PublicIncludePaths")
        custom_priv_inc = extract_strings("PrivateIncludePaths")
        
        mod.public_defs = extract_strings("PublicDefinitions")
        mod.private_defs = extract_strings("PrivateDefinitions")
        mod.public_sys_libs = extract_strings("PublicSystemLibraries")
        mod.private_sys_libs = extract_strings("PrivateSystemLibraries")
        mod.excluded_files = extract_strings("ExcludedFiles")
        mod.extra_source_dirs = extract_strings("ExtraSourceDirectories")

        # Standard UniVex conventions: discover all Expose and Internal directories under module
        for exp in mod_dir.rglob("Expose"):
            if exp.is_dir() and exp not in mod.public_includes:
                mod.public_includes.append(exp)
        for rel in custom_pub_inc:
            p = (mod_dir / rel).resolve()
            if p.is_dir() and p not in mod.public_includes:
                mod.public_includes.append(p)

        for intern in mod_dir.rglob("Internal"):
            if intern.is_dir() and intern not in mod.private_includes:
                mod.private_includes.append(intern)
        for rel in custom_priv_inc:
            p = (mod_dir / rel).resolve()
            if p.is_dir() and p not in mod.private_includes:
                mod.private_includes.append(p)

        # Discover source files (.cpp, .c) and Windows resource scripts (.rc)
        sources = []
        resources = []
        if mod.module_type != "HeaderOnly":
            extra_dirs = [(mod_dir / d).resolve() for d in mod.extra_source_dirs if (mod_dir / d).is_dir()]
            search_dirs = [mod_dir] + extra_dirs
            seen_roots = set()
            is_test_module = mod.name.endswith("Tests")
            for s_dir in search_dirs:
                for root_dir, dirnames, files in os.walk(s_dir):
                    if not is_test_module:
                        dirnames[:] = [d for d in dirnames if d not in ("Tests", "tests", "Testing")]
                    rpath = Path(root_dir).resolve()
                    if rpath in seen_roots or ".git" in rpath.parts or "build" in rpath.parts or ".uvbt" in rpath.parts:
                        continue
                    seen_roots.add(rpath)
                    for f in files:
                        if f.endswith((".cpp", ".c", ".cc", ".rc")):
                            full_src = rpath / f
                            try:
                                rel_str = str(full_src.relative_to(mod_dir)).replace("\\", "/")
                            except ValueError:
                                rel_str = str(full_src.relative_to(s_dir)).replace("\\", "/")
                            if f not in mod.excluded_files and not any(ex in rel_str for ex in mod.excluded_files):
                                (resources if f.endswith(".rc") else sources).append(full_src)
        mod.source_files = sorted(list(dict.fromkeys(sources)))
        mod.resource_files = sorted(list(dict.fromkeys(resources)))

        return mod

    @staticmethod
    def parse_target_cs(filepath: Path) -> TargetInfo:
        text = filepath.read_text(encoding="utf-8", errors="ignore")
        match_cls = re.search(r"class\s+([A-Za-z0-9_]+)\s*:\s*TargetRules", text)
        raw_name = match_cls.group(1) if match_cls else filepath.stem.replace(".Target", "")
        target_name = raw_name[:-6] if raw_name.endswith("Target") and len(raw_name) > 6 else raw_name

        match_type = re.search(r"Type\s*=\s*TargetType\.([A-Za-z]+)", text)
        target_type = match_type.group(1) if match_type else "Program"

        target = TargetInfo(target_name, filepath, target_type)

        pattern = re.compile(r"ExtraModuleNames\s*\.\s*(?:AddRange|Add)\s*\(\s*(?:new\s*(?:string\[\]|List<string>)\s*\{{)?([^}};\)]+)", re.DOTALL)
        m = pattern.search(text)
        if m:
            content = m.group(1)
            target.extra_modules = [s.strip().strip('"') for s in re.findall(r'"([^"]+)"', content)]

        pattern_out = re.search(r'OutputName\s*=\s*"([^"]+)"', text)
        if pattern_out:
            target.output_name = pattern_out.group(1)

        pattern_gui = re.search(r"IsGuiApp\s*=\s*(true|false)\b", text)
        if pattern_gui:
            target.is_gui_app = pattern_gui.group(1) == "true"

        return target


class BuildCache:
    def __init__(self, cache_file: Path):
        self.cache_file = cache_file
        self.data = {}
        if cache_file.exists():
            try:
                self.data = json.loads(cache_file.read_text(encoding="utf-8"))
            except Exception:
                self.data = {}

    def save(self):
        self.cache_file.parent.mkdir(parents=True, exist_ok=True)
        self.cache_file.write_text(json.dumps(self.data, indent=2), encoding="utf-8")

    def is_action_up_to_date(self, obj_file: Path, source_file: Path, dep_file: Path, cmd_str: str) -> bool:
        if not obj_file.exists():
            return False

        recorded = self.data.get(str(obj_file))
        if recorded is not None and recorded.get("cmd") != cmd_str:
            return False

        # Check source file mtime vs object file mtime
        obj_mtime = obj_file.stat().st_mtime
        if not source_file.exists() or source_file.stat().st_mtime > obj_mtime:
            return False

        # Check dependency files listed in .d vs object file mtime
        if dep_file.exists():
            headers = self._parse_dep_file(dep_file)
            for h in headers:
                hp = Path(h)
                if not hp.exists() or hp.stat().st_mtime > obj_mtime:
                    return False
        return True

    def record_action(self, obj_file: Path, source_file: Path, cmd_str: str):
        self.data[str(obj_file)] = {
            "cmd": cmd_str,
            "mtime": obj_file.stat().st_mtime if obj_file.exists() else time.time()
        }

    @staticmethod
    def _parse_dep_file(dep_file: Path):
        try:
            content = dep_file.read_text(encoding="utf-8", errors="ignore")
            # Replace escaped newlines
            content = content.replace("\\\n", " ")
            parts = content.split(":", 1)
            if len(parts) == 2:
                items = parts[1].split()
                return [i for i in items if not i.endswith(".o") and not i.endswith(".obj")]
        except Exception:
            pass
        return []


class EngineBuilder:
    def __init__(self, repo_root: Path):
        self.repo_root = repo_root.resolve()
        self.modules = {}
        self.targets = {}

    def _freetype_include_dirs(self, toolchain=None):
        if toolchain is not None and toolchain.is_android:
            installed = self.repo_root / ".uvbt" / "tp_lib" / toolchain.output_dir / "freetype" / "include" / "freetype2"
            return [installed] if installed.is_dir() else []
        dirs = []
        try:
            out = subprocess.run(["pkg-config", "--cflags-only-I", "freetype2"], stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL, text=True, check=False).stdout
            dirs += [Path(tok[2:]) for tok in out.split() if tok.startswith("-I")]
        except OSError:
            pass
        candidates = [Path("/usr/include/freetype2"), Path("/usr/local/include/freetype2")]
        gxx = shutil.which(os.environ.get("CXX", "g++"))
        if gxx:
            candidates.append(Path(gxx).resolve().parent.parent / "include" / "freetype2")
        for cand in candidates:
            if cand.is_dir() and cand not in dirs:
                dirs.append(cand)
        return [d for d in dirs if d.is_dir()]

    def _compile_resources(self, target, obj_dir, cxx):
        """Compiles the root modules' Windows resource scripts (.rc) into objects to link.

        Only the root modules' are used: a resource object inside a static library is never pulled
        into the link, because nothing references it. windres is taken from WINDRES or from beside
        the compiler, and each script runs from its own folder so relative file names in it work.
        """
        scripts = [rc for name in target.extra_modules if name in self.modules
                   for rc in self.modules[name].resource_files]
        if not scripts:
            return []
        windres = os.environ.get("WINDRES") or shutil.which("windres")
        if windres is None:
            compiler = shutil.which(cxx)
            candidate = Path(compiler).with_name("windres.exe") if compiler else None
            windres = str(candidate) if candidate and candidate.is_file() else None
        if windres is None:
            raise RuntimeError("[UVBT] windres is needed to compile Windows resources (.rc).")
        objects = []
        for script in scripts:
            out_obj = obj_dir / "resources" / (script.stem + ".res.o")
            out_obj.parent.mkdir(parents=True, exist_ok=True)
            cmd = [windres, "--input", script.name, "--output", str(out_obj), "--output-format=coff"]
            res = subprocess.run(cmd, cwd=script.parent, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if res.returncode != 0:
                print(res.stdout)
                print(res.stderr)
                raise RuntimeError(f"[UVBT] Resource compilation failed: {script}")
            print(f"[UVBT] Compiled resources :: {script.name}")
            objects.append(str(out_obj))
        return objects

    def _ensure_uiautomation_import_lib(self, lib_dir: Path):
        implib = lib_dir / "libuiautomationcore.a"
        def_file = self.repo_root / "Engine" / "Runtime" / "Prism" / "Internal" / "windows" / "uiautomationcore.def"
        if implib.exists() or not def_file.exists():
            return
        dlltool = shutil.which("dlltool") or shutil.which("x86_64-w64-mingw32-dlltool")
        if not dlltool:
            raise RuntimeError("[UVBT] dlltool not found: needed to build libuiautomationcore.a for PRISM on MinGW")
        subprocess.run([dlltool, "-d", str(def_file), "-l", str(implib)], check=True)

    def discover(self, platform: str = "Linux"):
        # Scan for *.Build.cs
        for root_dir, _, files in os.walk(self.repo_root):
            rpath = Path(root_dir)
            if ".git" in rpath.parts or "build" in rpath.parts or ".uvbt" in rpath.parts:
                continue
            for f in files:
                if f.endswith(".Build.cs"):
                    mod = CSharpParser.parse_build_cs(rpath / f, platform)
                    self.modules[mod.name] = mod
                elif f.endswith(".Target.cs"):
                    target = CSharpParser.parse_target_cs(rpath / f)
                    self.targets[target.name] = target

    def resolve_dependencies(self, root_module_names):
        """Returns ordered list of all transitive module dependencies."""
        resolved = []
        visited = set()

        def dfs(name):
            if name in visited:
                return
            visited.add(name)
            mod = self.modules.get(name)
            if not mod:
                raise ValueError(f"Module '{name}' not found. Registered modules: {list(self.modules.keys())}")
            for dep in mod.public_deps + mod.private_deps:
                dfs(dep)
            resolved.append(mod)

        for name in root_module_names:
            dfs(name)
        return resolved

    def generate_prebuild_artifacts(self, uvbt_root: Path, bin_dir: Path = None):
        gen_dir = uvbt_root / "generated"
        gen_dir.mkdir(parents=True, exist_ok=True)
        embed_script = self.repo_root / "Engine" / "Tools" / "embed_file.py"
        cxx = os.environ.get("CXX", "g++")

        def needs_regen(output_path: Path, input_paths) -> bool:
            if not output_path.exists():
                return True
            out_mtime = output_path.stat().st_mtime
            return any(p.exists() and p.stat().st_mtime > out_mtime for p in input_paths)

        # 1. Retarget humanoid reference
        retarget_gen = gen_dir / "Retarget"
        retarget_gen.mkdir(parents=True, exist_ok=True)
        retarget_inc = retarget_gen / "humanoid_reference_json.inc"
        retarget_src = self.repo_root / "Engine" / "Runtime" / "Retarget" / "Runtime" / "Data" / "humanoid_reference.json"
        if retarget_src.exists() and needs_regen(retarget_inc, [retarget_src, embed_script]):
            subprocess.run([sys.executable, str(embed_script), str(retarget_src), str(retarget_inc), "uve_retarget_humanoid_json"], check=True)

        # 2. UI runtime font
        ui_gen = gen_dir / "UI"
        ui_gen.mkdir(parents=True, exist_ok=True)
        ui_inc = ui_gen / "ui_font_ttf_bytes.inc"
        ui_src = self.repo_root / "Engine" / "Runtime" / "UI" / "assets" / "fonts" / "liberation-sans-subset.ttf"
        if ui_src.exists() and needs_regen(ui_inc, [ui_src, embed_script]):
            subprocess.run([sys.executable, str(embed_script), str(ui_src), str(ui_inc), "uve_ui_runtime_font_ttf_bytes"], check=True)

        editor_gen = gen_dir / "EditorCore"
        editor_gen.mkdir(parents=True, exist_ok=True)

        # 3. Editor icons
        editor_icons_inc = editor_gen / "uve_editor_icon_set.inc"
        embed_icons_script = self.repo_root / "Engine" / "Tools" / "editor_icons" / "embed_editor_icons.py"
        icons_dir = self.repo_root / "Engine" / "Editor" / "EditorCore" / "assets" / "icons"
        if embed_icons_script.exists() and icons_dir.is_dir() and needs_regen(editor_icons_inc, [embed_icons_script, *icons_dir.glob("*")]):
            subprocess.run([sys.executable, str(embed_icons_script), str(editor_icons_inc), str(icons_dir)], check=True)

        # 5. Viewport Shaders
        vp_render_gen = gen_dir / "univex" / "render"
        vp_render_gen.mkdir(parents=True, exist_ok=True)
        vp_shaders = self.repo_root / "Engine" / "Editor" / "Viewport" / "shaders"

        grid_in = vp_shaders / "grid_shader_sources.h.in"
        grid_out = vp_render_gen / "grid_shader_sources.h"
        grid_srcs = [grid_in, vp_shaders / "infinite_grid.vert", vp_shaders / "infinite_grid.frag"]
        if grid_in.exists() and vp_shaders.is_dir() and needs_regen(grid_out, grid_srcs):
            text = grid_in.read_text(encoding="utf-8")
            text = text.replace("@UNIVEX_GRID_VERT_SOURCE@", (vp_shaders / "infinite_grid.vert").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_GRID_FRAG_SOURCE@", (vp_shaders / "infinite_grid.frag").read_text(encoding="utf-8"))
            grid_out.write_text(text, encoding="utf-8")

        gizmo_in = vp_shaders / "gizmo_shader_sources.h.in"
        gizmo_out = vp_render_gen / "gizmo_shader_sources.h"
        gizmo_srcs = [gizmo_in, vp_shaders / "gizmo_line.vert", vp_shaders / "gizmo_line.frag",
                      vp_shaders / "gizmo_solid.vert", vp_shaders / "gizmo_solid.frag"]
        if gizmo_in.exists() and vp_shaders.is_dir() and needs_regen(gizmo_out, gizmo_srcs):
            text = gizmo_in.read_text(encoding="utf-8")
            text = text.replace("@UNIVEX_GIZMO_LINE_VERT@", (vp_shaders / "gizmo_line.vert").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_GIZMO_LINE_FRAG@", (vp_shaders / "gizmo_line.frag").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_GIZMO_SOLID_VERT@", (vp_shaders / "gizmo_solid.vert").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_GIZMO_SOLID_FRAG@", (vp_shaders / "gizmo_solid.frag").read_text(encoding="utf-8"))
            gizmo_out.write_text(text, encoding="utf-8")

        sel_in = vp_shaders / "selection_outline_shader_sources.h.in"
        sel_out = vp_render_gen / "selection_outline_shader_sources.h"
        sel_srcs = [sel_in, vp_shaders / "selection_mask.vert", vp_shaders / "selection_mask.frag",
                    vp_shaders / "selection_outline.vert", vp_shaders / "selection_outline.frag"]
        if sel_in.exists() and vp_shaders.is_dir() and needs_regen(sel_out, sel_srcs):
            text = sel_in.read_text(encoding="utf-8")
            text = text.replace("@UNIVEX_SELECTION_MASK_VERT@", (vp_shaders / "selection_mask.vert").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_SELECTION_MASK_FRAG@", (vp_shaders / "selection_mask.frag").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_SELECTION_OUTLINE_VERT@", (vp_shaders / "selection_outline.vert").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_SELECTION_OUTLINE_FRAG@", (vp_shaders / "selection_outline.frag").read_text(encoding="utf-8"))
            sel_out.write_text(text, encoding="utf-8")

        # 6. Test artifacts (co-located inside module Tests/ and Engine/Editor/EditorCore/Blueprint/Testing/)
        plugin_fixtures_dir = self.repo_root / "Engine" / "Runtime" / "Plugins" / "Tests" / "Fixtures"
        bp_native_dir = self.repo_root / "Engine" / "Editor" / "EditorCore" / "Blueprint" / "Testing" / "Compiler" / "native"
        if (plugin_fixtures_dir.is_dir() or bp_native_dir.is_dir()) and bin_dir is not None:
            # Build test plugins
            plugin_fixtures = [
                (plugin_fixtures_dir / "uve_test_plugin_valid.cpp", bin_dir / "libuve_test_plugin_valid.so"),
                (plugin_fixtures_dir / "uve_test_plugin_bad_protocol.cpp", bin_dir / "libuve_test_plugin_bad_protocol.so"),
                (plugin_fixtures_dir / "uve_test_plugin_no_entry.cpp", bin_dir / "libuve_test_plugin_no_entry.so"),
            ]
            plugin_inc = self.repo_root / "Engine" / "Runtime" / "Plugins" / "Expose"
            for src, dst in plugin_fixtures:
                if src.exists() and (not dst.exists() or src.stat().st_mtime > dst.stat().st_mtime):
                    cmd = [cxx, "-std=c++23", "-fPIC", "-shared", f"-I{plugin_inc}", str(src), "-o", str(dst)]
                    subprocess.run(cmd, check=True)

            # Build Blueprint native compiler tool
            uvsc_exe = bin_dir / "uvsc"
            bp_dir = self.repo_root / "Engine" / "Editor" / "EditorCore" / "Blueprint"
            uvsc_main = bp_dir / "Compiler" / "Backend" / "blueprint_compiler_main.cpp"
            bp_srcs = [
                p for sub in ("Compiler", "Runtime")
                for p in (bp_dir / sub).rglob("*.cpp")
                if p.name != "blueprint_compiler_main.cpp"
            ]
            bp_incs = [
                bp_dir / "Compiler" / "Frontend",
                bp_dir / "Compiler" / "Intermediate",
                bp_dir / "Compiler" / "Backend",
                bp_dir / "Runtime" / "VirtualMachine",
                bp_dir / "Runtime" / "Variables",
                bp_dir / "Runtime" / "ObjectBinding",
            ]

            rebuild_uvsc = not uvsc_exe.exists()
            if not rebuild_uvsc:
                uvsc_mtime = uvsc_exe.stat().st_mtime
                if uvsc_main.stat().st_mtime > uvsc_mtime or any(s.stat().st_mtime > uvsc_mtime for s in bp_srcs):
                    rebuild_uvsc = True

            if rebuild_uvsc and uvsc_main.exists():
                inc_flags = [f"-I{gen_dir}"] + [f"-I{d}" for d in bp_incs]
                cmd = [cxx, "-std=c++23", "-O2"] + inc_flags + [str(s) for s in bp_srcs] + [str(uvsc_main), "-o", str(uvsc_exe)]
                subprocess.run(cmd, check=True)

            # Generate native Blueprint script sources
            host_file = bp_native_dir / "fake_object.uvhost"
            script_names = ["player.uvs", "math.uvs", "waits.uvs", "errors.uvs", "methods.uvs", "collections.uvs"]
            gen_script_dir = bp_native_dir / "generated"
            gen_script_dir.mkdir(parents=True, exist_ok=True)
            for sname in script_names:
                ssrc = bp_native_dir / sname
                sdst = gen_script_dir / f"{sname}.cpp"
                if uvsc_exe.exists() and host_file.exists() and ssrc.exists() and (not sdst.exists() or ssrc.stat().st_mtime > sdst.stat().st_mtime or host_file.stat().st_mtime > sdst.stat().st_mtime):
                    subprocess.run([str(uvsc_exe), "--host", str(host_file), "--out", str(sdst), str(ssrc)], check=True)

        return [gen_dir, retarget_gen, ui_gen, editor_gen, vp_render_gen.parent, vp_render_gen]

    def ensure_third_party(self, toolchain):
        platform = toolchain.platform
        tp_dir = self.repo_root / "ThirdParty"
        tp_dir.mkdir(parents=True, exist_ok=True)
        
        deps = [
            ("googletest", "https://github.com/google/googletest.git", "v1.15.2", "googletest/include/gtest/gtest.h"),
            ("nlohmann_json", "https://github.com/nlohmann/json.git", "v3.11.3", "include/nlohmann/json.hpp"),
            ("fmt", "https://github.com/fmtlib/fmt.git", "11.0.2", "include/fmt/format.h"),
            ("spirv-reflect", "https://github.com/KhronosGroup/SPIRV-Reflect.git", "vulkan-sdk-1.4.321.0", "spirv_reflect.h"),
            ("vulkan-headers", "https://github.com/KhronosGroup/Vulkan-Headers.git", "vulkan-sdk-1.4.357.0", "include/vulkan/vulkan.h"),
            ("ufbx", "https://github.com/ufbx/ufbx.git", "v0.23.0", "ufbx.h"),
            ("miniaudio", "https://github.com/mackron/miniaudio.git", "0.11.25", "miniaudio.h"),
            ("basis_universal", "https://github.com/BinomialLLC/basis_universal.git", "v2_50", "transcoder/basisu_transcoder.h"),
        ]
        
        for name, url, tag, check_file in deps:
            dest = tp_dir / name
            if not (dest / check_file).exists():
                print(f"[UVBT] Fetching ThirdParty dependency '{name}' ({tag})...", flush=True)
                saved_bcs = {}
                if dest.exists():
                    for bcs in dest.glob("*.Build.cs"):
                        try:
                            saved_bcs[bcs.name] = bcs.read_text(encoding="utf-8")
                        except Exception:
                            pass
                    if not (dest / ".git").exists():
                        shutil.rmtree(dest, ignore_errors=True)
                if not dest.exists():
                    git_clone_public(url, tag, dest)
                for bcs_name, content in saved_bcs.items():
                    (dest / bcs_name).write_text(content, encoding="utf-8")


        tp_lib_dir = self.repo_root / ".uvbt" / "tp_lib" / toolchain.output_dir
        tp_lib_dir.mkdir(parents=True, exist_ok=True)

        cxx = [toolchain.cxx] + toolchain.target_flags
        cc = [toolchain.cc] + toolchain.target_flags
        ar = toolchain.ar

        # 1. libgtest.a & libgtest_main.a
        gt_lib = tp_lib_dir / "libgtest.a"
        gt_main_lib = tp_lib_dir / "libgtest_main.a"
        gt_src = tp_dir / "googletest" / "googletest"
        if not gt_lib.exists():
            fallback = tp_dir / "lib" / "libgtest.a"
            if fallback.exists() and platform == "Linux":
                shutil.copy2(fallback, gt_lib)
            elif (gt_src / "src" / "gtest-all.cc").exists():
                obj = tp_lib_dir / "gtest-all.o"
                subprocess.run([*cxx, "-std=c++17", "-O2", f"-I{gt_src / 'include'}", f"-I{gt_src}", "-c", str(gt_src / "src" / "gtest-all.cc"), "-o", str(obj)], check=True)
                subprocess.run([ar, "rcs", str(gt_lib), str(obj)], check=True)

        if not gt_main_lib.exists():
            fallback = tp_dir / "lib" / "libgtest_main.a"
            if fallback.exists() and platform == "Linux":
                shutil.copy2(fallback, gt_main_lib)
            elif (gt_src / "src" / "gtest_main.cc").exists():
                obj = tp_lib_dir / "gtest_main.o"
                subprocess.run([*cxx, "-std=c++17", "-O2", f"-I{gt_src / 'include'}", "-c", str(gt_src / "src" / "gtest_main.cc"), "-o", str(obj)], check=True)
                subprocess.run([ar, "rcs", str(gt_main_lib), str(obj)], check=True)

        # 2. libspirv-reflect-static.a
        spv_lib = tp_lib_dir / "libspirv-reflect-static.a"
        spv_src = tp_dir / "spirv-reflect" / "spirv_reflect.c"
        if not spv_lib.exists():
            fallback = tp_dir / "lib" / "libspirv-reflect-static.a"
            if fallback.exists() and platform == "Linux":
                shutil.copy2(fallback, spv_lib)
            elif spv_src.exists():
                obj = tp_lib_dir / "spirv_reflect.o"
                subprocess.run([*cc, "-O2", "-fPIC", "-c", str(spv_src), "-o", str(obj)], check=True)
                subprocess.run([ar, "rcs", str(spv_lib), str(obj)], check=True)

        # 3. libuve_ufbx.a
        ufbx_lib = tp_lib_dir / "libuve_ufbx.a"
        ufbx_src = tp_dir / "ufbx" / "ufbx.c"
        if not ufbx_lib.exists():
            fallback = tp_dir / "lib" / "libuve_ufbx.a"
            if fallback.exists() and platform == "Linux":
                shutil.copy2(fallback, ufbx_lib)
            elif ufbx_src.exists():
                obj = tp_lib_dir / "ufbx.o"
                subprocess.run([*cc, "-O2", "-fPIC", f"-I{tp_dir / 'ufbx'}", "-c", str(ufbx_src), "-o", str(obj)], check=True)
                subprocess.run([ar, "rcs", str(ufbx_lib), str(obj)], check=True)

        basis_defs = [
            "-DBASISD_USE_UNALIGNED_WORD_READS=0",
            "-DBASISD_SUPPORT_KTX2=1",
            "-DBASISD_SUPPORT_KTX2_ZSTD=0",
            "-DBASISU_SUPPORT_OPENCL=0",
            "-DBASISU_SUPPORT_SSE=0",
            "-DBASISU_SUPPORT_ASTCENC=0",
            "-DBASISU_DISABLE_ANDROID_ASTC_DECOMP=1",
        ]

        # 4. libuve_basisu_transcoder.a
        bu_tc_lib = tp_lib_dir / "libuve_basisu_transcoder.a"
        bu_tc_src = tp_dir / "basis_universal" / "transcoder" / "basisu_transcoder.cpp"
        if bu_tc_src.exists() and (not bu_tc_lib.exists() or bu_tc_src.stat().st_mtime > bu_tc_lib.stat().st_mtime):
            obj = tp_lib_dir / "basisu_transcoder.o"
            subprocess.run([*cxx, "-std=c++17", "-O2", "-fPIC", "-fno-strict-aliasing"] + basis_defs + [f"-I{tp_dir / 'basis_universal' / 'transcoder'}", f"-I{tp_dir / 'basis_universal' / 'encoder'}", "-c", str(bu_tc_src), "-o", str(obj)], check=True)
            subprocess.run([ar, "rcs", str(bu_tc_lib), str(obj)], check=True)

        # 5. libuve_basisu_encoder.a
        bu_enc_lib = tp_lib_dir / "libuve_basisu_encoder.a"
        bu_enc_dir = tp_dir / "basis_universal" / "encoder"
        if bu_enc_dir.is_dir() and not bu_enc_lib.exists():
            enc_cpps = list(bu_enc_dir.glob("*.cpp"))
            enc_objs = []
            for ec in enc_cpps:
                obj = tp_lib_dir / f"{ec.stem}.o"
                subprocess.run([*cxx, "-std=c++17", "-O2", "-fPIC", "-fno-strict-aliasing"] + basis_defs + [f"-I{tp_dir / 'basis_universal' / 'transcoder'}", f"-I{bu_enc_dir}", "-c", str(ec), "-o", str(obj)], check=True)
                enc_objs.append(str(obj))
            subprocess.run([ar, "rcs", str(bu_enc_lib)] + enc_objs, check=True)

        if toolchain.is_android:
            self._ensure_android_libraries(toolchain, tp_dir, tp_lib_dir)
        elif platform == "Linux":
            self._ensure_linux_libraries(toolchain, tp_dir, tp_lib_dir)

    # Third-party libraries the desktop takes from the system but Android does not have: fetched at a
    # fixed release and built with the NDK through CMake. Each installs under tp_lib/<platform>/<name>.
    ANDROID_CMAKE_LIBRARIES = (
        ("freetype", "https://github.com/freetype/freetype.git", "VER-2-13-3", "libfreetype.a",
         ["-DFT_DISABLE_ZLIB=ON", "-DFT_DISABLE_BZIP2=ON", "-DFT_DISABLE_PNG=ON", "-DFT_DISABLE_HARFBUZZ=ON",
          "-DFT_DISABLE_BROTLI=ON"]),
        ("libjpeg-turbo", "https://github.com/libjpeg-turbo/libjpeg-turbo.git", "3.1.0", "libjpeg.a",
         ["-DENABLE_SHARED=OFF", "-DWITH_TURBOJPEG=OFF", "-DWITH_SIMD=OFF"]),
    )

    # GLFW's header only: on Android the editor host's window calls are answered by the engine's own
    # Android implementation of that API (Engine/Runtime/Window), not by GLFW.
    ANDROID_GLFW_HEADERS = ("glfw", "https://github.com/glfw/glfw.git", "3.4")

    def _ensure_android_libraries(self, toolchain, tp_dir: Path, tp_lib_dir: Path):
        name, url, tag = self.ANDROID_GLFW_HEADERS
        glfw = tp_dir / "android" / name
        if not (glfw / "include" / "GLFW" / "glfw3.h").exists():
            print(f"[UVBT] Fetching Android dependency '{name}' headers ({tag})...", flush=True)
            git_clone_public(url, tag, glfw)

        # android_native_app_glue: the NDK's bridge from NativeActivity to android_main.
        glue_lib = tp_lib_dir / "libandroid_native_app_glue.a"
        glue_src = toolchain.ndk / "sources" / "android" / "native_app_glue" / "android_native_app_glue.c"
        if not glue_lib.exists():
            obj = tp_lib_dir / "android_native_app_glue.o"
            subprocess.run([toolchain.cc, *toolchain.target_flags, "-O2", "-fPIC", "-c", str(glue_src), "-o", str(obj)],
                           check=True)
            subprocess.run([toolchain.ar, "rcs", str(glue_lib), str(obj)], check=True)

        for name, url, tag, library, options in self.ANDROID_CMAKE_LIBRARIES:
            install = tp_lib_dir / name
            if (install / "lib" / library).exists():
                continue
            source = tp_dir / "android" / name
            if not source.is_dir():
                print(f"[UVBT] Fetching Android dependency '{name}' ({tag})...", flush=True)
                source.parent.mkdir(parents=True, exist_ok=True)
                git_clone_public(url, tag, source)
            build = tp_lib_dir / f"{name}-build"
            print(f"[UVBT] Building Android dependency '{name}' for {toolchain.abi}...", flush=True)
            subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                            "-DBUILD_SHARED_LIBS=OFF", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                            f"-DCMAKE_INSTALL_PREFIX={install}", "-DCMAKE_INSTALL_LIBDIR=lib",
                            *toolchain.cmake_toolchain_args(), *options], check=True)
            subprocess.run(["cmake", "--build", str(build)], check=True)
            subprocess.run(["cmake", "--install", str(build)], check=True)

    @staticmethod
    def _want_static_linux_ui() -> bool:
        return os.environ.get("UVBT_STATIC_LINUX_UI", "").strip().lower() in ("1", "true", "yes")

    @staticmethod
    def _system_header_exists(relative_header: str) -> bool:
        return any((root / relative_header).is_file() for root in (Path("/usr/include"), Path("/usr/local/include")))

    @staticmethod
    def _resolve_static_system_lib(compiler: str, lib_name: str):
        res = subprocess.run([compiler, f"-print-file-name=lib{lib_name}.a"], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True, check=False)
        candidate = Path(res.stdout.strip())
        return candidate if candidate.is_file() else None

    def _ensure_linux_libraries(self, toolchain, tp_dir: Path, tp_lib_dir: Path):
        static_ui = self._want_static_linux_ui()
        need_glfw = static_ui or not self._system_header_exists("GLFW/glfw3.h")
        if need_glfw:
            glfw_lib = tp_lib_dir / "libglfw3.a"
            glfw_src = tp_dir / "glfw"
            if not (glfw_src / "include" / "GLFW" / "glfw3.h").is_file():
                name, url, tag = LINUX_GLFW_REPO
                print(f"[UVBT] Fetching Linux dependency '{name}' ({tag})...", flush=True)
                git_clone_public(url, tag, glfw_src)
            if not glfw_lib.exists():
                glfw_build = tp_lib_dir / "glfw-build"
                print("[UVBT] Building static libglfw3.a for Linux...", flush=True)
                subprocess.run(
                    ["cmake", "-S", str(glfw_src), "-B", str(glfw_build), "-G", "Ninja",
                     "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_SHARED_LIBS=OFF",
                     "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                     "-DGLFW_BUILD_EXAMPLES=OFF", "-DGLFW_BUILD_TESTS=OFF", "-DGLFW_BUILD_DOCS=OFF",
                     "-DGLFW_BUILD_WAYLAND=OFF", "-DGLFW_BUILD_X11=ON",
                     *toolchain.cmake_toolchain_args()],
                    check=True,
                )
                subprocess.run(["cmake", "--build", str(glfw_build)], check=True)
                built = glfw_build / "src" / "libglfw3.a"
                if not built.is_file():
                    raise RuntimeError(f"[UVBT] Expected {built} after building GLFW")
                shutil.copy2(built, glfw_lib)

        need_glew = static_ui or not self._system_header_exists("GL/glew.h")
        if need_glew:
            glew_lib = tp_lib_dir / "libGLEW.a"
            glew_src = tp_dir / "glew"
            if not (glew_src / "include" / "GL" / "glew.h").is_file():
                name, url, tag = LINUX_GLEW_REPO
                print(f"[UVBT] Fetching Linux dependency '{name}' ({tag})...", flush=True)
                git_clone_public(url, tag, glew_src)
            if not glew_lib.exists():
                print("[UVBT] Building static libGLEW.a for Linux...", flush=True)
                glew_obj = tp_lib_dir / "glew.o"
                subprocess.run(
                    [toolchain.cc, *toolchain.target_flags, "-O2", "-fPIC", "-DGLEW_NO_GLU", "-DGLEW_STATIC",
                     f"-I{glew_src / 'include'}", "-c", str(glew_src / "src" / "glew.c"), "-o", str(glew_obj)],
                    check=True,
                )
                subprocess.run([toolchain.ar, "rcs", str(glew_lib), str(glew_obj)], check=True)

    def android_library_dirs(self, toolchain):
        """Where the Android third-party libraries are installed, for the link and includes."""
        tp_lib_dir = self.repo_root / ".uvbt" / "tp_lib" / toolchain.output_dir
        return [tp_lib_dir] + [tp_lib_dir / name / "lib" for name, *_ in self.ANDROID_CMAKE_LIBRARIES]

    def android_include_dirs(self, toolchain):
        tp_lib_dir = self.repo_root / ".uvbt" / "tp_lib" / toolchain.output_dir
        glfw = self.repo_root / "ThirdParty" / "android" / self.ANDROID_GLFW_HEADERS[0] / "include"
        return [toolchain.ndk / "sources" / "android" / "native_app_glue", glfw] + \
               [tp_lib_dir / name / "include" for name, *_ in self.ANDROID_CMAKE_LIBRARIES]

    def build_target(self, target_name: str, platform: str = "Linux", configuration: str = "Debug", jobs: int = 4):
        target = self.targets.get(target_name)
        if not target:
            raise ValueError(f"Target '{target_name}' not found. Registered targets: {list(self.targets.keys())}")

        toolchain = Toolchain(platform)
        if toolchain.is_android and target.target_type == "Test":
            raise ValueError(f"Target '{target_name}' is a test program; tests run on the desktop platforms.")

        # Prepare output directories
        uvbt_root = self.repo_root / ".uvbt"
        obj_dir = uvbt_root / "obj" / toolchain.output_dir / configuration
        lib_dir = uvbt_root / "lib" / toolchain.output_dir / configuration
        bin_dir = uvbt_root / "bin" / toolchain.output_dir / configuration
        cache_file = uvbt_root / "cache.json"

        for d in (obj_dir, lib_dir, bin_dir):
            d.mkdir(parents=True, exist_ok=True)

        self.ensure_third_party(toolchain)
        # Host-side test tools and fixtures are only built for the desktop.
        generated_includes = self.generate_prebuild_artifacts(uvbt_root, None if toolchain.is_android else bin_dir)
        cache = BuildCache(cache_file)

        # Refresh discovery in case new sources were generated, applying this platform's rules
        self.discover(platform)
        target = self.targets.get(target_name)

        if "CoreTests" in self.modules:
            self.modules["CoreTests"].private_defs.extend([
                f'UVE_TEST_PLUGIN_VALID_PATH="{bin_dir / "libuve_test_plugin_valid.so"}"',
                f'UVE_TEST_PLUGIN_BAD_PROTOCOL_PATH="{bin_dir / "libuve_test_plugin_bad_protocol.so"}"',
                f'UVE_TEST_PLUGIN_NO_ENTRY_PATH="{bin_dir / "libuve_test_plugin_no_entry.so"}"',
                f'UVE_UVSCRIPT_NATIVE_DIR="{self.repo_root / "Engine" / "Editor" / "EditorCore" / "Blueprint" / "Testing" / "Compiler" / "native"}"',
            ])

        print(f"\n[UVBT] ================================================================")
        print(f"[UVBT] Building Target: {target.name} | {platform} | {configuration}")
        print(f"[UVBT] Root Modules: {', '.join(target.extra_modules)}")
        print(f"[UVBT] ================================================================\n")

        ordered_modules = self.resolve_dependencies(target.extra_modules)
        print(f"[UVBT] Resolved {len(ordered_modules)} modules in dependency order:")
        for m in ordered_modules:
            print(f"  -> {m.name} ({len(m.source_files)} source files)")

        # Collect global include paths
        global_includes = [
            self.repo_root / "Engine" / "Runtime",
            self.repo_root / "Engine" / "Editor",
            self.repo_root / "Engine" / "Source",
        ] + generated_includes
        
        # Third-party / vendored deps in ThirdParty/
        third_party_dirs = [
            self.repo_root / "ThirdParty" / "googletest" / "googletest" / "include",
            self.repo_root / "ThirdParty" / "googletest" / "googlemock" / "include",
            self.repo_root / "ThirdParty" / "nlohmann_json" / "include",
            self.repo_root / "ThirdParty" / "fmt" / "include",
            self.repo_root / "ThirdParty" / "spirv-reflect",
            self.repo_root / "ThirdParty" / "vulkan-headers" / "include",
            self.repo_root / "ThirdParty" / "ufbx",
            self.repo_root / "ThirdParty" / "miniaudio",
            self.repo_root / "ThirdParty" / "basis_universal" / "transcoder",
            self.repo_root / "ThirdParty" / "basis_universal" / "encoder",
            self.repo_root / "ThirdParty" / "basis_universal",
            self.repo_root / "ThirdParty" / "glfw" / "include",
            self.repo_root / "ThirdParty" / "glew" / "include",
        ]
        for tp in third_party_dirs:
            if tp.is_dir():
                global_includes.append(tp)

        # System FreeType (PRISM text rendering). pkg-config first, then the usual install prefixes
        # (Debian/Ubuntu /usr/include/freetype2, MSYS2 <prefix>/include/freetype2 next to g++).
        for ft_inc in self._freetype_include_dirs(toolchain):
            if ft_inc not in global_includes:
                global_includes.append(ft_inc)
        if toolchain.is_android:
            global_includes += [d for d in self.android_include_dirs(toolchain) if d.is_dir()]

        # Base compiler flags
        cxx = toolchain.cxx
        ar = toolchain.ar

        base_cflags = toolchain.target_flags + [
            "-std=c++23",
            "-Wall", "-Wextra", "-Wpedantic",
            "-fPIC",
            "-pthread"
        ]
        if toolchain.is_android:
            # Isolate internal symbols so dlopen(uve_editor) never collides with libunivex_hub.so.
            base_cflags.append("-fvisibility=hidden")
            if configuration in ("Release", "Shipping"):
                base_cflags += ["-ffunction-sections", "-fdata-sections"]
        if os.environ.get("UVBT_WERROR", "").strip() in ("1", "true", "yes"):
            base_cflags.append("-Werror")

        # Engine build profile definitions
        if configuration == "Shipping":
            base_defs = [
                "UVE_BUILD_CONFIGURATION_SHIPPING=1",
                "UVE_SHIPPING=1",
                "UVE_PROFILE_ASSERTIONS_ENABLED=0",
                "UVE_PROFILE_DEFAULT_LOG_LEVEL=3",
                "UVE_COMPILED_LOG_LEVEL=3",
            ]
            base_cflags += ["-O3", "-DNDEBUG"]
        elif configuration == "Release":
            base_defs = [
                "UVE_BUILD_CONFIGURATION_RELEASE=1",
                "UVE_SHIPPING=0",
                "UVE_PROFILE_ASSERTIONS_ENABLED=0",
                "UVE_PROFILE_DEFAULT_LOG_LEVEL=2",
                "UVE_COMPILED_LOG_LEVEL=0",
            ]
            base_cflags += ["-O3", "-DNDEBUG"]
        elif configuration == "Development":
            base_defs = [
                "UVE_BUILD_CONFIGURATION_DEVELOPMENT=1",
                "UVE_SHIPPING=0",
                "UVE_PROFILE_ASSERTIONS_ENABLED=1",
                "UVE_PROFILE_DEFAULT_LOG_LEVEL=1",
                "UVE_COMPILED_LOG_LEVEL=0",
            ]
            base_cflags += ["-O2", "-g"]
        else: # Debug or Sanitizers
            base_defs = [
                "UVE_BUILD_CONFIGURATION_DEBUG=1",
                "UVE_SHIPPING=0",
                "UVE_PROFILE_ASSERTIONS_ENABLED=1",
                "UVE_PROFILE_DEFAULT_LOG_LEVEL=0",
                "UVE_COMPILED_LOG_LEVEL=0",
            ]
            base_cflags += ["-g", "-O0"]

        base_defs += [
            f"UVE_HAS_STD_FORMAT={1 if toolchain.has_std_format else 0}",
            # GLFW's header must not pull in desktop GL; GL code includes gl_system_headers.h itself.
            *(["GLFW_INCLUDE_NONE"] if toolchain.is_android else []),
            *(["GLEW_NO_GLU"] if platform == "Linux" else []),
            "FMT_HEADER_ONLY=1",
            f'UVE_SOURCE_DIR="{self.repo_root}"'
        ]

        if configuration == "Sanitizers":
            base_cflags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]

        # Optional compiler launcher for the per-source compiles (UVBT_COMPILER_LAUNCHER=ccache):
        # a content-hashed cache turns an unchanged file in a fresh checkout into a cache hit.
        launcher = []
        launcher_name = os.environ.get("UVBT_COMPILER_LAUNCHER", "").strip()
        if launcher_name:
            launcher_path = shutil.which(launcher_name)
            if launcher_path:
                launcher = [launcher_path]
                print(f"[UVBT] Compiler launcher: {launcher_path}")
            else:
                print(f"[UVBT] Compiler launcher '{launcher_name}' not found; compiling without it")

        # Compile tasks
        compile_tasks = []
        module_objects = {}

        for mod in ordered_modules:
            mod_obj_dir = obj_dir / mod.name
            mod_obj_dir.mkdir(parents=True, exist_ok=True)
            module_objects[mod.name] = []

            # Determine include paths for this module:
            # - Its own public & private includes
            # - Public includes of all direct and transitive dependencies
            mod_includes = list(global_includes)
            mod_includes.extend(mod.public_includes)
            mod_includes.extend(mod.private_includes)

            dep_modules = self.resolve_dependencies(mod.public_deps + mod.private_deps)
            for dm in dep_modules:
                mod_includes.extend(dm.public_includes)

            # Deduplicate includes
            seen_inc = set()
            clean_includes = []
            for inc in mod_includes:
                s = str(inc)
                if s not in seen_inc:
                    seen_inc.add(s)
                    clean_includes.append(inc)

            inc_flags = [f"-I{inc}" for inc in clean_includes]
            def_flags = [f"-D{d}" for d in base_defs + mod.public_defs + mod.private_defs + target.global_defs]

            for src in mod.source_files:
                out_obj = mod_obj_dir / (src.stem + ".o")
                out_dep = mod_obj_dir / (src.stem + ".d")
                module_objects[mod.name].append(out_obj)

                cmd = launcher + [cxx] + base_cflags + inc_flags + def_flags + [
                    "-MMD", "-MF", str(out_dep),
                    "-c", str(src),
                    "-o", str(out_obj)
                ]
                cmd_str = " ".join(cmd)

                if not cache.is_action_up_to_date(out_obj, src, out_dep, cmd_str):
                    compile_tasks.append((mod.name, src, out_obj, cmd, cmd_str))

        total_compiles = len(compile_tasks)
        print(f"[UVBT] Actions to compile: {total_compiles} (out of {sum(len(m.source_files) for m in ordered_modules)} total sources)")

        # Execute compilations in parallel
        start_time = time.time()
        completed = 0
        failed = False

        def run_compile(task):
            mod_name, src, out_obj, cmd, cmd_str = task
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            return (res.returncode, mod_name, src, out_obj, cmd_str, res.stdout, res.stderr)

        if total_compiles > 0:
            with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
                futures = [executor.submit(run_compile, t) for t in compile_tasks]
                for fut in concurrent.futures.as_completed(futures):
                    rc, mod_name, src, out_obj, cmd_str, stdout, stderr = fut.result()
                    completed += 1
                    status = f"[{completed}/{total_compiles}]"
                    if rc != 0:
                        failed = True
                        print(f"\n\033[91m{status} FAILED: {mod_name} :: {src.name}\033[0m", flush=True)
                        if stdout:
                            print(stdout, flush=True)
                        if stderr:
                            print(stderr, flush=True)
                        break
                    else:
                        print(f"{status} Compiling {mod_name} :: {src.name}", flush=True)
                        cache.record_action(out_obj, src, cmd_str)
                        if completed % 5 == 0:
                            cache.save()

        if failed:
            cache.save()
            raise RuntimeError("[UVBT] Compilation failed. See compiler output above.")

        cache.save()
        compile_duration = time.time() - start_time
        if total_compiles > 0:
            print(f"[UVBT] Compilations completed in {compile_duration:.2f}s")
        else:
            print("[UVBT] All module object files are up to date.")

        # Archive static libraries
        module_libs = {}
        for mod in ordered_modules:
            if mod.module_type in ("StaticLibrary", "Executable"):
                lib_file = lib_dir / f"lib{mod.name}.a"
                module_libs[mod.name] = lib_file
                objs = module_objects[mod.name]
                if objs:
                    ar_cmd = [ar, "rcs", str(lib_file)] + [str(o) for o in objs]
                    subprocess.run(ar_cmd, check=True)

        # Link executable if TargetType is Test or Program. On Android a program is the shared library
        # the activity loads (lib<name>.so).
        if platform == "Windows":
            exe_name = target.output_name if target.output_name.endswith(".exe") else f"{target.output_name}.exe"
            all_sys_libs = ["pthread", "opengl32", "gdi32", "glfw3", "glew32", "z", "jpeg", "dbghelp", "ws2_32", "winmm", "ole32"]
        elif toolchain.is_android:
            exe_name = f"lib{target.output_name}.so"
            all_sys_libs = list(ANDROID_SYSTEM_LIBRARIES)
        else:
            exe_name = target.output_name
            all_sys_libs = ["pthread", "dl", "m", "GL", "GLX", "X11"]
        target_exe = bin_dir / exe_name
        print(f"\n[UVBT] Linking {'shared library' if toolchain.is_android else 'executable'}: {target_exe}")

        all_objs = []
        all_libs = []

        tp_lib_dir = uvbt_root / "tp_lib" / toolchain.output_dir

        # If testing, add gtest libraries
        if target.target_type == "Test":
            for glib_name in ["libgtest.a", "libgtest_main.a"]:
                glib = tp_lib_dir / glib_name
                if not glib.exists():
                    glib = self.repo_root / "ThirdParty" / "lib" / glib_name
                if glib.exists():
                    all_libs.append(str(glib))

        # Third-party libraries
        for tlib_name in ["libuve_ufbx.a", "libuve_basisu_encoder.a", "libuve_basisu_transcoder.a", "libspirv-reflect-static.a"]:
            tlib = tp_lib_dir / tlib_name
            if not tlib.exists() and platform == "Linux":
                tlib = self.repo_root / "ThirdParty" / "lib" / tlib_name
            if tlib.exists():
                all_libs.append(str(tlib))

        if platform == "Linux":
            has_glfw = False
            for gl_cand in [tp_lib_dir / "libglfw3.a", self.repo_root / "ThirdParty" / "glfw" / "lib" / "libglfw3.a"]:
                if gl_cand.exists():
                    all_libs.append(str(gl_cand))
                    has_glfw = True
                    break
            if not has_glfw:
                all_sys_libs.append("glfw")

            has_glew = False
            for glew_cand in [tp_lib_dir / "libGLEW.a", self.repo_root / "ThirdParty" / "glew" / "lib" / "libGLEW.a"]:
                if glew_cand.exists():
                    all_libs.append(str(glew_cand))
                    has_glew = True
                    break
            if not has_glew:
                all_sys_libs.append("GLEW")

        for mod in ordered_modules:
            for sys_lib in mod.public_sys_libs + mod.private_sys_libs:
                # "win:<lib>" / "linux:<lib>" restrict a system library to one platform.
                # Platform qualifiers ("win:", "linux:", ...) were applied when the Build.cs was read.
                all_sys_libs.append(sys_lib)
            lib_p = module_libs.get(mod.name)
            if lib_p and lib_p.exists():
                all_libs.append(str(lib_p))

        # Deduplicate sys libs and filter out platform-incompatible libraries
        unique_sys = []
        for s in all_sys_libs:
            if platform == "Windows" and s in ("dl", "X11", "GL", "GLX", "m"):
                continue
            # Bionic has threads in libc and no desktop GL or X11.
            if toolchain.is_android and s in ("pthread", "rt", "X11", "GL", "GLX"):
                continue
            if s not in unique_sys:
                unique_sys.append(s)

        if platform == "Linux" and self._want_static_linux_ui():
            static_candidates = []
            for lib_name in ("freetype", "jpeg", "z"):
                if lib_name in unique_sys:
                    static_candidates.append(lib_name)
                    if lib_name == "freetype":
                        try:
                            pc_out = subprocess.run(
                                ["pkg-config", "--static", "--libs-only-l", "freetype2"],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, check=False,
                            ).stdout
                            for tok in pc_out.split():
                                if tok.startswith("-l"):
                                    dep = tok[2:]
                                    if dep not in ("m", "dl", "pthread", "c", "rt") and dep not in static_candidates:
                                        static_candidates.append(dep)
                        except OSError:
                            pass
            for lib_name in static_candidates:
                static_path = self._resolve_static_system_lib(cxx, lib_name)
                if static_path is not None:
                    if str(static_path) not in all_libs:
                        all_libs.append(str(static_path))
                    if lib_name in unique_sys:
                        unique_sys.remove(lib_name)
                elif lib_name not in unique_sys:
                    unique_sys.append(lib_name)

        link_cmd = [cxx] + base_cflags + ["-o", str(target_exe)]
        if platform == "Linux":
            link_cmd.append("-Wl,--as-needed")
            if target.target_type != "Test":
                link_cmd += ["-Wl,-rpath,$ORIGIN/lib:$ORIGIN", "-Wl,--disable-new-dtags"]
        if toolchain.is_android:
            export_map = obj_dir / "android_exports.map"
            # Glob form: only the editor library defines the entries, and lld rejects unmatched exact names.
            editor_entry_glob = f"{ANDROID_EDITOR_ENTRY[:-1]}[{ANDROID_EDITOR_ENTRY[-1]}]"
            editor_abi_glob = f"{ANDROID_EDITOR_ABI_VERSION_ENTRY[:-1]}[{ANDROID_EDITOR_ABI_VERSION_ENTRY[-1]}]"
            export_map.write_text(
                f"{{\n  global:\n    {ANDROID_ACTIVITY_ENTRY};\n    {editor_entry_glob};\n    {editor_abi_glob};\n  local:\n    *;\n}};\n",
                encoding="utf-8",
            )
            link_cmd += ["-shared", "-static-libstdc++", f"-Wl,-u,{ANDROID_ACTIVITY_ENTRY}", "-Wl,--no-undefined",
                         "-Wl,-Bsymbolic", f"-Wl,--version-script={export_map}",
                         f"-Wl,-z,max-page-size={ANDROID_PAGE_SIZE_BYTES}"]
            if configuration in ("Release", "Shipping"):
                link_cmd.append("-Wl,--gc-sections")
            link_cmd += [f"-L{d}" for d in self.android_library_dirs(toolchain) if d.is_dir()]
            all_libs.append(str(tp_lib_dir / "libandroid_native_app_glue.a"))
        if platform == "Windows" and target.is_gui_app:
            link_cmd.append("-mwindows")
        if platform == "Windows" and "uiautomationcore" in unique_sys:
            # MinGW ships no UIAutomationCore import library: build one from the exported names PRISM uses.
            self._ensure_uiautomation_import_lib(lib_dir)
            link_cmd.append(f"-L{lib_dir}")
        sys_flags = [f"-l{s}" for s in unique_sys]
        if target.target_type == "Test":
            root_test_libs = [str(module_libs[m]) for m in target.extra_modules if m in module_libs]
            other_libs = [l for l in all_libs if l not in root_test_libs]
            link_cmd += ["-Wl,--whole-archive"] + root_test_libs + ["-Wl,--no-whole-archive"]
            link_cmd += ["-Wl,--start-group"] + other_libs + sys_flags + ["-Wl,--end-group"]
        else:
            # The target's own root modules are linked as plain object files ahead of every archive.
            # Archives are scanned in dependency order, so a dependency that carries its own
            # main() (e.g. the Viewport demo app) would otherwise win symbol resolution over the
            # real entry point of the root module and the wrong program would be produced.
            root_objs = [str(o) for m in target.extra_modules for o in module_objects.get(m, [])]
            if platform == "Windows":
                root_objs += self._compile_resources(target, obj_dir, cxx)
            root_libs = {str(module_libs[m]) for m in target.extra_modules if m in module_libs}
            other_libs = [l for l in all_libs if l not in root_libs]
            link_cmd += root_objs
            link_cmd += ["-Wl,--start-group"] + other_libs + sys_flags + ["-Wl,--end-group"]

        link_res = subprocess.run(link_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if link_res.returncode != 0:
            print(f"\033[91m[UVBT] Link failed:\033[0m")
            print(link_res.stdout)
            print(link_res.stderr)
            raise RuntimeError("[UVBT] Link failed.")

        if any(m.name == "Prism" for m in ordered_modules):
            # PRISM loads fonts / themes / locales from disk: ship them next to the binary.
            prism_assets = self.repo_root.joinpath(*PRISM_ASSETS_SOURCE)
            if prism_assets.is_dir():
                shutil.copytree(prism_assets, bin_dir / PRISM_ASSETS_DIRECTORY, dirs_exist_ok=True)

        if any(m.name == "Pack" for m in ordered_modules):
            # New projects are created from the templates shipped beside the binary.
            project_templates = self.repo_root.joinpath(*PROJECT_TEMPLATES_SOURCE)
            if project_templates.is_dir():
                shutil.copytree(project_templates, bin_dir / PROJECT_TEMPLATES_DIRECTORY, dirs_exist_ok=True)

        if any(m.name == "Online" for m in ordered_modules):
            # Where the launcher finds engine releases.
            engine_feed = self.repo_root.joinpath(*DISTRIBUTION_SOURCE, ENGINE_FEED_FILE)
            if engine_feed.is_file():
                shutil.copy2(engine_feed, bin_dir / ENGINE_FEED_FILE)

        print(f"\033[92m[UVBT] BUILD SUCCESSFUL!\033[0m")
        print(f"[UVBT] Output Binary: {target_exe}")
        return target_exe


def main():
    parser = argparse.ArgumentParser(description="UniVexBuildTool (UVBT)")
    parser.add_argument("command", nargs="?", default="build", choices=["build", "clean", "list-modules", "list-targets"], help="Command")
    parser.add_argument("target", nargs="?", default="UniVexTests", help="Target name")
    parser.add_argument("platform", nargs="?", default="Linux", help="Platform")
    parser.add_argument("config", nargs="?", default="Debug", help="Configuration")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4, help="Parallel jobs")
    parser.add_argument("--list-modules", action="store_true", help="List registered modules")
    parser.add_argument("--list-targets", action="store_true", help="List registered targets")

    args = parser.parse_args()
    cur = Path(__file__).resolve().parent
    repo_root = cur.parents[2]
    while cur != cur.parent:
        if (cur / "Engine").is_dir() and ((cur / ".git").is_dir() or (cur / "ThirdParty").is_dir()):
            repo_root = cur
            break
        cur = cur.parent
    builder = EngineBuilder(repo_root)
    builder.discover()

    if args.list_modules or args.command == "list-modules":
        print(f"[UVBT] Discovered {len(builder.modules)} modules:")
        for name in sorted(builder.modules.keys()):
            m = builder.modules[name]
            print(f"  - {name:<25} ({m.module_type}) [{len(m.source_files)} sources]")
        return 0

    if args.list_targets or args.command == "list-targets":
        print(f"[UVBT] Discovered {len(builder.targets)} targets:")
        for name in sorted(builder.targets.keys()):
            t = builder.targets[name]
            print(f"  - {name:<25} ({t.target_type}) [Root Modules: {', '.join(t.extra_modules)}]")
        return 0

    if args.command == "clean":
        out_dir = repo_root / ".uvbt"
        if out_dir.exists():
            shutil.rmtree(out_dir)
            print(f"[UVBT] Cleaned {out_dir}")
        return 0

    if args.command == "build":
        try:
            builder.build_target(args.target, args.platform, args.config, args.jobs)
            return 0
        except Exception as e:
            print(f"[UVBT] Error: {e}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
