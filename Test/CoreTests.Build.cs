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
            "UVE_BUILD_TESTS=1",
            "UVE_TEST_PLUGIN_VALID_PATH=\"/home/antigravity/UniVexStudio/UniVex_Engine/build/Test/libuve_test_plugin_valid.so\"",
            "UVE_TEST_PLUGIN_BAD_PROTOCOL_PATH=\"/home/antigravity/UniVexStudio/UniVex_Engine/build/Test/libuve_test_plugin_bad_protocol.so\"",
            "UVE_TEST_PLUGIN_NO_ENTRY_PATH=\"/home/antigravity/UniVexStudio/UniVex_Engine/build/Test/libuve_test_plugin_no_entry.so\"",
            "UVE_UVSCRIPT_NATIVE_DIR=\"/home/antigravity/UniVexStudio/UniVex_Engine/Test/UVScript/native\""
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
            "Plugins/Fixtures/",
            "window_manager_tests.cpp",
            "gl_render_device_tests.cpp",
            "vulkan_render_device_tests.cpp",
            "compute_system_tests.cpp",
            "particle_compute_simulation_tests.cpp",
            "frustum_cull_compute_tests.cpp",
            "frustum_cull_indirect_tests.cpp",
            "mesh_skin_compute_tests.cpp"
        });
    }
}
