// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class RenderSystemsModule : ModuleRules
{
    public RenderSystemsModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Containers",
            "Math",
            "Scene",
            "Objects2D",
            "Asset",
            "RHI",
            "RHIShader",
            "UI",
            "ObjectsCore"
        });
    }
}
