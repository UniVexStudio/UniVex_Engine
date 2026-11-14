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

        ExcludedFiles.AddRange(new string[] {
            "!android:android_window_manager.cpp",
            "android:window_manager.cpp"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "linux:X11",
            "linux:GL",
            "linux:GLX",
            "android:android",
            "android:EGL",
            "android:GLESv3"
        });
    }
}
