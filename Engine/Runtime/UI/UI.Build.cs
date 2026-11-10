// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UIModule : ModuleRules
{
    public UIModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
            "Asset",
            "Component",
            "Entity",
            "ObjectsCanvas",
            "Input",
            "Localization"
        });

        PrivateIncludePaths.AddRange(new string[] {
            "thirdparty"
        });
    }
}
