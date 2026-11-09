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
            "UVScript",
            "Network"
        });

        PublicDefinitions.AddRange(new string[] {
            "UVE_HAS_DESKTOP_GL=0"
        });
    }
}
