// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class GameplayModule : ModuleRules
{
    public GameplayModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Input",
            "Math",
            "Component",
            "Physics",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
