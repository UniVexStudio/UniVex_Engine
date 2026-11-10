// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UniVexPackTarget : TargetRules
{
    public UniVexPackTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Program;
        OutputName = "uve_pack";

        ExtraModuleNames.AddRange(new string[] {
            "PackApp"
        });
    }
}
