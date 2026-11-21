// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class HubSetupAppModule : ModuleRules
{
    public HubSetupAppModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        // The Hub's installer: a PRISM window over the Online module's install steps.
        PublicDependencyModuleNames.AddRange(new string[] {
            "LauncherUI",
            "Engine",
            "Online"
        });

        // The installer's entry point lives at Engine/App/Internal/setup/
        ExtraSourceDirectories.AddRange(new string[] {
            "../../App/Internal/setup"
        });

        PublicIncludePaths.AddRange(new string[] {
            "../../App/Internal/setup"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "GL",
            "GLX",
            "X11"
        });
    }
}
