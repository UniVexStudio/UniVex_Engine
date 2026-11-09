// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class EditorCoreModule : ModuleRules
{
    public EditorCoreModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Engine",
            "World",
            "Scene",
            "Entity",
            "Component",
            "Platform",
            "Diagnostics",
            "Network",
            "Plugins",
            "Retarget",
            "ImGui",
            "Math",
            "Strings",
            "Logging"
        });

        PublicDefinitions.AddRange(new string[] {
            "GLEW_STATIC",
            "GLEW_NO_GLU"
        });
    }
}
