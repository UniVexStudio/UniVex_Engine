// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class RHIShaderModule : ModuleRules
{
    public RHIShaderModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "RHI",
            "Logging",
            "Math",
            "Asset",
            "Threading",
            "Events",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
