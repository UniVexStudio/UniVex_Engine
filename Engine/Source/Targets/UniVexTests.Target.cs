// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UniVexTestsTarget : TargetRules
{
    public UniVexTestsTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Test;
        OutputName = "UniVexTests";

        ExtraModuleNames.AddRange(new string[] {
            "CoreTests"
        });
    }
}
