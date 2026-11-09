// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class ComponentModule : ModuleRules
{
    public ComponentModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
            "Asset",
            "ObjectsCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
