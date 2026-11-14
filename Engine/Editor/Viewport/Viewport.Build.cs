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

        ExcludedFiles.AddRange(new string[] {
            "android:app/main.cpp",
            "android:tools/",
            "android:tests/"
        });

        PublicDefinitions.AddRange(new string[] {
            "desktop:GLEW_STATIC",
            "desktop:GLEW_NO_GLU"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "desktop:GL",
            "desktop:GLX",
            "desktop:X11",
            "android:EGL",
            "android:GLESv3",
            "android:android",
            "android:log"
        });
    }
}
