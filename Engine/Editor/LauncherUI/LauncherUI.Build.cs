// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class LauncherUIModule : ModuleRules
{
    public LauncherUIModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        // Shared by the Hub and its installer: the window bootstrap (engine window, PRISM host,
        // launcher theme, title bar) and the widgets both use.
        PublicDependencyModuleNames.AddRange(new string[] {
            "EditorCore",
            "Engine",
            "Platform"
        });

        ExtraSourceDirectories.AddRange(new string[] {
            "../../App/Internal/launcher_ui"
        });

        PublicIncludePaths.AddRange(new string[] {
            "../../App/Internal/launcher_ui"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "GL",
            "GLX",
            "X11"
        });
    }
}
