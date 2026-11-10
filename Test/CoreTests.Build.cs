// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class CoreTestsModule : ModuleRules
{
    public CoreTestsModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Platform",
            "Logging",
            "Memory",
            "Strings",
            "Threading",
            "Scheduling",
            "Diagnostics",
            "Math",
            "Utilities",
            "Containers",
            "Events",
            "Input",
            "Window",
            "Asset",
            "Entity",
            "Component",
            "Scene",
            "Gameplay",
            "AI",
            "Physics",
            "Animation",
            "Retarget",
            "ObjectsCore",
            "Objects2D",
            "Objects3D",
            "ObjectsCanvas",
            "RHI",
            "RHIShader",
            "RHIOpenGL",
            "RHIVulkan",
            "RHINull",
            "RenderSystems",
            "Commandline",
            "Config",
            "Network",
            "Plugins",
            "ProjectCheck",
            "UVScript",
            "World",
            "Engine",
            "Audio",
            "Save",
            "Navigation",
            "Localization",
            "UI",
            "Pack"
        });

        PrivateIncludePaths.AddRange(new string[] {
            ".",
            "Support",
            "../Engine/Runtime/Asset/Internal",
            "../Engine/Runtime/RHI/Shader/Internal",
            "../Engine/Runtime/Entity/Internal"
        });

        PrivateDefinitions.AddRange(new string[] {
            "UVE_TESTS=1",
            "UVE_BUILD_TESTS=1"
        });

        PrivateSystemLibraries.AddRange(new string[] {
            "pthread",
            "dl",
            "m",
            "GL"
        });

        ExcludedFiles.AddRange(new string[] {
            "Editor/",
            "Samples/",
            "Plugins/Fixtures/"
        });
    }
}
