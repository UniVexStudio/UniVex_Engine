// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UniVexAndroidTarget : TargetRules
{
    public UniVexAndroidTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        OutputName = "univex_android";

        ExtraModuleNames.AddRange(new string[] {
            "EngineApp"
        });
    }
}
