// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class RetargetModule : ModuleRules
{
    public RetargetModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
            "Asset",
        });

        ExcludedFiles.AddRange(new string[] {
            "make_humanoid_reference.cpp"
        });
    }
}
