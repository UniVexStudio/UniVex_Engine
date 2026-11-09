// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class MemoryModule : ModuleRules
{
    public MemoryModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
