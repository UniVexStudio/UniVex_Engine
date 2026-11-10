// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class AnimationModule : ModuleRules
{
    public AnimationModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Utilities",
        });
    }
}
