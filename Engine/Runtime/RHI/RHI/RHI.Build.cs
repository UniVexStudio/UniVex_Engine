// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class RHIModule : ModuleRules
{
    public RHIModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
            "Logging",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
