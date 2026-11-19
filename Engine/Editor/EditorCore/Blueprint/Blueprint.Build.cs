// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class BlueprintModule : ModuleRules
{
    public BlueprintModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicIncludePaths.AddRange(new string[] {
            "Compiler/Frontend",
            "Compiler/Backend",
            "Runtime/VirtualMachine",
            "Runtime/Variables",
            "Runtime/ObjectBinding",
            "Nodes/Networking",
        });

        PrivateIncludePaths.AddRange(new string[] {
            "Compiler/Intermediate",
        });

        ExcludedFiles.AddRange(new string[] {
            "blueprint_compiler_main.cpp",
        });
    }
}
