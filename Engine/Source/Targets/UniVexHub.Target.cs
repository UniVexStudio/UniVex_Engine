// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UniVexHubTarget : TargetRules
{
    public UniVexHubTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        OutputName = "uve_hub";

        ExtraModuleNames.AddRange(new string[] {
            "HubApp"
        });
    }
}
