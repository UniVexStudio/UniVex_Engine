// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class WindowModule : ModuleRules
{
    public WindowModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Events",
            "Input",
        });

        // The desktop windows through GLFW; Android draws into the activity's surface through EGL.
        ExcludedFiles.AddRange(new string[] {
            "desktop:android_window_manager.cpp",
            "desktop:android_glfw.cpp",
            "android:Internal/window_manager.cpp"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "linux:X11",
            "linux:GL",
            "linux:GLX"
        });
    }
}
