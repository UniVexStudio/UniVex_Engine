// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class CommandlineModule : ModuleRules
{
    public CommandlineModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
