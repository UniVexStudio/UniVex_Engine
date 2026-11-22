// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The UniVex editor as an Android shared library. UniVex Hub loads it from an installed engine
// package and calls UveEditorRunUVE on the Hub's own activity; the editor takes over the activity's
// callbacks while it runs and gives them back by returning.

#include <android/log.h>
#include <android_native_app_glue.h>

#include <exception>
#include <filesystem>

#include "android_host.h"
#include "editor_main.h"
#include "launcher_window.h"

namespace {

inline constexpr const char* kEditorRenderedMarkerUVE = "UVE_EDITOR_RENDERED";
/// Folders of the engine package beside the editor library (Engine/Tools/package_engine.py).
inline constexpr std::string_view kShadersDirectoryUVE = "shaders";
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
    if (!UVE::AndroidHost::WaitForWindowUVE(activity)) {
        return kEditorStartFailedExitCodeUVE;
    }
    const std::filesystem::path package = std::filesystem::path(argv[0]).parent_path();
    UVE::App::EditorPlatformUVE platform;
    platform.nativeSurface = app->window;
    platform.prismAssetsDirectory = package / UVE::LauncherUI::kPrismAssetsDirectoryNameUVE;
    platform.shaderDirectory = package / kShadersDirectoryUVE;
    platform.logDirectory = std::filesystem::path(app->activity->internalDataPath);
    platform.beforeFrame = [&activity](UVE::Core::EngineCoreUVE& core) {
        return UVE::AndroidHost::BeforeFrameUVE(activity, core, kEditorRenderedMarkerUVE);
    };
    __android_log_print(ANDROID_LOG_INFO, UVE::AndroidHost::kLogTagUVE, "UVE_EDITOR_STARTED");
    int exitCode = kEditorStartFailedExitCodeUVE;
    try {
        exitCode = UVE::App::RunEditorUVE(argc, argv, platform);
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, UVE::AndroidHost::kLogTagUVE, "UVE_EDITOR_FAILED %s", error.what());
    }
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
