// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class PackModule : ModuleRules
{
    public PackModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Engine",
            "Platform",
            "Asset",
            "Scene"
        });

        ExcludedFiles.AddRange(new string[] {
            "app/"
        });
    }
}
