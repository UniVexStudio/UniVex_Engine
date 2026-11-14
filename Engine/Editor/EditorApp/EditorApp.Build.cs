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

        // The product editor entry point lives at Engine/App/Internal/editor/ (desktop)
        // or Engine/App/Android/editor/ (Android)
        ExtraSourceDirectories.AddRange(new string[] {
            "../../App/Internal/editor",
            "android:../../App/Android/editor"
        });

        PublicIncludePaths.AddRange(new string[] {
            "../../App/Internal/editor",
            "android:../../App/Android/editor"
        });

        // Exclude the old smoke harness main.cpp so it does not collide with the product editor main.cpp
        // On Android, exclude desktop editor main.cpp so editor_android_main.cpp owns NativeActivity startup
        ExcludedFiles.AddRange(new string[] {
            "Engine/Editor/EditorApp/Internal/main.cpp",
            "Internal/main.cpp",
            "android:main.cpp"
        });

        PublicSystemLibraries.AddRange(new string[] {
            "desktop:GL",
            "desktop:GLX",
            "desktop:X11",
            "android:EGL",
            "android:GLESv3",
            "android:android",
            "android:log"
        });
    }
}
