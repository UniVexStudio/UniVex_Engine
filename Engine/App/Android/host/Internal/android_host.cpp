// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "android_host.h"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <GLFW/glfw3.h>

#include <array>
#include <chrono>
#include <cmath>
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
/// Touch gestures (TouchGestureUVE). Distances are density-independent pixels.
inline constexpr float kTouchSlopDpUVE = 8.0F;              // a finger moving less than this is still
inline constexpr std::int64_t kLongPressMsUVE = 450;         // held still this long: a right click
inline constexpr float kPinchDpPerWheelNotchUVE = 40.0F;     // fingers spreading this far: one wheel notch
inline constexpr int kTwoFingerButtonUVE = GLFW_MOUSE_BUTTON_RIGHT;
inline constexpr int kThreeFingerButtonUVE = GLFW_MOUSE_BUTTON_MIDDLE;

[[nodiscard]] std::int64_t NowMsUVE() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Centre of the fingers on the screen, and the spread of the first two.
struct TouchPointsUVE final {
    int count = 0;
    float x = 0.0F;
    float y = 0.0F;
    float spread = 0.0F;
};

[[nodiscard]] TouchPointsUVE ReadPointsUVE(const AInputEvent* const event) {
    TouchPointsUVE points;
    points.count = static_cast<int>(AMotionEvent_getPointerCount(event));
    for (int i = 0; i < points.count; ++i) {
        points.x += AMotionEvent_getX(event, static_cast<std::size_t>(i));
        points.y += AMotionEvent_getY(event, static_cast<std::size_t>(i));
    }
    if (points.count > 0) {
        points.x /= static_cast<float>(points.count);
        points.y /= static_cast<float>(points.count);
    }
    if (points.count >= 2) {
        points.spread = std::hypot(AMotionEvent_getX(event, 0) - AMotionEvent_getX(event, 1),
                                   AMotionEvent_getY(event, 0) - AMotionEvent_getY(event, 1));
    }
    return points;
}

void ReleaseGestureButtonUVE(TouchGestureUVE& touch) {
    if (touch.button >= 0) {
        Window::DispatchAndroidGlfwMouseButtonUVE(touch.button, false, touch.lastX, touch.lastY);
        touch.button = -1;
    }
}

/// The one finger becomes a left-button press at where it went down (a drag or a tap).
void PressPendingLeftUVE(TouchGestureUVE& touch) {
    if (!touch.pending) {
        return;
    }
    touch.pending = false;
    touch.button = GLFW_MOUSE_BUTTON_LEFT;
    Window::DispatchAndroidGlfwMouseButtonUVE(touch.button, true, touch.startX, touch.startY);
}

/// Multi-finger gesture: the button for that many fingers, held at the fingers' centre.
void StartMultiFingerUVE(TouchGestureUVE& touch, const TouchPointsUVE& points) {
    touch.pending = false;
    ReleaseGestureButtonUVE(touch);
    touch.button = points.count >= 3 ? kThreeFingerButtonUVE : kTwoFingerButtonUVE;
    touch.lastX = points.x;
    touch.lastY = points.y;
    touch.pinchDistance = points.spread;
    Window::DispatchAndroidGlfwMouseButtonUVE(touch.button, true, points.x, points.y);
}

