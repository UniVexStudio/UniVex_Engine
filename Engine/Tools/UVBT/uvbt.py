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

    def _ensure_imgui_module_rules(self):
        imgui_dir = self.repo_root / "ThirdParty" / "imgui"
        imgui_bcs = imgui_dir / "ImGui.Build.cs"
        if not imgui_bcs.exists():
            imgui_dir.mkdir(parents=True, exist_ok=True)
            content = """// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class ImGuiModule : ModuleRules
{
    public ImGuiModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicIncludePaths.AddRange(new string[] {
            ".",
            "backends"
        });

        ExtraSourceDirectories.AddRange(new string[] {
            "backends"
        });

        ExcludedFiles.AddRange(new string[] {
            "examples/",
            "docs/",
            "misc/",
            "imgui_demo.cpp",
            "imgui_impl_android.cpp",
            "imgui_impl_dx9.cpp",
            "imgui_impl_dx10.cpp",
            "imgui_impl_dx11.cpp",
            "imgui_impl_dx12.cpp",
            "imgui_impl_metal.mm",
            "imgui_impl_metal.cpp",
            "imgui_impl_vulkan.cpp",
            "imgui_impl_wgpu.cpp",
            "imgui_impl_win32.cpp",
            "imgui_impl_sdl2.cpp",
            "imgui_impl_sdl3.cpp",
            "imgui_impl_sdlrenderer2.cpp",
            "imgui_impl_sdlrenderer3.cpp",
            "imgui_impl_osx.mm",
            "imgui_impl_allegro5.cpp",
            "imgui_impl_glut.cpp",
            "imgui_impl_null.cpp",
            "imgui_impl_opengl2.cpp",
            "imgui_impl_qnx.cpp",
            "imgui_impl_sdlgpu3.cpp"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "GL",
            "GLX",
            "X11"
        });
    }
}
"""
            imgui_bcs.write_text(content, encoding="utf-8")

    def discover(self):
        self._ensure_imgui_module_rules()
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

    def generate_prebuild_artifacts(self, uvbt_root: Path, bin_dir: Path = None):
        gen_dir = uvbt_root / "generated"
        gen_dir.mkdir(parents=True, exist_ok=True)
        embed_script = self.repo_root / "Engine" / "Tools" / "embed_file.py"
        cxx = os.environ.get("CXX", "g++")

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

        # 4. Editor icons
        editor_icons_inc = editor_gen / "uve_editor_icon_set.inc"
        embed_icons_script = self.repo_root / "Engine" / "Tools" / "editor_icons" / "embed_editor_icons.py"
        icons_dir = self.repo_root / "Engine" / "Editor" / "EditorCore" / "assets" / "icons"
        if embed_icons_script.exists() and icons_dir.is_dir() and not editor_icons_inc.exists():
            subprocess.run([sys.executable, str(embed_icons_script), str(editor_icons_inc), str(icons_dir)], check=True)

        # 5. Viewport Shaders
        vp_render_gen = gen_dir / "univex" / "render"
        vp_render_gen.mkdir(parents=True, exist_ok=True)
        vp_shaders = self.repo_root / "Engine" / "Editor" / "Viewport" / "shaders"

        grid_in = vp_shaders / "grid_shader_sources.h.in"
        grid_out = vp_render_gen / "grid_shader_sources.h"
        if grid_in.exists() and vp_shaders.is_dir() and not grid_out.exists():
            text = grid_in.read_text(encoding="utf-8")
            text = text.replace("@UNIVEX_GRID_VERT_SOURCE@", (vp_shaders / "infinite_grid.vert").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_GRID_FRAG_SOURCE@", (vp_shaders / "infinite_grid.frag").read_text(encoding="utf-8"))
            grid_out.write_text(text, encoding="utf-8")

        gizmo_in = vp_shaders / "gizmo_shader_sources.h.in"
        gizmo_out = vp_render_gen / "gizmo_shader_sources.h"
        if gizmo_in.exists() and vp_shaders.is_dir() and not gizmo_out.exists():
            text = gizmo_in.read_text(encoding="utf-8")
            text = text.replace("@UNIVEX_GIZMO_LINE_VERT@", (vp_shaders / "gizmo_line.vert").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_GIZMO_LINE_FRAG@", (vp_shaders / "gizmo_line.frag").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_GIZMO_SOLID_VERT@", (vp_shaders / "gizmo_solid.vert").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_GIZMO_SOLID_FRAG@", (vp_shaders / "gizmo_solid.frag").read_text(encoding="utf-8"))
            gizmo_out.write_text(text, encoding="utf-8")

        sel_in = vp_shaders / "selection_outline_shader_sources.h.in"
        sel_out = vp_render_gen / "selection_outline_shader_sources.h"
        if sel_in.exists() and vp_shaders.is_dir() and not sel_out.exists():
            text = sel_in.read_text(encoding="utf-8")
            text = text.replace("@UNIVEX_SELECTION_MASK_VERT@", (vp_shaders / "selection_mask.vert").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_SELECTION_MASK_FRAG@", (vp_shaders / "selection_mask.frag").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_SELECTION_OUTLINE_VERT@", (vp_shaders / "selection_outline.vert").read_text(encoding="utf-8"))
            text = text.replace("@UNIVEX_SELECTION_OUTLINE_FRAG@", (vp_shaders / "selection_outline.frag").read_text(encoding="utf-8"))
            sel_out.write_text(text, encoding="utf-8")

        # 6. Test artifacts if Test/ exists
        test_dir = self.repo_root / "Test"
        if test_dir.is_dir() and bin_dir is not None:
            # Build test plugins
            plugin_fixtures = [
                (test_dir / "Plugins" / "Fixtures" / "uve_test_plugin_valid.cpp", bin_dir / "libuve_test_plugin_valid.so"),
                (test_dir / "Plugins" / "Fixtures" / "uve_test_plugin_bad_protocol.cpp", bin_dir / "libuve_test_plugin_bad_protocol.so"),
                (test_dir / "Plugins" / "Fixtures" / "uve_test_plugin_no_entry.cpp", bin_dir / "libuve_test_plugin_no_entry.so"),
            ]
            plugin_inc = self.repo_root / "Engine" / "Runtime" / "Plugins" / "Expose"
            for src, dst in plugin_fixtures:
                if src.exists() and (not dst.exists() or src.stat().st_mtime > dst.stat().st_mtime):
                    cmd = [cxx, "-std=c++23", "-fPIC", "-shared", f"-I{plugin_inc}", str(src), "-o", str(dst)]
                    subprocess.run(cmd, check=True)

            # Build uvsc compiler tool
            uvsc_exe = bin_dir / "uvsc"
            uvsc_main = self.repo_root / "Engine" / "Runtime" / "UVScript" / "Tool" / "uvsc_main.cpp"
            uvscript_dir = self.repo_root / "Engine" / "Runtime" / "UVScript"
            uvscript_srcs = list((uvscript_dir / "Internal").glob("*.cpp"))

            rebuild_uvsc = not uvsc_exe.exists()
            if not rebuild_uvsc:
                uvsc_mtime = uvsc_exe.stat().st_mtime
                if uvsc_main.stat().st_mtime > uvsc_mtime or any(s.stat().st_mtime > uvsc_mtime for s in uvscript_srcs):
                    rebuild_uvsc = True

            if rebuild_uvsc and uvsc_main.exists():
                cmd = [cxx, "-std=c++23", "-O2", f"-I{gen_dir}", f"-I{uvscript_dir / 'Expose'}"] + [str(s) for s in uvscript_srcs] + [str(uvsc_main), "-o", str(uvsc_exe)]
                subprocess.run(cmd, check=True)

            # Generate native script sources
            host_file = test_dir / "UVScript" / "native" / "fake_object.uvhost"
            script_names = ["player.uvs", "math.uvs", "waits.uvs", "errors.uvs", "methods.uvs", "collections.uvs"]
            gen_script_dir = test_dir / "UVScript" / "native" / "generated"
            gen_script_dir.mkdir(parents=True, exist_ok=True)
            for sname in script_names:
                ssrc = test_dir / "UVScript" / "native" / sname
                sdst = gen_script_dir / f"{sname}.cpp"
                if uvsc_exe.exists() and host_file.exists() and ssrc.exists() and (not sdst.exists() or ssrc.stat().st_mtime > sdst.stat().st_mtime or host_file.stat().st_mtime > sdst.stat().st_mtime):
                    subprocess.run([str(uvsc_exe), "--host", str(host_file), "--out", str(sdst), str(ssrc)], check=True)

        return [gen_dir, retarget_gen, ui_gen, editor_gen, vp_render_gen.parent, vp_render_gen]

    def ensure_third_party(self, platform: str):
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
            ("imgui", "https://github.com/ocornut/imgui.git", "docking", "imgui.h"),
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
                    subprocess.run(["git", "clone", "--depth", "1", "-b", tag, url, str(dest)], check=True)
                for bcs_name, content in saved_bcs.items():
                    (dest / bcs_name).write_text(content, encoding="utf-8")

        self._ensure_imgui_module_rules()

        tp_lib_dir = self.repo_root / ".uvbt" / "tp_lib" / platform
        tp_lib_dir.mkdir(parents=True, exist_ok=True)

        cxx = os.environ.get("CXX", "g++")
        cc = os.environ.get("CC", "gcc")
        ar = os.environ.get("AR", "ar")

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
                subprocess.run([cxx, "-std=c++17", "-O2", f"-I{gt_src / 'include'}", f"-I{gt_src}", "-c", str(gt_src / "src" / "gtest-all.cc"), "-o", str(obj)], check=True)
                subprocess.run([ar, "rcs", str(gt_lib), str(obj)], check=True)

        if not gt_main_lib.exists():
            fallback = tp_dir / "lib" / "libgtest_main.a"
            if fallback.exists() and platform == "Linux":
                shutil.copy2(fallback, gt_main_lib)
            elif (gt_src / "src" / "gtest_main.cc").exists():
                obj = tp_lib_dir / "gtest_main.o"
                subprocess.run([cxx, "-std=c++17", "-O2", f"-I{gt_src / 'include'}", "-c", str(gt_src / "src" / "gtest_main.cc"), "-o", str(obj)], check=True)
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
                subprocess.run([cc, "-O2", "-fPIC", "-c", str(spv_src), "-o", str(obj)], check=True)
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
                subprocess.run([cc, "-O2", "-fPIC", f"-I{tp_dir / 'ufbx'}", "-c", str(ufbx_src), "-o", str(obj)], check=True)
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
            subprocess.run([cxx, "-std=c++17", "-O2", "-fPIC", "-fno-strict-aliasing"] + basis_defs + [f"-I{tp_dir / 'basis_universal' / 'transcoder'}", f"-I{tp_dir / 'basis_universal' / 'encoder'}", "-c", str(bu_tc_src), "-o", str(obj)], check=True)
            subprocess.run([ar, "rcs", str(bu_tc_lib), str(obj)], check=True)

        # 5. libuve_basisu_encoder.a
        bu_enc_lib = tp_lib_dir / "libuve_basisu_encoder.a"
        bu_enc_dir = tp_dir / "basis_universal" / "encoder"
        if bu_enc_dir.is_dir() and not bu_enc_lib.exists():
            enc_cpps = list(bu_enc_dir.glob("*.cpp"))
            enc_objs = []
            for ec in enc_cpps:
                obj = tp_lib_dir / f"{ec.stem}.o"
                subprocess.run([cxx, "-std=c++17", "-O2", "-fPIC", "-fno-strict-aliasing"] + basis_defs + [f"-I{tp_dir / 'basis_universal' / 'transcoder'}", f"-I{bu_enc_dir}", "-c", str(ec), "-o", str(obj)], check=True)
                enc_objs.append(str(obj))
            subprocess.run([ar, "rcs", str(bu_enc_lib)] + enc_objs, check=True)

    def build_target(self, target_name: str, platform: str = "Linux", configuration: str = "Debug", jobs: int = 4):
        target = self.targets.get(target_name)
        if not target:
            raise ValueError(f"Target '{target_name}' not found. Registered targets: {list(self.targets.keys())}")

        # Prepare output directories
        uvbt_root = self.repo_root / ".uvbt"
        obj_dir = uvbt_root / "obj" / platform / configuration
        lib_dir = uvbt_root / "lib" / platform / configuration
        bin_dir = uvbt_root / "bin" / platform / configuration
        cache_file = uvbt_root / "cache.json"

        for d in (obj_dir, lib_dir, bin_dir):
            d.mkdir(parents=True, exist_ok=True)

        self.ensure_third_party(platform)
        generated_includes = self.generate_prebuild_artifacts(uvbt_root, bin_dir)
        cache = BuildCache(cache_file)

        # Refresh discovery in case new sources were generated
        self.discover()
        target = self.targets.get(target_name)

        if "CoreTests" in self.modules:
            self.modules["CoreTests"].private_defs.extend([
                f'UVE_TEST_PLUGIN_VALID_PATH="{bin_dir / "libuve_test_plugin_valid.so"}"',
                f'UVE_TEST_PLUGIN_BAD_PROTOCOL_PATH="{bin_dir / "libuve_test_plugin_bad_protocol.so"}"',
                f'UVE_TEST_PLUGIN_NO_ENTRY_PATH="{bin_dir / "libuve_test_plugin_no_entry.so"}"',
                f'UVE_UVSCRIPT_NATIVE_DIR="{self.repo_root / "Test" / "UVScript" / "native"}"',
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
            self.repo_root / "ThirdParty" / "imgui",
            self.repo_root / "ThirdParty" / "imgui" / "backends",
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
        if platform == "Windows":
            exe_name = target.output_name if target.output_name.endswith(".exe") else f"{target.output_name}.exe"
            all_sys_libs = ["pthread", "opengl32", "gdi32", "glfw3", "glew32", "z", "jpeg"]
        else:
            exe_name = target.output_name
            all_sys_libs = ["pthread", "dl", "m", "GL", "GLX", "X11"]
        target_exe = bin_dir / exe_name
        print(f"\n[UVBT] Linking executable: {target_exe}")

        all_objs = []
        all_libs = []

        tp_lib_dir = uvbt_root / "tp_lib" / platform

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
            all_sys_libs.extend(mod.public_sys_libs + mod.private_sys_libs)
            lib_p = module_libs.get(mod.name)
            if lib_p and lib_p.exists():
                all_libs.append(str(lib_p))

        # Deduplicate sys libs and filter out platform-incompatible libraries
        unique_sys = []
        for s in all_sys_libs:
            if platform == "Windows" and s in ("dl", "X11", "GL", "GLX", "m"):
                continue
            if s not in unique_sys:
                unique_sys.append(s)

        link_cmd = [cxx] + base_cflags + ["-o", str(target_exe)]
        sys_flags = [f"-l{s}" for s in unique_sys]
        if target.target_type == "Test":
            root_test_libs = [str(module_libs[m]) for m in target.extra_modules if m in module_libs]
            other_libs = [l for l in all_libs if l not in root_test_libs]
            link_cmd += ["-Wl,--whole-archive"] + root_test_libs + ["-Wl,--no-whole-archive"]
            link_cmd += ["-Wl,--start-group"] + other_libs + sys_flags + ["-Wl,--end-group"]
        else:
            link_cmd += ["-Wl,--start-group"] + all_libs + sys_flags + ["-Wl,--end-group"]

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
