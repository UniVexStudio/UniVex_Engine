// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

/// UniVex Hub on Android: the same Hub window as the desktop, in a NativeActivity. Built only for
/// Android, as the shared library the activity loads.
public class HubAndroidModule : ModuleRules
{
    public HubAndroidModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "HubApp"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "android:android",
            "android:log",
            "android:EGL",
            "android:GLESv3"
        });
    }
}
