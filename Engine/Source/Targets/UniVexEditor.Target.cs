// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UniVexEditorTarget : TargetRules
{
    public UniVexEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        OutputName = "uve_editor";

        ExtraModuleNames.AddRange(new string[] {
            "EditorApp"
        });
    }
}
