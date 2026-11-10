// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class ObjectsCoreModule : ModuleRules
{
    public ObjectsCoreModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
            "Strings",
            "Utilities",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Containers",
        });
    }
}
