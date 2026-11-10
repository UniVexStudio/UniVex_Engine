// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class UniVexAssetWorkloadTarget : TargetRules
{
    public UniVexAssetWorkloadTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Program;
        OutputName = "uve_asset_workload";

        ExtraModuleNames.AddRange(new string[] {
            "AssetWorkloadApp"
        });
    }
}
