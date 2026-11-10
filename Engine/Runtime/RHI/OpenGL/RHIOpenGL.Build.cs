// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class RHIOpenGLModule : ModuleRules
{
    public RHIOpenGLModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "RHI",
            "Window",
            "Platform",
            "Logging",
        });
    }
}
