// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class SaveModule : ModuleRules
{
    public SaveModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Scene",
            "Asset"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Utilities"
        });
    }
}
