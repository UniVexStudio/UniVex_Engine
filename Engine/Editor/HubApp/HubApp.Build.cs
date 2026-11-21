// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class HubAppModule : ModuleRules
{
    public HubAppModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        // The Hub hosts PRISM through the editor's GLFW host and launches the editor it ships with.
        PublicDependencyModuleNames.AddRange(new string[] {
            "EditorCore",
            "LauncherUI",
            "Engine",
            "Pack",
            "Online"
        });

        // The Hub's entry point lives at Engine/App/Internal/hub/
        ExtraSourceDirectories.AddRange(new string[] {
            "../../App/Internal/hub"
        });

        PublicIncludePaths.AddRange(new string[] {
            "../../App/Internal/hub"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "GL",
            "GLX",
            "X11"
        });
    }
}
