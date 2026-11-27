// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The UniVex editor as an Android shared library. UniVex Hub loads it from an installed engine
// package and calls UveEditorRun on the Hub's own activity; the editor takes over the activity's
// callbacks while it runs and gives them back by returning.

#include <android/log.h>
#include <android_native_app_glue.h>

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>

#include "android_host.h"
#include "editor.h"
#include "editor_main.h"
#include "prism/Application.h"
#include "univex_android_editor_abi.h"

namespace {

inline constexpr const char* kEditorRenderedMarker = "UVE_EDITOR_RENDERED";
/// Folders of the engine package beside the editor library (Engine/Tools/package_engine.py).
inline constexpr std::string_view kPrismAssetsDirectoryName = "prism_assets";
inline constexpr std::string_view kShadersDirectory = "shaders";
inline constexpr std::string_view kShaderCacheDirectory = "shader_cache";
/// Process exit code when the editor cannot start on this activity.
inline constexpr int kEditorStartFailedExitCode = 1;

} // namespace

/// Reports the Android editor host ABI version implemented by this library (contracts/v2).
extern "C" __attribute__((visibility("default"))) std::uint32_t UveEditorAbiVersion(void) {
    return UVE_ANDROID_EDITOR_ABI_VERSION;
}

/// The editor's entry point on Android (engine_install.h, kEditorAndroidEntryPoint): runs the
/// editor on `app`'s activity with the given command line and returns its exit code.
extern "C" __attribute__((visibility("default"))) int UveEditorRun(android_app* const app, const int argc,
                                                                      char** const argv) {
    if (app == nullptr || argc < 1 || argv == nullptr) {
        return kEditorStartFailedExitCode;
    }
    struct AppCallbackRestore final {
        android_app* app = nullptr;
        void* userData = nullptr;
        void (*onAppCmd)(android_app*, int32_t) = nullptr;
        int32_t (*onInputEvent)(android_app*, AInputEvent*) = nullptr;
        explicit AppCallbackRestore(android_app* target) noexcept
            : app(target),
              userData(target->userData),
              onAppCmd(target->onAppCmd),
              onInputEvent(target->onInputEvent) {}
        ~AppCallbackRestore() {
            app->userData = userData;
            app->onAppCmd = onAppCmd;
            app->onInputEvent = onInputEvent;
        }
    } restoreAppCallbacks{app};
    UVE::AndroidHost::ActivityState activity;
    UVE::AndroidHost::Attach(activity, app);
    // Back closes the editor's menus and dialogs (Escape); leaving the editor is its own command.
    activity.backIsEscape = true;
    if (!UVE::AndroidHost::WaitForWindow(activity)) {
        return kEditorStartFailedExitCode;
    }
    const std::filesystem::path package = std::filesystem::path(argv[0]).parent_path();
    const std::filesystem::path dataRoot(app->activity->internalDataPath);
    UVE::App::EditorPlatform platform;
    platform.nativeSurface = app->window;
    platform.prismAssetsDirectory = package / kPrismAssetsDirectoryName;
    platform.shaderDirectory = package / kShadersDirectory;
    platform.shaderCacheDirectory = dataRoot / kShaderCacheDirectory;
    platform.logDirectory = dataRoot;
    platform.safeArea = [&activity]() -> std::array<float, 4> {
        const float scale = prism::Application::exists() ? prism::Application::get().dpiScale() : 1.0F;
        const float invScale = scale > 0.0F ? (1.0F / scale) : 1.0F;
        const UVE::Window::AndroidSafeArea& area = activity.safeArea;
        return {static_cast<float>(area.left) * invScale, static_cast<float>(area.top) * invScale,
                static_cast<float>(area.right) * invScale, static_cast<float>(area.imeBottom) * invScale};
    };
    platform.beforeFrame = [&activity](UVE::Core::EngineCore& core) {
        return UVE::AndroidHost::BeforeFrame(activity, core, kEditorRenderedMarker);
    };
    __android_log_print(ANDROID_LOG_INFO, UVE::AndroidHost::kLogTag, "UVE_EDITOR_STARTED");
    // Import opens Android's own file manager directly (multi-select files and whole folders).
    static const std::vector<std::string> kImportExtensions{
        ".fbx", ".gltf", ".glb", ".obj", ".uvanim", ".png", ".jpg", ".jpeg",
        ".tga", ".bmp",  ".hdr", ".wav", ".ogg",    ".mp3", ".uvs", ".uvshader", ".glsl"};
    UVE::Editor::SetSystemImportFilesPicker(
        [&activity](std::function<void(const std::vector<std::filesystem::path>&)> onPicked) {
            return UVE::AndroidHost::StartMultiFilePick(
                activity, kImportExtensions, std::move(onPicked), [](const std::string& reason) {
                    __android_log_print(ANDROID_LOG_WARN, UVE::AndroidHost::kLogTag, "UVE_IMPORT_PICK %s",
                                        reason.c_str());
                });
        });
    UVE::Editor::SetSystemImportFolderPicker(
        [&activity](std::function<void(const std::vector<std::filesystem::path>&)> onPicked) {
            return UVE::AndroidHost::StartFolderPick(
                activity, std::move(onPicked), [](const std::string& reason) {
                    __android_log_print(ANDROID_LOG_WARN, UVE::AndroidHost::kLogTag, "UVE_IMPORT_FOLDER_PICK %s",
                                        reason.c_str());
                });
        });
    int exitCode = kEditorStartFailedExitCode;
    try {
        exitCode = UVE::App::RunEditor(argc, argv, platform);
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, UVE::AndroidHost::kLogTag, "UVE_EDITOR_FAILED %s", error.what());
    }
    UVE::Editor::SetSystemImportFilesPicker({});
    UVE::Editor::SetSystemImportFolderPicker({});
    UVE::Editor::SetSystemImportPicker({});
    activity.pendingPick = nullptr;
    activity.pendingPickError = nullptr;
    activity.pendingMultiPick = nullptr;
    activity.pendingMultiPickError = nullptr;
    UVE::AndroidHost::DetachEngine(activity);
    __android_log_print(ANDROID_LOG_INFO, UVE::AndroidHost::kLogTag, "UVE_EDITOR_STOPPED %d", exitCode);
    return exitCode;
}

/// Every Android library UVBT links carries the activity glue, which calls android_main. The editor
/// library never starts an activity of its own: opened directly, it says where it belongs and closes.
extern "C" void android_main(android_app* const app) {
    __android_log_print(ANDROID_LOG_ERROR, UVE::AndroidHost::kLogTag,
                        "UVE_EDITOR_FAILED the editor library runs inside UniVex Hub, not on its own");
    ANativeActivity_finish(app->activity);
    UVE::AndroidHost::DrainUntilDestroyed(app);
}
