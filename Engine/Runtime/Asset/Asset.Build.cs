// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class AssetModule : ModuleRules
{
    public AssetModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Memory",
            "Math",
            "Threading",
            "Events",
            "Utilities"
        });

        ExcludedFiles.AddRange(new string[] {
            "texture_compression_encode.cpp"
        });

        PublicDefinitions.AddRange(new string[] {
            "UVE_HAS_BASIS_ENCODER=0"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "z",
            "jpeg"
        });
    }
}
