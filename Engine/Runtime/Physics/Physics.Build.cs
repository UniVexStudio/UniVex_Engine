// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class PhysicsModule : ModuleRules
{
    public PhysicsModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Math",
            "Scene",
            "Threading",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
