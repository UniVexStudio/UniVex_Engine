// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class ObjectsCanvasModule : ModuleRules
{
    public ObjectsCanvasModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
            "Component",
            "Entity",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {

        });
    }
}
