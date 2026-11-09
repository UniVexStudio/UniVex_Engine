// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class SchedulingModule : ModuleRules
{
    public SchedulingModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Containers",
            "Threading",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Utilities",
        });
    }
}
