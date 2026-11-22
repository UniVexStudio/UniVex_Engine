// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UniVexHubSetupTarget : TargetRules
{
    public UniVexHubSetupTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        OutputName = "uve_hub_setup";
        IsGuiApp = true;

        ExtraModuleNames.AddRange(new string[] {
            "HubSetupApp"
        });
    }
}
