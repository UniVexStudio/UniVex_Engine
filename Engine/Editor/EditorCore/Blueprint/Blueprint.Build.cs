// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class BlueprintModule : ModuleRules
{
    public BlueprintModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Asset",
            "Logging",
        });

        PublicIncludePaths.AddRange(new string[] {
            "Assets",
            "Core",
            "Debug",
            "Nodes",
            "Nodes/Networking",
            "Platform",
            "Plugins",
            "Profiling",
            "Resources",
            "Serialization",
            "Compiler/Frontend",
            "Compiler/Backend",
            "Runtime/VirtualMachine",
            "Runtime/Variables",
            "Runtime/ObjectBinding",
        });

        PrivateIncludePaths.AddRange(new string[] {
            "Compiler/Intermediate",
        });

        ExcludedFiles.AddRange(new string[] {
            "blueprint_compiler_main.cpp",
            "Editor/",
        });
    }
}
