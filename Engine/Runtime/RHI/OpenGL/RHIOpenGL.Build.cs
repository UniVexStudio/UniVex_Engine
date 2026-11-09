// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class RHIOpenGLModule : ModuleRules
{
    public RHIOpenGLModule(ReadOnlyTargetRules Target) : base(Target)
    {
        // When desktop GL is disabled (headless Linux build), RHIOpenGL is header-only
        Type = ModuleType.HeaderOnly;

        PublicDependencyModuleNames.AddRange(new string[] {
            "RHI",
            "Window",
            "Platform",
            "Logging",
        });
    }
}