void HandleTouchUVE(ActivityStateUVE& state, const AInputEvent* const event) {
    TouchGestureUVE& touch = state.touch;
    const TouchPointsUVE points = ReadPointsUVE(event);
    switch (AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) {
    case AMOTION_EVENT_ACTION_DOWN:
        touch = TouchGestureUVE{};
        touch.fingers = 1;
        touch.pending = true;
        touch.startX = touch.lastX = points.x;
        touch.startY = touch.lastY = points.y;
        touch.startMs = NowMsUVE();
        Window::DispatchAndroidGlfwCursorUVE(points.x, points.y);
        break;
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        touch.fingers = points.count;
        if (!touch.finished) {
            StartMultiFingerUVE(touch, points);
        }
        break;
    case AMOTION_EVENT_ACTION_MOVE:
        if (touch.finished) {
            break;
        }
        if (touch.fingers == 1) {
            const float slop = kTouchSlopDpUVE * state.pixelsPerDp;
            if (touch.pending && std::hypot(points.x - touch.startX, points.y - touch.startY) > slop) {
                PressPendingLeftUVE(touch);
            }
            if (!touch.pending) {
                touch.lastX = points.x;
                touch.lastY = points.y;
                Window::DispatchAndroidGlfwCursorUVE(points.x, points.y);
            }
        } else if (points.count >= 2) {
            touch.lastX = points.x;
            touch.lastY = points.y;
            Window::DispatchAndroidGlfwCursorUVE(points.x, points.y);
            const float notch = kPinchDpPerWheelNotchUVE * state.pixelsPerDp;
            if (points.count == 2 && notch > 0.0F) {
                const float notches = (points.spread - touch.pinchDistance) / notch;
                if (std::abs(notches) >= 1.0F) {
                    Window::DispatchAndroidGlfwScrollUVE(std::trunc(notches));
                    touch.pinchDistance += std::trunc(notches) * notch;
                }
            }
        }
        break;
    case AMOTION_EVENT_ACTION_POINTER_UP:
        // Lifting a finger ends a multi-finger gesture; the rest of it moves nothing.
        touch.fingers = points.count - 1;
        ReleaseGestureButtonUVE(touch);
        touch.pending = false;
        touch.finished = true;
        break;
    case AMOTION_EVENT_ACTION_UP:
        // A tap is a press and a release where the finger went down.
        PressPendingLeftUVE(touch);
        ReleaseGestureButtonUVE(touch);
        touch = TouchGestureUVE{};
        break;
    case AMOTION_EVENT_ACTION_CANCEL:
        touch.pending = false;
        ReleaseGestureButtonUVE(touch);
        touch = TouchGestureUVE{};
        break;
    default:
        break;
    }
}

/// Android key codes and the GLFW keys the interface reads for them.
struct KeyMappingUVE final {
    int32_t android;
    int glfw;
};
inline constexpr std::array<KeyMappingUVE, 21> kNamedKeysUVE{{
    {AKEYCODE_SPACE, GLFW_KEY_SPACE},           {AKEYCODE_ENTER, GLFW_KEY_ENTER},
    {AKEYCODE_NUMPAD_ENTER, GLFW_KEY_KP_ENTER}, {AKEYCODE_DEL, GLFW_KEY_BACKSPACE},
    {AKEYCODE_FORWARD_DEL, GLFW_KEY_DELETE},    {AKEYCODE_TAB, GLFW_KEY_TAB},
    {AKEYCODE_ESCAPE, GLFW_KEY_ESCAPE},         {AKEYCODE_DPAD_LEFT, GLFW_KEY_LEFT},
    {AKEYCODE_DPAD_RIGHT, GLFW_KEY_RIGHT},      {AKEYCODE_DPAD_UP, GLFW_KEY_UP},
    {AKEYCODE_DPAD_DOWN, GLFW_KEY_DOWN},        {AKEYCODE_MOVE_HOME, GLFW_KEY_HOME},
    {AKEYCODE_MOVE_END, GLFW_KEY_END},          {AKEYCODE_PAGE_UP, GLFW_KEY_PAGE_UP},
    {AKEYCODE_PAGE_DOWN, GLFW_KEY_PAGE_DOWN},   {AKEYCODE_SHIFT_LEFT, GLFW_KEY_LEFT_SHIFT},
    {AKEYCODE_SHIFT_RIGHT, GLFW_KEY_RIGHT_SHIFT}, {AKEYCODE_CTRL_LEFT, GLFW_KEY_LEFT_CONTROL},
    {AKEYCODE_CTRL_RIGHT, GLFW_KEY_RIGHT_CONTROL}, {AKEYCODE_ALT_LEFT, GLFW_KEY_LEFT_ALT},
    {AKEYCODE_ALT_RIGHT, GLFW_KEY_RIGHT_ALT},
}};
/// The first character a key may type: control characters and DEL type nothing.
inline constexpr unsigned int kFirstPrintableCharacterUVE = 0x20U;
inline constexpr unsigned int kDeleteCharacterUVE = 0x7FU;

[[nodiscard]] int GlfwKeyUVE(const int32_t keyCode) {
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        return GLFW_KEY_A + (keyCode - AKEYCODE_A);
    }
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        return GLFW_KEY_0 + (keyCode - AKEYCODE_0);
    }
    for (const KeyMappingUVE& key : kNamedKeysUVE) {
        if (key.android == keyCode) {
            return key.glfw;
        }
    }
    return GLFW_KEY_UNKNOWN;
}

