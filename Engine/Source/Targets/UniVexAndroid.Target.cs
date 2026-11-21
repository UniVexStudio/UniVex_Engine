// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

/// libunivex_android.so: the Android runtime APK's native code (see Engine/Tools/android/build_apk.py).
public class UniVexAndroidTarget : TargetRules
{
    public UniVexAndroidTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Program;
        OutputName = "univex_android";
        ExtraModuleNames.AddRange(new string[] {
            "AndroidRuntime"
        });
    }
}
