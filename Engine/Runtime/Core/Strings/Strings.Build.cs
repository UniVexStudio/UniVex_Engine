// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class StringsModule : ModuleRules
{
    public StringsModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {

        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Logging",
        });
    }
}
