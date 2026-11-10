// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class RHINullModule : ModuleRules
{
    public RHINullModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "RHI",
            "Logging",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
