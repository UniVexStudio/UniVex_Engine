// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The UniVex editor as an Android shared library. UniVex Hub loads it from an installed engine
// package and calls UveEditorRunUVE on the Hub's own activity; the editor takes over the activity's
// callbacks while it runs and gives them back by returning.

#include <android/log.h>
#include <android_native_app_glue.h>

#include <array>
#include <exception>
#include <filesystem>

#include "android_host.h"
#include "editor.h"
#include "editor_main.h"
#include "launcher_window.h"
#include "prism/Application.h"

namespace {

inline constexpr const char* kEditorRenderedMarkerUVE = "UVE_EDITOR_RENDERED";
/// Folders of the engine package beside the editor library (Engine/Tools/package_engine.py).
inline constexpr std::string_view kShadersDirectoryUVE = "shaders";
inline constexpr std::string_view kShaderCacheDirectoryUVE = "shader_cache";
/// Process exit code when the editor cannot start on this activity.
inline constexpr int kEditorStartFailedExitCodeUVE = 1;

} // namespace

/// The editor's entry point on Android (engine_install.h, kEditorAndroidEntryPointUVE): runs the
/// editor on `app`'s activity with the given command line and returns its exit code.
extern "C" __attribute__((visibility("default"))) int UveEditorRunUVE(android_app* const app, const int argc,
                                                                      char** const argv) {
    if (app == nullptr || argc < 1 || argv == nullptr) {
        return kEditorStartFailedExitCodeUVE;
    }
    UVE::AndroidHost::ActivityStateUVE activity;
    UVE::AndroidHost::AttachUVE(activity, app);
    // Back closes the editor's menus and dialogs (Escape); leaving the editor is its own command.
    activity.backIsEscape = true;
    if (!UVE::AndroidHost::WaitForWindowUVE(activity)) {
        return kEditorStartFailedExitCodeUVE;
    }
    const std::filesystem::path package = std::filesystem::path(argv[0]).parent_path();
    const std::filesystem::path dataRoot(app->activity->internalDataPath);
    UVE::App::EditorPlatformUVE platform;
    platform.nativeSurface = app->window;
    platform.prismAssetsDirectory = package / UVE::LauncherUI::kPrismAssetsDirectoryNameUVE;
    platform.shaderDirectory = package / kShadersDirectoryUVE;
    platform.shaderCacheDirectory = dataRoot / kShaderCacheDirectoryUVE;
    platform.logDirectory = dataRoot;
    platform.safeArea = [&activity]() -> std::array<float, 4> {
        const float scale = prism::Application::exists() ? prism::Application::get().dpiScale() : 1.0F;
        const float invScale = scale > 0.0F ? (1.0F / scale) : 1.0F;
        const UVE::Window::AndroidSafeAreaUVE& area = activity.safeArea;
        return {static_cast<float>(area.left) * invScale, static_cast<float>(area.top) * invScale,
                static_cast<float>(area.right) * invScale, static_cast<float>(area.imeBottom) * invScale};
    };
    platform.beforeFrame = [&activity](UVE::Core::EngineCoreUVE& core) {
        return UVE::AndroidHost::BeforeFrameUVE(activity, core, kEditorRenderedMarkerUVE);
    };
    __android_log_print(ANDROID_LOG_INFO, UVE::AndroidHost::kLogTagUVE, "UVE_EDITOR_STARTED");
    // Import opens Android's own file manager (it reaches Downloads, Drive and USB storage).
    UVE::Editor::SetSystemImportPickerUVE([&activity](std::function<void(const std::filesystem::path&)> onPicked) {
        return UVE::AndroidHost::StartFilePickUVE(
            activity, {".fbx", ".gltf", ".glb", ".obj", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".wav", ".ogg",
                       ".mp3", ".uvshader", ".glsl"},
            std::move(onPicked), [](const std::string& reason) {
                __android_log_print(ANDROID_LOG_WARN, UVE::AndroidHost::kLogTagUVE, "UVE_IMPORT_PICK %s", reason.c_str());
            });
    });
    int exitCode = kEditorStartFailedExitCodeUVE;
    try {
        exitCode = UVE::App::RunEditorUVE(argc, argv, platform);
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, UVE::AndroidHost::kLogTagUVE, "UVE_EDITOR_FAILED %s", error.what());
    }
    UVE::Editor::SetSystemImportPickerUVE({});
    activity.pendingPick = nullptr;
    activity.pendingPickError = nullptr;
    UVE::AndroidHost::DetachEngineUVE(activity);
    __android_log_print(ANDROID_LOG_INFO, UVE::AndroidHost::kLogTagUVE, "UVE_EDITOR_STOPPED %d", exitCode);
    return exitCode;
}

/// Every Android library UVBT links carries the activity glue, which calls android_main. The editor
/// library never starts an activity of its own: opened directly, it says where it belongs and closes.
extern "C" void android_main(android_app* const app) {
    __android_log_print(ANDROID_LOG_ERROR, UVE::AndroidHost::kLogTagUVE,
                        "UVE_EDITOR_FAILED the editor library runs inside UniVex Hub, not on its own");
    ANativeActivity_finish(app->activity);
    UVE::AndroidHost::DrainUntilDestroyedUVE(app);
}
