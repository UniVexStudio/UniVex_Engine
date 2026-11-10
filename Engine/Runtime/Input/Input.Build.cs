// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class InputModule : ModuleRules
{
    public InputModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
            "Events",
        });

        ExcludedFiles.AddRange(new string[] {
            "android_motion_event.cpp"
        });
    }
}
