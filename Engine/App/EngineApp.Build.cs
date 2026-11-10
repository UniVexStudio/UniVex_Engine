// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class EngineAppModule : ModuleRules
{
    public EngineAppModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Engine",
            "Pack"
        });

        ExcludedFiles.AddRange(new string[] {
            "editor/",
            "Android/"
        });
    }
}
