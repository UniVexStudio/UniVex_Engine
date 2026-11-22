// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class EngineModule : ModuleRules
{
    public EngineModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Utilities",
            "Events",
            "Memory",
            "Threading",
            "Commandline",
            "Config",
            "Asset",
            "Scene",
            "Physics",
            "Navigation",
            "Gameplay",
            "AI",
            "Input",
            "Audio",
            "Save",
            "UI",
            "Window",
            "RHI",
            "RHIShader",
            "RHIOpenGL",
            "RHIVulkan",
            "RHINull",
            "RenderSystems",
            "Blueprint",
            "Networking",
            "VFX"
        });

        PublicDefinitions.AddRange(new string[] {
            // Windowed rendering: GLFW and desktop OpenGL, or the activity surface and OpenGL ES on Android.
            "UVE_HAS_DESKTOP_GL=1"
        });
    }
}
