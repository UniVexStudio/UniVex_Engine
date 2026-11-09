// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class PackAppModule : ModuleRules
{
    public PackAppModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Pack"
        });

        ExcludedFiles.AddRange(new string[] {
            "asset_workload.cpp"
        });
    }
}
