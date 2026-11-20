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
            "World"
        });

        // The product editor entry point lives at Engine/App/Internal/editor/
        ExtraSourceDirectories.AddRange(new string[] {
            "../../App/Internal/editor"
        });

        PublicIncludePaths.AddRange(new string[] {
            "../../App/Internal/editor",
            "../../App/Internal/editor/viewports"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "GL",
            "GLX",
            "X11"
        });
    }
}
