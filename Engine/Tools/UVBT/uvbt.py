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
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


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

    def __repr__(self):
        return f"<Target {self.name} ({self.target_type})>"


class CSharpParser:
    """Parses *.Build.cs and *.Target.cs files without requiring Roslyn or .NET SDK."""

    @staticmethod
    def parse_build_cs(filepath: Path) -> ModuleInfo:
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
            return [s.replace('\\"', '"').replace('\\\\', '\\') for s in raw_matches]

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

        # Discover source files (.cpp, .c)
        sources = []
        if mod.module_type != "HeaderOnly":
            extra_dirs = [(mod_dir / d).resolve() for d in mod.extra_source_dirs if (mod_dir / d).is_dir()]
            search_dirs = [mod_dir] + extra_dirs
            seen_roots = set()
            for s_dir in search_dirs:
                for root_dir, _, files in os.walk(s_dir):
                    rpath = Path(root_dir).resolve()
                    if rpath in seen_roots or ".git" in rpath.parts or "build" in rpath.parts or ".uvbt" in rpath.parts:
                        continue
                    seen_roots.add(rpath)
                    for f in files:
                        if f.endswith((".cpp", ".c", ".cc")):
                            full_src = rpath / f
                            try:
                                rel_str = str(full_src.relative_to(mod_dir)).replace("\\", "/")
                            except ValueError:
                                rel_str = str(full_src.relative_to(s_dir)).replace("\\", "/")
                            if f not in mod.excluded_files and not any(ex in rel_str for ex in mod.excluded_files):
                                sources.append(full_src)
        mod.source_files = sorted(list(dict.fromkeys(sources)))

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

    def discover(self):
        # Scan for *.Build.cs
        for root_dir, _, files in os.walk(self.repo_root):
            rpath = Path(root_dir)
            if ".git" in rpath.parts or "build" in rpath.parts or ".uvbt" in rpath.parts:
                continue
            for f in files:
                if f.endswith(".Build.cs"):
                    mod = CSharpParser.parse_build_cs(rpath / f)
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

    def generate_prebuild_artifacts(self, uvbt_root: Path):
        gen_dir = uvbt_root / "generated"
        gen_dir.mkdir(parents=True, exist_ok=True)
        embed_script = self.repo_root / "Engine" / "Tools" / "embed_file.py"

        # 1. Retarget humanoid reference
        retarget_gen = gen_dir / "Retarget"
        retarget_gen.mkdir(parents=True, exist_ok=True)
        retarget_inc = retarget_gen / "humanoid_reference_json.inc"
        retarget_src = self.repo_root / "Engine" / "Runtime" / "Retarget" / "Runtime" / "Data" / "humanoid_reference.json"
        if retarget_src.exists() and (not retarget_inc.exists() or retarget_src.stat().st_mtime > retarget_inc.stat().st_mtime):
            subprocess.run([sys.executable, str(embed_script), str(retarget_src), str(retarget_inc), "uve_retarget_humanoid_json"], check=True)

        # 2. UI runtime font
        ui_gen = gen_dir / "UI"
        ui_gen.mkdir(parents=True, exist_ok=True)
        ui_inc = ui_gen / "ui_font_ttf_bytes.inc"
        ui_src = self.repo_root / "Engine" / "Runtime" / "UI" / "assets" / "fonts" / "liberation-sans-subset.ttf"
        if ui_src.exists() and (not ui_inc.exists() or ui_src.stat().st_mtime > ui_inc.stat().st_mtime):
            subprocess.run([sys.executable, str(embed_script), str(ui_src), str(ui_inc), "uve_ui_runtime_font_ttf_bytes"], check=True)

        # 3. Editor fonts
        editor_gen = gen_dir / "EditorCore"
        editor_gen.mkdir(parents=True, exist_ok=True)
        fonts = [
            (self.repo_root / "Engine" / "Runtime" / "UI" / "assets" / "fonts" / "liberation-sans-subset.ttf", editor_gen / "uve_ui_font_bytes.inc", "uve_ui_font_ttf_bytes"),
            (self.repo_root / "Engine" / "Editor" / "EditorCore" / "assets" / "fonts" / "liberation-mono-subset.ttf", editor_gen / "uve_mono_font_bytes.inc", "uve_mono_font_ttf_bytes"),
            (self.repo_root / "Engine" / "Editor" / "EditorCore" / "assets" / "fonts" / "tabler-icons-subset.ttf", editor_gen / "uve_icon_font_bytes.inc", "uve_icon_font_ttf_bytes"),
        ]
        for src, dst, sym in fonts:
            if src.exists() and (not dst.exists() or src.stat().st_mtime > dst.stat().st_mtime):
                subprocess.run([sys.executable, str(embed_script), str(src), str(dst), sym], check=True)

        return [gen_dir, retarget_gen, ui_gen, editor_gen]

    def build_target(self, target_name: str, platform: str = "Linux", configuration: str = "Debug", jobs: int = 4):
        target = self.targets.get(target_name)
        if not target:
            raise ValueError(f"Target '{target_name}' not found. Registered targets: {list(self.targets.keys())}")

        print(f"\n[UVBT] ================================================================")
        print(f"[UVBT] Building Target: {target.name} | {platform} | {configuration}")
        print(f"[UVBT] Root Modules: {', '.join(target.extra_modules)}")
        print(f"[UVBT] ================================================================\n")

        ordered_modules = self.resolve_dependencies(target.extra_modules)
        print(f"[UVBT] Resolved {len(ordered_modules)} modules in dependency order:")
        for m in ordered_modules:
            print(f"  -> {m.name} ({len(m.source_files)} source files)")

        # Prepare output directories
        uvbt_root = self.repo_root / ".uvbt"
        obj_dir = uvbt_root / "obj" / platform / configuration
        lib_dir = uvbt_root / "lib" / platform / configuration
        bin_dir = uvbt_root / "bin" / platform / configuration
        cache_file = uvbt_root / "cache.json"

        for d in (obj_dir, lib_dir, bin_dir):
            d.mkdir(parents=True, exist_ok=True)

        generated_includes = self.generate_prebuild_artifacts(uvbt_root)
        cache = BuildCache(cache_file)

        # Collect global include paths
        global_includes = [
            self.repo_root / "Engine" / "Runtime",
            self.repo_root / "Engine" / "Editor",
            self.repo_root / "Engine" / "Source",
        ] + generated_includes
        
        # Third-party / fetched deps if available
        third_party_dirs = [
            self.repo_root / "build" / "_deps" / "googletest-src" / "googletest" / "include",
            self.repo_root / "build" / "_deps" / "googletest-src" / "googlemock" / "include",
            self.repo_root / "build" / "_deps" / "nlohmann_json-src" / "include",
            self.repo_root / "build" / "_deps" / "uve_fmt-src" / "include",
            self.repo_root / "build" / "_deps" / "spirvreflect-src",
            self.repo_root / "build" / "_deps" / "vulkanheaders-src" / "include",
            self.repo_root / "build" / "_deps" / "uve_ufbx-src",
            self.repo_root / "build" / "_deps" / "uve_miniaudio-src",
            self.repo_root / "build" / "_deps" / "uve_basis_universal-src" / "transcoder",
            self.repo_root / "build" / "_deps" / "uve_basis_universal-src" / "encoder",
            self.repo_root / "build" / "_deps" / "uve_basis_universal-src",
        ]
        for tp in third_party_dirs:
            if tp.is_dir():
                global_includes.append(tp)

        # Base compiler flags
        cxx = os.environ.get("CXX", "g++")
        ar = os.environ.get("AR", "ar")
        
        base_cflags = [
            "-std=c++23",
            "-Wall", "-Wextra", "-Wpedantic",
            "-fPIC",
            "-pthread"
        ]

        # Engine build profile definitions
        if configuration == "Release":
            base_defs = [
                "UVE_BUILD_CONFIGURATION_RELEASE=1",
                "UVE_SHIPPING=0",
                "UVE_PROFILE_ASSERTIONS_ENABLED=0",
                "UVE_PROFILE_DEFAULT_LOG_LEVEL=2",
                "UVE_COMPILED_LOG_LEVEL=0",
            ]
            base_cflags += ["-O3", "-DNDEBUG"]
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
            "UVE_HAS_STD_FORMAT=0",
            "FMT_HEADER_ONLY=1",
            f'UVE_SOURCE_DIR="{self.repo_root}"'
        ]

        if configuration == "Sanitizers":
            base_cflags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]

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

                cmd = [cxx] + base_cflags + inc_flags + def_flags + [
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

        # Link executable if TargetType is Test or Program
        target_exe = bin_dir / target.output_name
        print(f"\n[UVBT] Linking executable: {target_exe}")

        all_objs = []
        all_libs = []
        all_sys_libs = ["pthread", "dl", "m"]

        # If testing, add gtest libraries
        if target.target_type == "Test":
            gtest_libs = [
                self.repo_root / "build" / "lib" / "libgtest.a",
                self.repo_root / "build" / "lib" / "libgtest_main.a",
            ]
            for glib in gtest_libs:
                if glib.exists():
                    all_libs.append(str(glib))

        # Third-party libraries
        third_party_libs = [
            self.repo_root / "build" / "Engine" / "Runtime" / "Asset" / "libuve_ufbx.a",
            self.repo_root / "build" / "Engine" / "Runtime" / "Asset" / "libuve_basisu_encoder.a",
            self.repo_root / "build" / "Engine" / "Runtime" / "Asset" / "libuve_basisu_transcoder.a",
            self.repo_root / "build" / "_deps" / "spirvreflect-build" / "libspirv-reflect-static.a",
        ]
        for tlib in third_party_libs:
            if tlib.exists():
                all_libs.append(str(tlib))

        for mod in ordered_modules:
            all_sys_libs.extend(mod.public_sys_libs + mod.private_sys_libs)
            lib_p = module_libs.get(mod.name)
            if lib_p and lib_p.exists():
                all_libs.append(str(lib_p))

        # Deduplicate sys libs
        unique_sys = []
        for s in all_sys_libs:
            if s not in unique_sys:
                unique_sys.append(s)

        link_cmd = [cxx] + base_cflags + ["-o", str(target_exe)]
        if target.target_type == "Test":
            root_test_libs = [str(module_libs[m]) for m in target.extra_modules if m in module_libs]
            other_libs = [l for l in all_libs if l not in root_test_libs]
            link_cmd += ["-Wl,--whole-archive"] + root_test_libs + ["-Wl,--no-whole-archive"]
            link_cmd += ["-Wl,--start-group"] + other_libs + ["-Wl,--end-group"]
        else:
            link_cmd += ["-Wl,--start-group"] + all_libs + ["-Wl,--end-group"]
        link_cmd += [f"-l{s}" for s in unique_sys]

        link_res = subprocess.run(link_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if link_res.returncode != 0:
            print(f"\033[91m[UVBT] Link failed:\033[0m")
            print(link_res.stdout)
            print(link_res.stderr)
            raise RuntimeError("[UVBT] Link failed.")

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
    repo_root = Path(__file__).resolve().parents[3]
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
