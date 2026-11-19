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
            "Networking",
            "Plugins",
            "ProjectCheck",
            "Blueprint",
            "World",
            "Engine",
            "Audio",
            "Save",
            "Navigation",
            "Localization",
            "UI",
            "Pack",
            "VFX",
            "Prism",
            "EditorCore"
        });

        PrivateIncludePaths.AddRange(new string[] {
            ".",
            "Support",
            "../Runtime/Asset/Internal",
            "../Runtime/RHI/Shader/Internal",
            "../Runtime/Entity/Internal",
            "../Runtime/Retarget/Tests",
            "../Editor/EditorCore/Internal/shell",
            "../Editor/EditorCore/Internal/shell/vfx"
        });

        ExtraSourceDirectories.AddRange(new string[] {
            "../Runtime/AI/Tests",
            "../Runtime/Animation/Tests",
            "../Runtime/Asset/Tests",
            "../Runtime/Audio/Tests",
            "../Runtime/Commandline/Tests",
            "../Runtime/Config/Tests",
            "../Runtime/Core/Containers/Tests",
            "../Runtime/Core/Diagnostics/Tests",
            "../Runtime/Core/Logging/Tests",
            "../Runtime/Core/Math/Tests",
            "../Runtime/Core/Memory/Tests",
            "../Runtime/Core/Scheduling/Tests",
            "../Runtime/Core/Strings/Tests",
            "../Runtime/Core/Threading/Tests",
            "../Runtime/Core/Utilities/Tests",
            "../Runtime/Engine/Tests",
            "../Runtime/Entity/Tests",
            "../Runtime/Events/Tests",
            "../Runtime/Gameplay/Tests",
            "../Runtime/Input/Tests",
            "../Runtime/Localization/Tests",
            "../Runtime/Navigation/Tests",
            "../Runtime/Networking/Tests",
            "../Runtime/Objects/Core/Tests",
            "../Runtime/Objects/2D/Tests",
            "../Runtime/Objects/3D/Tests",
            "../Runtime/Objects/UI/Tests",
            "../Runtime/Pack/Tests",
            "../Runtime/Physics/Tests",
            "../Runtime/Platform/Tests",
            "../Runtime/Plugins/Tests",
            "../Runtime/ProjectCheck/Tests",
            "../Runtime/RHI/Tests",
            "../Runtime/RHI/OpenGL/Tests",
            "../Runtime/RHI/RenderSystems/Tests",
            "../Runtime/RHI/Shader/Tests",
            "../Runtime/RHI/Vulkan/Tests",
            "../Runtime/Retarget/Tests",
            "../Runtime/Save/Tests",
            "../Runtime/Scene/Tests",
            "../Runtime/UI/Tests",
            "../Runtime/VFX/Tests",
            "../Runtime/Window/Tests",
            "../Runtime/World/Tests",
            "../Editor/EditorCore/Blueprint/Testing"
            // Note: ../Editor/EditorCore/Tests is intentionally excluded here, matching
            // the pre-relocation ExcludedFiles entry for Test/Editor/.
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
            "Samples/",
            "Fixtures/"
        });
    }
}
