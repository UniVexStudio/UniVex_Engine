// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class AIModule : ModuleRules
{
    public AIModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Component",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
