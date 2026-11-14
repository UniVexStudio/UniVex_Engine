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
            "Network",
            "Plugins",
            "Retarget",
            "Prism",
            "Math",
            "Strings",
            "Logging"
        });

        // The editor's PRISM windows: shared pieces in shell/, each kind of asset editor in a folder
        // of its own (Blueprint Class, Animation Graph, Widget Blueprint, Montage, Retarget, Animation Library).
        PrivateIncludePaths.AddRange(new string[] {
            "Internal/shell",
            "Internal/shell/blueprint",
            "Internal/shell/anim_graph",
            "Internal/shell/widget",
            "Internal/shell/montage",
            "Internal/shell/retarget",
            "Internal/shell/anim_library"
        });

        ExcludedFiles.AddRange(new string[] {
            "android:Internal/shell/editor_host.cpp",
            "android:editor_host.cpp",
            "desktop:Internal/shell/editor_host_android.cpp",
            "desktop:editor_host_android.cpp"
        });

        PublicDefinitions.AddRange(new string[] {
            "desktop:GLEW_STATIC",
            "desktop:GLEW_NO_GLU"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "android:EGL",
            "android:GLESv3",
            "android:android",
            "android:log"
        });
    }
}
