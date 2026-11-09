// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class ProjectCheckModule : ModuleRules
{
    public ProjectCheckModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Asset",
        });

        ExcludedFiles.AddRange(new string[] {
            "main.cpp"
        });
    }
}
