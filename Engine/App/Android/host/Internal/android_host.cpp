// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "android_host.h"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#include "android_glfw.h"
#include "android_window_manager.h"
#include "engine_core.h"
#include "http_client.h"

namespace UVE::AndroidHost {
namespace {

/// Android reports density in dots per inch; 160 dpi is scale 1.
inline constexpr float kBaselineDensityUVE = 160.0F;

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

void HandleCommandUVE(android_app* const app, const int32_t command) {
    auto& state = *static_cast<ActivityStateUVE*>(app->userData);
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        state.safeArea = Window::ReadAndroidSafeAreaUVE(app->activity);
        Window::SetAndroidGlfwSurfaceUVE(app->window);
        if (state.windowManager != nullptr && !state.windowManager->AttachNativeWindowUVE(app->window)) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTagUVE, "UVE_ANDROID_HOST_FAILED the surface could not be reattached");
            ANativeActivity_finish(app->activity);
        }
        break;
    case APP_CMD_TERM_WINDOW:
        // The surface is destroyed once this returns, so stop drawing to it now.
        if (state.windowManager != nullptr) {
            state.windowManager->DetachNativeWindowUVE();
        }
        Window::SetAndroidGlfwSurfaceUVE(nullptr);
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
    case APP_CMD_CONFIG_CHANGED:
        // Rotation and layout changes move the cutouts to other edges.
        state.safeArea = Window::ReadAndroidSafeAreaUVE(app->activity);
        break;
    case APP_CMD_RESUME:
        state.resumed = true;
        break;
    case APP_CMD_PAUSE:
        state.resumed = false;
        break;
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_LOST_FOCUS:
        Window::SetAndroidGlfwFocusUVE(command == APP_CMD_GAINED_FOCUS);
        if (state.windowManager != nullptr) {
            state.windowManager->SetFocusedUVE(command == APP_CMD_GAINED_FOCUS);
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
    using Window::AndroidPointerPhaseUVE;
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
    Window::DispatchAndroidGlfwPointerUVE(phase, AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
    return 1;
}

} // namespace

void AttachUVE(ActivityStateUVE& state, android_app* const app) {
    state.app = app;
    state.windowManager = nullptr;
    state.framesStarted = 0;
    // Attached while the activity is already in the foreground (the editor taking over from the Hub).
    state.resumed = state.resumed || app->window != nullptr;
    app->userData = &state;
    app->onAppCmd = HandleCommandUVE;
    app->onInputEvent = HandleInputUVE;
    Online::SetAndroidJavaVmUVE(app->activity->vm);
    Window::SetAndroidGlfwContentScaleUVE(static_cast<float>(AConfiguration_getDensity(app->config)) / kBaselineDensityUVE);
    Window::SetAndroidGlfwSurfaceUVE(app->window);
    state.safeArea = Window::ReadAndroidSafeAreaUVE(app->activity);
}

bool WaitForWindowUVE(ActivityStateUVE& state) {
    android_app* const app = state.app;
    while (app->window == nullptr) {
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0 && source != nullptr) {
            source->process(app, source);
        }
        if (app->destroyRequested != 0) {
            return false;
        }
    }
    return true;
}

bool PumpEventsUVE(ActivityStateUVE& state) {
    android_app* const app = state.app;
    for (;;) {
        const bool active = state.resumed && app->window != nullptr;
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

bool BeforeFrameUVE(ActivityStateUVE& state, Core::EngineCoreUVE& core, const char* const renderedMarker) {
    state.windowManager = dynamic_cast<Window::AndroidWindowManagerUVE*>(&core.GetServicesUVE().GetWindowManagerUVE());
    if (++state.framesStarted == 2U && renderedMarker != nullptr) {
        __android_log_print(ANDROID_LOG_INFO, kLogTagUVE, "%s", renderedMarker);
    }
    return PumpEventsUVE(state) && !Window::AndroidGlfwCloseRequestedUVE();
}

void DetachEngineUVE(ActivityStateUVE& state) {
    state.windowManager = nullptr;
}

void EnterDataDirectoryUVE(const android_app* const app) {
    const std::filesystem::path data(app->activity->internalDataPath);
    setenv("HOME", data.c_str(), 1);
    std::error_code error;
    std::filesystem::current_path(data, error);
    if (error) {
        __android_log_print(ANDROID_LOG_WARN, kLogTagUVE, "UVE_ANDROID_HOST_WARNING the data directory could not be entered: %s",
                            error.message().c_str());
    }
}

bool UnpackAssetsUVE(AAssetManager* const assets, const std::filesystem::path& destination) {
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

void DrainUntilDestroyedUVE(android_app* const app) {
    while (app->destroyRequested == 0) {
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0 && source != nullptr) {
            source->process(app, source);
        }
    }
}

} // namespace UVE::AndroidHost
