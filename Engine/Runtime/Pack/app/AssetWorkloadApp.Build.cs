// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class AssetWorkloadAppModule : ModuleRules
{
    public AssetWorkloadAppModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Pack",
            "Engine"
        });

        ExcludedFiles.AddRange(new string[] {
            "main.cpp"
        });
    }
}
