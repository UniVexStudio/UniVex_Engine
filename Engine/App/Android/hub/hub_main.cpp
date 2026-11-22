// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// UniVex Hub on Android: the desktop Hub's window in a NativeActivity. The APK's data (PRISM's
// fonts and themes, project templates, the engine feed, built-in shaders) is unpacked into the app's
// private storage, which also holds the project list and downloaded engines.

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "android_activity.h"
#include "android_glfw.h"
#include "android_window_manager.h"
#include "engine_core.h"
#include "engine_release_feed.h"
#include "http_client.h"
#include "hub_window.h"
#include "launcher_window.h"
#include "project_list.h"
#include "project_registry.h"

namespace {

inline constexpr const char* kLogTagUVE = "UniVex";
/// Written by build_apk.py: every file under the APK's assets/, one relative path per line.
inline constexpr const char* kAssetIndexUVE = "asset-index.txt";
/// The APK's data, unpacked here inside the app's private storage.
inline constexpr std::string_view kUnpackedAppDirectoryUVE = "app";
inline constexpr std::string_view kShadersDirectoryUVE = "shaders";
/// Android reports density in dots per inch; 160 dpi is scale 1.
inline constexpr float kBaselineDensityUVE = 160.0F;

struct HubActivityUVE final {
    android_app* app = nullptr;
    bool resumed = false;
    /// The running Hub's window manager, once the loop has started.
    UVE::Window::AndroidWindowManagerUVE* windowManager = nullptr;
    /// The screen edges display cutouts cover, read again whenever the window or its layout changes.
    UVE::Window::AndroidSafeAreaUVE safeArea;
    /// Frames the loop has started; the first one is on screen once the second starts.
    std::uint64_t framesStarted = 0;
};

[[nodiscard]] std::string ReadAssetUVE(AAssetManager* const assets, const std::string& name) {
    AAsset* const asset = AAssetManager_open(assets, name.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        return {};
    }
    const auto* data = static_cast<const char*>(AAsset_getBuffer(asset));
    std::string text = data != nullptr ? std::string(data, static_cast<std::size_t>(AAsset_getLength(asset))) : "";
    AAsset_close(asset);
    return text;
}

/// Copies every asset the index lists into `destination`, replacing what an older APK left there.
[[nodiscard]] bool UnpackAssetsUVE(AAssetManager* const assets, const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::remove_all(destination, error);
    std::istringstream index(ReadAssetUVE(assets, kAssetIndexUVE));
    std::string name;
    bool any = false;
    while (std::getline(index, name)) {
        const std::filesystem::path relative(name);
        if (name.empty() || relative.is_absolute() || name.find("..") != std::string::npos) {
            continue;
        }
        const std::filesystem::path target = destination / relative;
        std::filesystem::create_directories(target.parent_path(), error);
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        output << ReadAssetUVE(assets, name);
        if (!output) {
            return false;
        }
        any = true;
    }
    return any;
}

void HandleCommandUVE(android_app* const app, const int32_t command) {
    auto& activity = *static_cast<HubActivityUVE*>(app->userData);
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        activity.safeArea = UVE::Window::ReadAndroidSafeAreaUVE(app->activity);
        UVE::Window::SetAndroidGlfwSurfaceUVE(app->window);
        if (activity.windowManager != nullptr && !activity.windowManager->AttachNativeWindowUVE(app->window)) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_FAILED the surface could not be reattached");
            ANativeActivity_finish(app->activity);
        }
        break;
    case APP_CMD_TERM_WINDOW:
        // The surface is destroyed once this returns, so stop drawing to it now.
        if (activity.windowManager != nullptr) {
            activity.windowManager->DetachNativeWindowUVE();
        }
        UVE::Window::SetAndroidGlfwSurfaceUVE(nullptr);
        break;
    case APP_CMD_RESUME:
        activity.resumed = true;
        break;
    case APP_CMD_PAUSE:
        activity.resumed = false;
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
    case APP_CMD_CONFIG_CHANGED:
        // Rotation and layout changes move the cutouts to other edges.
        activity.safeArea = UVE::Window::ReadAndroidSafeAreaUVE(app->activity);
        break;
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_LOST_FOCUS:
        UVE::Window::SetAndroidGlfwFocusUVE(command == APP_CMD_GAINED_FOCUS);
        if (activity.windowManager != nullptr) {
            activity.windowManager->SetFocusedUVE(command == APP_CMD_GAINED_FOCUS);
        }
        break;
    default:
        break;
    }
}

int32_t HandleInputUVE(android_app*, AInputEvent* const event) {
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) {
        return 0;
    }
    using UVE::Window::AndroidPointerPhaseUVE;
    AndroidPointerPhaseUVE phase;
    switch (AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) {
    case AMOTION_EVENT_ACTION_DOWN:
        phase = AndroidPointerPhaseUVE::Down;
        break;
    case AMOTION_EVENT_ACTION_MOVE:
        phase = AndroidPointerPhaseUVE::Move;
        break;
    case AMOTION_EVENT_ACTION_UP:
        phase = AndroidPointerPhaseUVE::Up;
        break;
    case AMOTION_EVENT_ACTION_CANCEL:
        phase = AndroidPointerPhaseUVE::Cancel;
        break;
    default:
        return 0;   // further fingers: the first one drives the interface
    }
    UVE::Window::DispatchAndroidGlfwPointerUVE(phase, AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
    return 1;
}

