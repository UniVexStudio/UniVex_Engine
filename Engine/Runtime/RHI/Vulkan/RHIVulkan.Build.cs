// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class RHIVulkanModule : ModuleRules
{
    public RHIVulkanModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "RHI",
            "Window",
            "Platform",
            "Logging",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
