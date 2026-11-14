// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class LoggingModule : ModuleRules
{
    public LoggingModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });

        PublicSystemLibraries.AddRange(new string[] {
            "android:log"
        });
    }
}
