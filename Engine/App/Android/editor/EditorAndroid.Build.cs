// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

/// The UniVex editor on Android: a shared library the Hub loads and runs on its activity (see
/// engine_install.h, kEditorAndroidEntryPointUVE). Built only for Android.
public class EditorAndroidModule : ModuleRules
{
    public EditorAndroidModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "EditorApp",
            "AndroidHost",
            "LauncherUI"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "android:android",
            "android:log",
            "android:EGL",
            "android:GLESv3"
        });
    }
}
