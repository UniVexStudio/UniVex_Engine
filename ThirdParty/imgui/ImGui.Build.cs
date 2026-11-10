// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

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
