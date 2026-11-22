// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

/// The activity side every UniVex app on Android shares: the event loop, touch input, the surface
/// coming and going, the display cutouts and the APK's data. The Hub and the editor each link their
/// own copy and take over the activity's callbacks while they run.
public class AndroidHostModule : ModuleRules
{
    public AndroidHostModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Window",
            "Online"
        });

        PublicIncludePaths.AddRange(new string[] {
            "Expose"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "android:android",
            "android:log",
            "android:EGL",
            "android:GLESv3"
        });
    }
}
