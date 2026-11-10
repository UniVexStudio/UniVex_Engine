// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using UniVexBuildTool;

// PRISM - the engine's own retained-mode UI framework (editor shell, docking, widgets, blueprint graph + VM).
// Hosted by the engine: no SDL window backend; text through FreeType; OpenGL 3.3 reference render backend.
public class PrismModule : ModuleRules
{
    public PrismModule(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.StaticLibrary;

        PrivateIncludePaths.AddRange(new string[] {
            "Internal/gl"
        });

        PublicDefinitions.AddRange(new string[] {
            "PRISM_HAS_GL=1"
        });

        // "win:" entries link on Windows only (UI Automation bridge; UVBT builds the MinGW import library
        // from Internal/windows/uiautomationcore.def).
        PublicSystemLibraries.AddRange(new string[] {
            "freetype",
            "win:uiautomationcore",
            "win:ole32",
            "win:oleaut32"
        });

        ExcludedFiles.AddRange(new string[] {
            "tests/"
        });
    }
}
