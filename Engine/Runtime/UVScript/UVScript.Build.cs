// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UVScriptModule : ModuleRules
{
    public UVScriptModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {

        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });

        ExcludedFiles.AddRange(new string[] {
            "uvsc_main.cpp"
        });
    }
}