[[nodiscard]] int GlfwModsUVE(const int32_t metaState) {
    int mods = 0;
    mods |= (metaState & AMETA_SHIFT_ON) != 0 ? GLFW_MOD_SHIFT : 0;
    mods |= (metaState & AMETA_CTRL_ON) != 0 ? GLFW_MOD_CONTROL : 0;
    mods |= (metaState & AMETA_ALT_ON) != 0 ? GLFW_MOD_ALT : 0;
    return mods;
}

/// A hardware or soft keyboard key: the GLFW key and, on press, the character it types.
[[nodiscard]] int32_t HandleKeyUVE(const ActivityStateUVE& state, const AInputEvent* const event) {
    const int32_t action = AKeyEvent_getAction(event);
    int32_t keyCode = AKeyEvent_getKeyCode(event);
    if (keyCode == AKEYCODE_BACK) {
        if (!state.backIsEscape) {
            return 0;   // Android's own Back: leaves the app
        }
        keyCode = AKEYCODE_ESCAPE;
    }
    if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) {
        return 0;
    }
    const int glfwKey = GlfwKeyUVE(keyCode);
    const int32_t metaState = AKeyEvent_getMetaState(event);
    if (glfwKey != GLFW_KEY_UNKNOWN) {
        const int glfwAction = action == AKEY_EVENT_ACTION_UP ? GLFW_RELEASE
                               : AKeyEvent_getRepeatCount(event) > 0 ? GLFW_REPEAT
                                                                      : GLFW_PRESS;
        Window::DispatchAndroidGlfwKeyUVE(glfwKey, glfwAction, GlfwModsUVE(metaState));
    }
    if (action == AKEY_EVENT_ACTION_DOWN && keyCode != AKEYCODE_ESCAPE) {
        const unsigned int character = Window::AndroidKeyCharacterUVE(state.app->activity, action, keyCode, metaState);
        if (character >= kFirstPrintableCharacterUVE && character != kDeleteCharacterUVE) {
            Window::DispatchAndroidGlfwCharUVE(character);
        }
    }
    return glfwKey != GLFW_KEY_UNKNOWN ? 1 : 0;
}

/// Shows the soft keyboard while the interface wants typed text, and hides it after.
void UpdateSoftKeyboardUVE(ActivityStateUVE& state) {
    const bool wanted = Window::AndroidGlfwTextInputWantedUVE();
    if (wanted == state.keyboardShown) {
        return;
    }
    state.keyboardShown = wanted;
    if (wanted) {
        ANativeActivity_showSoftInput(state.app->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
    } else {
        ANativeActivity_hideSoftInput(state.app->activity, 0);
    }
}

/// A finger held still long enough is a right click (context menus).
void UpdateLongPressUVE(TouchGestureUVE& touch) {
    if (!touch.pending || touch.fingers != 1 || NowMsUVE() - touch.startMs < kLongPressMsUVE) {
        return;
    }
    touch.pending = false;
    touch.finished = true;
    Window::DispatchAndroidGlfwMouseButtonUVE(GLFW_MOUSE_BUTTON_RIGHT, true, touch.startX, touch.startY);
    Window::DispatchAndroidGlfwMouseButtonUVE(GLFW_MOUSE_BUTTON_RIGHT, false, touch.startX, touch.startY);
}

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

int32_t HandleInputUVE(android_app* const app, AInputEvent* const event) {
    auto& state = *static_cast<ActivityStateUVE*>(app->userData);
    switch (AInputEvent_getType(event)) {
    case AINPUT_EVENT_TYPE_MOTION:
        HandleTouchUVE(state, event);
        return 1;
    case AINPUT_EVENT_TYPE_KEY:
        return HandleKeyUVE(state, event);
    default:
        return 0;
    }
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
    state.pixelsPerDp = static_cast<float>(AConfiguration_getDensity(app->config)) / kBaselineDensityUVE;
    state.touch = TouchGestureUVE{};
    state.keyboardShown = false;
    Window::ResetAndroidGlfwWindowUVE();
    Window::SetAndroidGlfwContentScaleUVE(state.pixelsPerDp);
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
    UpdateLongPressUVE(state.touch);
    UpdateSoftKeyboardUVE(state);
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
