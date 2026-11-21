// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

/// libunivex_hub.so: UniVex Hub's native code for the Android APK (see Engine/Tools/android/build_apk.py).
public class UniVexHubAndroidTarget : TargetRules
{
    public UniVexHubAndroidTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Program;
        OutputName = "univex_hub";
        ExtraModuleNames.AddRange(new string[] {
            "HubAndroid"
        });
    }
}
