// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UniVexRuntimeTarget : TargetRules
{
    public UniVexRuntimeTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        OutputName = "uve_runtime";

        ExtraModuleNames.AddRange(new string[] {
            "EngineApp"
        });
    }
}
