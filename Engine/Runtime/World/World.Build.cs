// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class WorldModule : ModuleRules
{
    public WorldModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Scene",
            "Entity",
            "Memory",
            "Events",
            "Logging",
            "ObjectsCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Scheduling",
        });
    }
}
