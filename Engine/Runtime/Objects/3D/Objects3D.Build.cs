// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class Objects3DModule : ModuleRules
{
    public Objects3DModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Math",
            "Component",
            "Entity",
            "Animation",
            "Gameplay",
            "Physics",
            "Network",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Retarget",
        });

        PublicIncludePaths.AddRange(new string[] {
            "Expose",
            "Physics/Expose",
            "../AI/3D/Expose",
            "../Animation/Expose"
        });

        PrivateIncludePaths.AddRange(new string[] {
            "Internal",
            "Physics/Internal",
            "../AI/3D/Internal",
            "../Animation/Internal"
        });

        ExtraSourceDirectories.AddRange(new string[] {
            "../AI/3D/Internal",
            "../Animation/Internal"
        });
    }
}
