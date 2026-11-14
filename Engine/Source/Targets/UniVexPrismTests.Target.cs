// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UniVexPrismTestsTarget : TargetRules
{
    public UniVexPrismTestsTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Program;
        OutputName = "uve_prism_tests";

        ExtraModuleNames.AddRange(new string[] {
            "PrismTests"
        });
    }
}
