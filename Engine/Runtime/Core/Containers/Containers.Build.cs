// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class ContainersModule : ModuleRules
{
    public ContainersModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Logging",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
