// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class Objects2DModule : ModuleRules
{
    public Objects2DModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
            "Component",
            "Entity",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