/// Handles the activity's pending events; blocks while the app is in the background. Returns false
/// once the activity is closing.
[[nodiscard]] bool PumpEventsUVE(HubActivityUVE& activity) {
    android_app* const app = activity.app;
    for (;;) {
        const bool active = activity.resumed && app->window != nullptr;
        android_poll_source* source = nullptr;
        const int result = ALooper_pollOnce(active ? 0 : -1, nullptr, nullptr, reinterpret_cast<void**>(&source));
        if (result >= 0 && source != nullptr) {
            source->process(app, source);
        }
        if (app->destroyRequested != 0) {
            return false;
        }
        if (result < 0 && active) {
            return true;
        }
    }
}

} // namespace

extern "C" void android_main(android_app* const app) {
    HubActivityUVE activity;
    activity.app = app;
    app->userData = &activity;
    app->onAppCmd = HandleCommandUVE;
    app->onInputEvent = HandleInputUVE;
    UVE::Online::SetAndroidJavaVmUVE(app->activity->vm);
    UVE::Window::SetAndroidGlfwContentScaleUVE(
        static_cast<float>(AConfiguration_getDensity(app->config)) / kBaselineDensityUVE);

    // The window manager needs the surface from the start.
    while (app->window == nullptr) {
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0 && source != nullptr) {
            source->process(app, source);
        }
        if (app->destroyRequested != 0) {
            return;
        }
    }

    const std::filesystem::path data(app->activity->internalDataPath);
    // The project list, settings and engines live under the app's private storage, and the engine's
    // relative paths (settings, caches, logs) resolve there too: the process starts in "/", which is
    // not writable.
    setenv("HOME", data.c_str(), 1);
    std::error_code directoryError;
    std::filesystem::current_path(data, directoryError);
    if (directoryError) {
        __android_log_print(ANDROID_LOG_WARN, kLogTagUVE, "UVE_HUB_WARNING the data directory could not be entered: %s",
                            directoryError.message().c_str());
    }
    const std::filesystem::path unpacked = data / kUnpackedAppDirectoryUVE;
    if (!UnpackAssetsUVE(app->activity->assetManager, unpacked)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_FAILED the APK's data could not be unpacked");
        return;
    }

    try {
        UVE::Hub::HubContextUVE context;
        context.templatesDirectory = unpacked / UVE::Pack::kProjectTemplatesDirectoryNameUVE;
        std::string registryError;
        const std::optional<std::filesystem::path> registry = UVE::Pack::DefaultProjectRegistryFileUVE(&registryError);
        if (!registry.has_value()) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_FAILED %s", registryError.c_str());
            return;
        }
        context.registryFile = *registry;
        context.settingsFile = context.registryFile.parent_path() / UVE::Pack::kLauncherSettingsFileNameUVE;
        context.enginesRoot = context.registryFile.parent_path() / UVE::Online::kEnginesDirectoryNameUVE;
        context.feed = UVE::Online::LoadEngineFeedSourceUVE(unpacked / UVE::Online::kEngineFeedFileNameUVE);

        UVE::LauncherUI::LauncherWindowDescUVE window;
        window.title = std::string(UVE::Hub::kHubWindowTitleUVE);
        // Full screen on a phone: the system owns the window, so there is no title bar or buttons.
        window.windowChrome = false;
        window.safeArea = [&activity] {
            const UVE::Window::AndroidSafeAreaUVE& area = activity.safeArea;
            return prism::Margin(static_cast<float>(area.left), static_cast<float>(area.top),
                                 static_cast<float>(area.right), static_cast<float>(area.bottom));
        };
        window.nativeSurface = app->window;
        window.shaderDirectory = unpacked / kShadersDirectoryUVE;
        window.prismAssetsDirectory = unpacked / UVE::LauncherUI::kPrismAssetsDirectoryNameUVE;
        window.logFile = context.registryFile.parent_path() / UVE::Hub::kHubLogFileNameUVE;
        window.beforeFrame = [&activity](UVE::Core::EngineCoreUVE& core) {
            activity.windowManager =
                dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(&core.GetServicesUVE().GetWindowManagerUVE());
            if (++activity.framesStarted == 2U) {
                __android_log_print(ANDROID_LOG_INFO, kLogTagUVE, "UVE_HUB_RENDERED");
            }
            return PumpEventsUVE(activity) && !UVE::Window::AndroidGlfwCloseRequestedUVE();
        };

        UVE::Hub::HubWindowUVE hub(std::move(context));
        __android_log_print(ANDROID_LOG_INFO, kLogTagUVE, "UVE_HUB_STARTED");
        const int exitCode = UVE::LauncherUI::RunLauncherWindowUVE(
            window, [&hub] { return hub.BuildUVE(); }, [&hub] { hub.TickUVE(); });
        activity.windowManager = nullptr;
        __android_log_print(ANDROID_LOG_INFO, kLogTagUVE, "UVE_HUB_STOPPED %d", exitCode);
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_HUB_FAILED %s", error.what());
    }
    // The activity finishes when the Hub stops on its own (a failed start), not only on destroy.
    ANativeActivity_finish(app->activity);
    while (app->destroyRequested == 0) {
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0 && source != nullptr) {
            source->process(app, source);
        }
    }
}
