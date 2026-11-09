// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class AudioModule : ModuleRules
{
    public AudioModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Math",
            "Scene",
            "Asset"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Containers",
            "Utilities"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "dl",
            "m",
            "pthread"
        });
    }
}
