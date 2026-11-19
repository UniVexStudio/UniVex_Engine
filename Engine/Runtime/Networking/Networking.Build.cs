// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class NetworkingModule : ModuleRules
{
    public NetworkingModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
            "Blueprint",
            "Diagnostics",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Utilities",
        });
    }
}
