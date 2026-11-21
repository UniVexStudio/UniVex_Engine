// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class OnlineModule : ModuleRules
{
    public OnlineModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        // Engine releases: the feed, HTTPS transfers, package verification and installation.
        PublicDependencyModuleNames.AddRange(new string[] {
            "Engine",
            "Platform",
            "Pack"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "linux:curl",
            "win:winhttp",
            "z"
        });
    }
}
