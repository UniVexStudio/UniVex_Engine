// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class PluginsModule : ModuleRules
{
    public PluginsModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Asset",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
