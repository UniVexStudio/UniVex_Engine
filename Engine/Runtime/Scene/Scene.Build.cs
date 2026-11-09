// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class SceneModule : ModuleRules
{
    public SceneModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Memory",
            "Events",
            "Math",
            "Asset",
            "Component",
            "Entity",
            "ObjectsCore",
            "Threading",
            "Animation",
            "Objects3D",
            "ObjectsCanvas",
            "Objects2D",
            "Strings",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Gameplay",
            "AI",
        });
    }
}
