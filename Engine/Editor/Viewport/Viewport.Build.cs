// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class ViewportModule : ModuleRules
{
    public ViewportModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Engine",
            "World",
            "Scene",
            "Math",
            "Platform",
            "Logging"
        });

        PublicIncludePaths.AddRange(new string[] {
            "app",
            "Expose",
            "Internal"
        });

        PublicDefinitions.AddRange(new string[] {
            "GLEW_STATIC",
            "GLEW_NO_GLU"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "GL",
            "GLX",
            "X11"
        });
    }
}
