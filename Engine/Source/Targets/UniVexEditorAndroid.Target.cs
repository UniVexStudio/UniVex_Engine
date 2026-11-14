// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UniVexEditorAndroidTarget : TargetRules
{
    public UniVexEditorAndroidTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        OutputName = "univex_editor_android";

        ExtraModuleNames.AddRange(new string[] {
            "EditorApp"
        });
    }
}
