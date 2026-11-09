// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

public class EditorAppModule : ModuleRules
{
    public EditorAppModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PublicDependencyModuleNames.AddRange(new string[] {
            "EditorCore",
            "Viewport",
            "Engine",
            "ImGui",
            "World"
        });

        // The product editor entry point lives at Engine/App/Internal/editor/
        ExtraSourceDirectories.AddRange(new string[] {
            "../../App/Internal/editor"
        });

        PublicIncludePaths.AddRange(new string[] {
            "../../App/Internal/editor"
        });

        // Exclude the old smoke harness main.cpp so it does not collide with the product editor main.cpp
        ExcludedFiles.AddRange(new string[] {
            "Engine/Editor/EditorApp/Internal/main.cpp",
            "Internal/main.cpp"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "GL",
            "GLX",
            "X11"
        });
    }
}
