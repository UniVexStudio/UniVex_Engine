// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

/// The Android runtime: a NativeActivity that runs a cooked project (or the built-in sample) full
/// screen. Built only for Android, as the shared library the activity loads.
public class AndroidRuntimeModule : ModuleRules
{
    public AndroidRuntimeModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Engine",
            "Pack"
        });

        // The Hub app beside it is a module of its own.
        ExcludedFiles.AddRange(new string[] {
            "hub/"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "android:android",
            "android:log",
            "android:EGL",
            "android:GLESv3"
        });
    }
}
