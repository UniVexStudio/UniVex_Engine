// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class EditorCoreModule : ModuleRules
{
    public EditorCoreModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Engine",
            "World",
            "Scene",
            "Entity",
            "Component",
            "Platform",
            "Diagnostics",
            "Networking",
            "Plugins",
            "Retarget",
            "Prism",
            "Math",
            "Strings",
            "Logging",
            "VFX",
            "Blueprint",
            "RenderSystems",
            "RHIOpenGL"
        });

        // The editor's PRISM windows: shared pieces in shell/, each kind of asset editor in a folder
        // of its own (Blueprint Class in Blueprint/Editor, Animation Graph, Widget Blueprint, Montage,
        // Retarget, Animation Library, VFX).
        PrivateIncludePaths.AddRange(new string[] {
            "Internal/shell",
            "Blueprint/Editor",
            "Internal/shell/anim_graph",
            "Internal/shell/widget",
            "Internal/shell/montage",
            "Internal/shell/retarget",
            "Internal/shell/anim_library",
            "Internal/shell/vfx"
        });

        PublicDefinitions.AddRange(new string[] {
            "GLEW_STATIC",
            "GLEW_NO_GLU"
        });

        // Exclude BlueprintModule's non-Editor directories (compiled by Blueprint/Blueprint.Build.cs).
        // Blueprint/Editor/ remains compiled as part of EditorCore so it can use EditorCore & PRISM UI.
        ExcludedFiles.AddRange(new string[] {
            "Blueprint/Assets/",
            "Blueprint/Compiler/",
            "Blueprint/Core/",
            "Blueprint/Debug/",
            "Blueprint/Nodes/",
            "Blueprint/Platform/",
            "Blueprint/Plugins/",
            "Blueprint/Profiling/",
            "Blueprint/Resources/",
            "Blueprint/Runtime/",
            "Blueprint/Serialization/",
            "Blueprint/Testing/"
        });
    }
}
