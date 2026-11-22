// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

/// libuve_editor.so: the UniVex editor for Android. The Android engine package ships it as
/// uve_editor (Engine/Tools/package_engine.py), which the Hub loads to open a project.
public class UniVexEditorAndroidTarget : TargetRules
{
    public UniVexEditorAndroidTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Program;
        OutputName = "uve_editor";
        ExtraModuleNames.AddRange(new string[] {
            "EditorAndroid"
        });
    }
}
