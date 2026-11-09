// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class EntityModule : ModuleRules
{
    public EntityModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Memory",
            "Events",
            "Component",
            "Utilities",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
