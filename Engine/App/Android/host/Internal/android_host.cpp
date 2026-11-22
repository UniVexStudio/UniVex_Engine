// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "android_host.h"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <optional>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
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
inline constexpr float kTouchSlopDpUVE = 6.0F;                 // finger stays inside this to keep long-press armed
inline constexpr std::int64_t kLongPressMsUVE = 450;           // held still this long: a right click
inline constexpr float kPinchDpPerWheelNotchUVE = 36.0F;       // fingers spreading this far: 1.0 wheel unit (continuous)
inline constexpr int kMultiFingerButtonUVE = GLFW_MOUSE_BUTTON_MIDDLE;
inline constexpr float kCancelOffscreenPosUVE = -100000.0F;
inline constexpr std::int64_t kIdleWakeHoldMsUVE = 600;        // keep full-rate frames after touch/IME for fling decay

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

[[nodiscard]] bool IsPointerClaimedUVE(const TouchGestureUVE& touch, const std::int32_t id) {
    return std::find(touch.claimedPointerIds.begin(), touch.claimedPointerIds.end(), id) !=
           touch.claimedPointerIds.end();
}

void EraseClaimedPointerUVE(TouchGestureUVE& touch, const std::int32_t id) {
    touch.claimedPointerIds.erase(std::remove(touch.claimedPointerIds.begin(), touch.claimedPointerIds.end(), id),
                                  touch.claimedPointerIds.end());
}

[[nodiscard]] bool IsHardwareMouseEventUVE(const AInputEvent* const event) {
    const std::int32_t source = AInputEvent_getSource(event);
    if ((source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE) {
        return true;
    }
    return AMotionEvent_getPointerCount(event) > 0 &&
           AMotionEvent_getToolType(event, 0) == AMOTION_EVENT_TOOL_TYPE_MOUSE;
}

void HandleMouseEventUVE(ActivityStateUVE& state, const AInputEvent* const event) {
    state.lastInteractionMs = NowMsUVE();
    Window::SetAndroidGlfwHardwareInputUVE(true);
    const float x = AMotionEvent_getX(event, 0);
    const float y = AMotionEvent_getY(event, 0);
    const std::int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    if (action == AMOTION_EVENT_ACTION_SCROLL) {
        const float vscroll = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
        if (std::abs(vscroll) > 1.0e-4F) {
            Window::DispatchAndroidGlfwScrollUVE(vscroll);
        }
        return;
    }
    Window::DispatchAndroidGlfwCursorUVE(x, y);
    const std::int32_t buttons = AMotionEvent_getButtonState(event);
    const std::int32_t prev = state.touch.mouseButtonMask;
    const auto syncButton = [&](const std::int32_t androidMask, const int glfwButton) {
        const bool wasDown = (prev & androidMask) != 0;
        const bool isDown = (buttons & androidMask) != 0;
        if (wasDown != isDown) {
            Window::DispatchAndroidGlfwMouseButtonUVE(glfwButton, isDown, x, y);
        }
    };
    syncButton(AMOTION_EVENT_BUTTON_PRIMARY, GLFW_MOUSE_BUTTON_LEFT);
    syncButton(AMOTION_EVENT_BUTTON_SECONDARY, GLFW_MOUSE_BUTTON_RIGHT);
    syncButton(AMOTION_EVENT_BUTTON_TERTIARY, GLFW_MOUSE_BUTTON_MIDDLE);
    state.touch.mouseButtonMask = buttons;
}

[[nodiscard]] TouchPointsUVE ReadUnclaimedPointsUVE(const AInputEvent* const event, const TouchGestureUVE& touch,
                                                    const std::int32_t ignorePointerId = -1) {
    TouchPointsUVE points;
    const std::size_t count = AMotionEvent_getPointerCount(event);
    float firstX = 0.0F;
    float firstY = 0.0F;
    float secondX = 0.0F;
    float secondY = 0.0F;
    for (std::size_t i = 0U; i < count; ++i) {
        const std::int32_t id = AMotionEvent_getPointerId(event, i);
        if (id == ignorePointerId || IsPointerClaimedUVE(touch, id)) {
            continue;
        }
        const float px = AMotionEvent_getX(event, i);
        const float py = AMotionEvent_getY(event, i);
        if (points.count == 0) {
            firstX = px;
            firstY = py;
        } else if (points.count == 1) {
            secondX = px;
            secondY = py;
        }
        points.x += px;
        points.y += py;
        ++points.count;
    }
    if (points.count > 0) {
        points.x /= static_cast<float>(points.count);
        points.y /= static_cast<float>(points.count);
    }
    if (points.count >= 2) {
        points.spread = std::hypot(firstX - secondX, firstY - secondY);
    }
    return points;
}

void PublishTouchContactsUVE(const AInputEvent* const event, const TouchGestureUVE& touch,
                             const std::int32_t liftedPointerId = -1, const bool clearAll = false) {
    if (clearAll) {
        Window::SetAndroidGlfwTouchContactsUVE({});
        return;
    }
    const std::size_t count = AMotionEvent_getPointerCount(event);
    std::vector<Window::AndroidTouchContactUVE> contacts;
    contacts.reserve(count);
    for (std::size_t i = 0U; i < count; ++i) {
        const std::int32_t id = AMotionEvent_getPointerId(event, i);
        if (id == liftedPointerId) {
            continue;
        }
        contacts.push_back(Window::AndroidTouchContactUVE{id, AMotionEvent_getX(event, i), AMotionEvent_getY(event, i),
                                                          IsPointerClaimedUVE(touch, id)});
    }
    Window::SetAndroidGlfwTouchContactsUVE(std::move(contacts));
}

void ReleaseGestureButtonUVE(TouchGestureUVE& touch) {
    if (touch.button >= 0) {
        Window::DispatchAndroidGlfwMouseButtonUVE(touch.button, false, touch.lastX, touch.lastY);
        touch.button = -1;
    }
}

/// Cancels a held mouse button without firing a widget click or viewport selection at its press spot.
void CancelGestureButtonUVE(TouchGestureUVE& touch) {
    if (touch.button >= 0) {
        Window::DispatchAndroidGlfwCursorUVE(kCancelOffscreenPosUVE, kCancelOffscreenPosUVE);
        Window::DispatchAndroidGlfwMouseButtonUVE(touch.button, false, kCancelOffscreenPosUVE, kCancelOffscreenPosUVE);
        touch.button = -1;
    }
}

/// Two+ finger gesture: cancels any single-finger left press cleanly and holds Middle (Pan) at the
/// fingers' midpoint while tracking spread for continuous pinch Dolly Zoom.
void StartMultiFingerUVE(TouchGestureUVE& touch, const TouchPointsUVE& points) {
    touch.longPressArmed = false;
    CancelGestureButtonUVE(touch);
    touch.button = kMultiFingerButtonUVE;
    touch.lastX = points.x;
    touch.lastY = points.y;
    touch.pinchDistance = points.spread;
    Window::DispatchAndroidGlfwCursorUVE(points.x, points.y);
    Window::DispatchAndroidGlfwMouseButtonUVE(touch.button, true, points.x, points.y);
}

void HandleTouchUVE(ActivityStateUVE& state, const AInputEvent* const event) {
    if (IsHardwareMouseEventUVE(event)) {
        HandleMouseEventUVE(state, event);
        return;
    }
    state.lastInteractionMs = NowMsUVE();
    if (!state.lastHardwareInputConnected) {
        Window::SetAndroidGlfwHardwareInputUVE(false);
    }
    TouchGestureUVE& touch = state.touch;
    const std::int32_t rawAction = AMotionEvent_getAction(event);
    const std::int32_t action = rawAction & AMOTION_EVENT_ACTION_MASK;
    const std::size_t actionIndex =
        static_cast<std::size_t>((rawAction & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                 AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    const std::size_t pointerCount = AMotionEvent_getPointerCount(event);
    const std::int32_t actionPointerId =
        actionIndex < pointerCount ? AMotionEvent_getPointerId(event, actionIndex) : -1;

    switch (action) {
    case AMOTION_EVENT_ACTION_DOWN: {
        const std::int32_t prevButtons = touch.mouseButtonMask;
        touch = TouchGestureUVE{};
        touch.mouseButtonMask = prevButtons;
        if (actionPointerId >= 0) {
            const float px = AMotionEvent_getX(event, actionIndex);
            const float py = AMotionEvent_getY(event, actionIndex);
            if (Window::AndroidGlfwClaimTouchAtUVE(px, py)) {
                touch.claimedPointerIds.push_back(actionPointerId);
                PublishTouchContactsUVE(event, touch);
                break;
            }
        }
        const TouchPointsUVE points = ReadUnclaimedPointsUVE(event, touch);
        touch.fingers = points.count;
        touch.longPressArmed = true;
        touch.startX = touch.lastX = points.x;
        touch.startY = touch.lastY = points.y;
        touch.startMs = state.lastInteractionMs;
        touch.button = GLFW_MOUSE_BUTTON_LEFT;
        PublishTouchContactsUVE(event, touch);
        Window::DispatchAndroidGlfwCursorUVE(points.x, points.y);
        Window::DispatchAndroidGlfwMouseButtonUVE(GLFW_MOUSE_BUTTON_LEFT, true, points.x, points.y);
        break;
    }
    case AMOTION_EVENT_ACTION_POINTER_DOWN: {
        if (actionPointerId >= 0) {
            const float px = AMotionEvent_getX(event, actionIndex);
            const float py = AMotionEvent_getY(event, actionIndex);
            if (Window::AndroidGlfwClaimTouchAtUVE(px, py)) {
                if (!IsPointerClaimedUVE(touch, actionPointerId)) {
                    touch.claimedPointerIds.push_back(actionPointerId);
                }
                PublishTouchContactsUVE(event, touch);
                break;
            }
        }
        const TouchPointsUVE points = ReadUnclaimedPointsUVE(event, touch);
        touch.fingers = points.count;
        PublishTouchContactsUVE(event, touch);
        if (points.count == 1 && touch.button < 0 && !touch.finished) {
            touch.longPressArmed = true;
            touch.startX = touch.lastX = points.x;
            touch.startY = touch.lastY = points.y;
            touch.startMs = state.lastInteractionMs;
            touch.button = GLFW_MOUSE_BUTTON_LEFT;
            Window::DispatchAndroidGlfwCursorUVE(points.x, points.y);
            Window::DispatchAndroidGlfwMouseButtonUVE(GLFW_MOUSE_BUTTON_LEFT, true, points.x, points.y);
        } else if (points.count >= 2 && !touch.finished && touch.button != kMultiFingerButtonUVE) {
            StartMultiFingerUVE(touch, points);
        } else if (points.count >= 2 && !touch.finished) {
            touch.lastX = points.x;
            touch.lastY = points.y;
            touch.pinchDistance = points.spread;
            Window::DispatchAndroidGlfwCursorUVE(points.x, points.y);
        }
        break;
    }
    case AMOTION_EVENT_ACTION_MOVE: {
        PublishTouchContactsUVE(event, touch);
        if (touch.finished) {
            break;
        }
        const TouchPointsUVE points = ReadUnclaimedPointsUVE(event, touch);
        if (points.count == 1 && touch.fingers == 1) {
            const float slop = kTouchSlopDpUVE * state.pixelsPerDp;
            if (touch.longPressArmed && std::hypot(points.x - touch.startX, points.y - touch.startY) > slop) {
                touch.longPressArmed = false;
            }
            touch.lastX = points.x;
            touch.lastY = points.y;
            Window::DispatchAndroidGlfwCursorUVE(points.x, points.y);
        } else if (points.count >= 2) {
            touch.lastX = points.x;
            touch.lastY = points.y;
            Window::DispatchAndroidGlfwCursorUVE(points.x, points.y);
            const float notch = kPinchDpPerWheelNotchUVE * state.pixelsPerDp;
            if (points.count == 2 && notch > 0.0F) {
                const float delta = (points.spread - touch.pinchDistance) / notch;
                if (std::abs(delta) >= 0.02F) {
                    Window::DispatchAndroidGlfwScrollUVE(delta);
                    touch.pinchDistance = points.spread;
                }
            }
        }
        break;
    }
    case AMOTION_EVENT_ACTION_POINTER_UP: {
        if (actionPointerId >= 0 && IsPointerClaimedUVE(touch, actionPointerId)) {
            EraseClaimedPointerUVE(touch, actionPointerId);
            PublishTouchContactsUVE(event, touch, actionPointerId);
            break;
        }
        const TouchPointsUVE remaining = ReadUnclaimedPointsUVE(event, touch, actionPointerId);
        touch.fingers = remaining.count;
        PublishTouchContactsUVE(event, touch, actionPointerId);
        ReleaseGestureButtonUVE(touch);
        touch.longPressArmed = false;
        if (remaining.count > 0) {
            touch.finished = true;
        }
        break;
    }
    case AMOTION_EVENT_ACTION_UP: {
        const std::int32_t prevButtons = touch.mouseButtonMask;
        ReleaseGestureButtonUVE(touch);
        PublishTouchContactsUVE(event, touch, -1, true);
        touch = TouchGestureUVE{};
        touch.mouseButtonMask = prevButtons;
        break;
    }
    case AMOTION_EVENT_ACTION_CANCEL: {
        const std::int32_t prevButtons = touch.mouseButtonMask;
        touch.longPressArmed = false;
        CancelGestureButtonUVE(touch);
        PublishTouchContactsUVE(event, touch, -1, true);
        touch = TouchGestureUVE{};
        touch.mouseButtonMask = prevButtons;
        break;
    }
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

void DispatchUtf8TextUVE(const std::string_view utf8) {
    std::size_t i = 0U;
    while (i < utf8.size()) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        unsigned int cp = 0U;
        std::size_t len = 1U;
        if (c < 0x80U) {
            cp = c;
        } else if ((c & 0xE0U) == 0xC0U && i + 1U < utf8.size()) {
            cp = ((c & 0x1FU) << 6U) | (static_cast<unsigned char>(utf8[i + 1U]) & 0x3FU);
            len = 2U;
        } else if ((c & 0xF0U) == 0xE0U && i + 2U < utf8.size()) {
            cp = ((c & 0x0FU) << 12U) | ((static_cast<unsigned char>(utf8[i + 1U]) & 0x3FU) << 6U) |
                 (static_cast<unsigned char>(utf8[i + 2U]) & 0x3FU);
            len = 3U;
        } else if ((c & 0xF8U) == 0xF0U && i + 3U < utf8.size()) {
            cp = ((c & 0x07U) << 18U) | ((static_cast<unsigned char>(utf8[i + 1U]) & 0x3FU) << 12U) |
                 ((static_cast<unsigned char>(utf8[i + 2U]) & 0x3FU) << 6U) |
                 (static_cast<unsigned char>(utf8[i + 3U]) & 0x3FU);
            len = 4U;
        }
        i += len;
        if (cp >= kFirstPrintableCharacterUVE && cp != kDeleteCharacterUVE) {
            Window::DispatchAndroidGlfwCharUVE(cp);
        }
    }
}

/// A hardware or soft keyboard key: the GLFW key and, on press, the character it types.
[[nodiscard]] int32_t HandleKeyUVE(ActivityStateUVE& state, const AInputEvent* const event) {
    state.lastInteractionMs = NowMsUVE();
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
    if (glfwKey != GLFW_KEY_UNKNOWN && AInputEvent_getDeviceId(event) > 0 &&
        keyCode != AKEYCODE_ESCAPE && keyCode != AKEYCODE_VOLUME_UP && keyCode != AKEYCODE_VOLUME_DOWN &&
        (AInputEvent_getSource(event) & AINPUT_SOURCE_KEYBOARD) == AINPUT_SOURCE_KEYBOARD) {
        Window::SetAndroidGlfwHardwareInputUVE(true);
    }
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

/// Shows the soft keyboard while the interface wants typed text, hides it after, and drains any
/// committed text/actions sent by Android soft keyboards through UniVexActivity's InputConnection.
void UpdateSoftKeyboardUVE(ActivityStateUVE& state) {
    const bool wanted = Window::AndroidGlfwTextInputWantedUVE();
    if (wanted != state.keyboardShown) {
        state.keyboardShown = wanted;
        state.lastInteractionMs = NowMsUVE();
        if (!wanted) {
            state.imeDrainTailFrames = 12;
        }
        if (!Window::SetAndroidSoftKeyboardUVE(state.app->activity, wanted)) {
            if (wanted) {
                ANativeActivity_showSoftInput(state.app->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
            } else {
                ANativeActivity_hideSoftInput(state.app->activity, 0);
            }
        }
    }
    if (!state.keyboardShown) {
        if (state.imeDrainTailFrames <= 0) {
            return;
        }
        --state.imeDrainTailFrames;
    }
    const std::vector<std::string> events = Window::DrainAndroidImeEventsUVE(state.app->activity);
    if (!events.empty()) {
        state.lastInteractionMs = NowMsUVE();
    }
    for (const std::string& item : events) {
        if (item.size() < 2U || item[1] != ':') {
            continue;
        }
        if (item[0] == 'T') {
            DispatchUtf8TextUVE(std::string_view(item).substr(2U));
        } else if (item[0] == 'K') {
            const int32_t androidKey = std::atoi(item.c_str() + 2);
            const int glfwKey = GlfwKeyUVE(androidKey);
            if (glfwKey != GLFW_KEY_UNKNOWN) {
                Window::DispatchAndroidGlfwKeyUVE(glfwKey, GLFW_PRESS, 0);
                Window::DispatchAndroidGlfwKeyUVE(glfwKey, GLFW_RELEASE, 0);
            }
        }
    }
}

[[nodiscard]] float ReadPixelsPerDpUVE(AConfiguration* const config) {
    if (config == nullptr) {
        return 1.0F;
    }
    const int32_t density = AConfiguration_getDensity(config);
    if (density <= 0 || density == ACONFIGURATION_DENSITY_NONE || density == ACONFIGURATION_DENSITY_ANY) {
        return 1.0F;
    }
    return static_cast<float>(density) / kBaselineDensityUVE;
}

[[nodiscard]] std::string ToLowerAsciiUVE(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

[[nodiscard]] bool HasGzipMagicUVE(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    unsigned char header[2]{0U, 0U};
    if (!file.read(reinterpret_cast<char*>(header), 2)) {
        return false;
    }
    return header[0] == 0x1FU && header[1] == 0x8BU;
}

[[nodiscard]] bool MatchesPickedFileUVE(const std::filesystem::path& path, const std::vector<std::string>& suffixes) {
    if (suffixes.empty()) {
        return true;
    }
    const std::string lowered = ToLowerAsciiUVE(path.filename().string());
    bool expectsUvPackage = false;
    for (const std::string& suffix : suffixes) {
        const std::string wanted = ToLowerAsciiUVE(suffix);
        if (wanted == ".uvpackage") {
            expectsUvPackage = true;
        }
        if (lowered.size() >= wanted.size() &&
            lowered.compare(lowered.size() - wanted.size(), wanted.size(), wanted) == 0) {
            return true;
        }
        // Android browsers frequently append ".bin" or " (1)" to downloaded ".uvpackage" files.
        const std::size_t pos = lowered.find(wanted);
        if (pos != std::string::npos) {
            const std::size_t after = pos + wanted.size();
            if (after == lowered.size() || lowered[after] == '.' || lowered[after] == ' ' || lowered[after] == '(') {
                return true;
            }
        }
    }
    return expectsUvPackage && HasGzipMagicUVE(path);
}

inline constexpr std::string_view kPickErrorPrefixUVE = "error:";
inline constexpr std::string_view kAssetStampFileUVE = ".asset_stamp";
inline constexpr std::uint64_t kFnvOffsetBasisUVE = 14695981039346656037ULL;
inline constexpr std::uint64_t kFnvPrimeUVE = 1099511628211ULL;

void HashBytesUVE(std::uint64_t& hash, const char* const data, const std::size_t length) {
    for (std::size_t index = 0U; index < length; ++index) {
        hash ^= static_cast<unsigned char>(data[index]);
        hash *= kFnvPrimeUVE;
    }
}

/// Hands a finished system file pick to whoever asked for it.
void UpdateFilePickUVE(ActivityStateUVE& state) {
    if (!state.pendingPick) {
        return;
    }
    const std::optional<std::string> picked = Window::TakeAndroidPickedFileUVE(state.app->activity);
    if (!picked.has_value()) {
        return;   // the picker is still open
    }
    const auto onPicked = std::move(state.pendingPick);
    const auto onError = std::move(state.pendingPickError);
    state.pendingPick = nullptr;
    state.pendingPickError = nullptr;
    if (picked->empty()) {
        return;   // cancelled
    }
    if (picked->starts_with(kPickErrorPrefixUVE)) {
        const std::string reason = picked->substr(kPickErrorPrefixUVE.size());
        __android_log_print(ANDROID_LOG_WARN, kLogTagUVE, "UVE_ANDROID_HOST_WARNING file pick failed: %s",
                            reason.c_str());
        if (onError) {
            onError(reason);
        }
        return;
    }
    const std::filesystem::path pickedPath(*picked);
    if (!MatchesPickedFileUVE(pickedPath, state.pickSuffixes)) {
        const std::string filename = pickedPath.filename().string();
        std::error_code removeError;
        std::filesystem::remove(pickedPath, removeError);
        __android_log_print(ANDROID_LOG_WARN, kLogTagUVE,
                            "UVE_ANDROID_HOST_WARNING the picked file is not one that was asked for: %s", picked->c_str());
        if (onError) {
            onError("The chosen file '" + filename + "' is not a UniVex engine package (.uvpackage).");
        }
        return;
    }
    onPicked(pickedPath);
}

/// A finger held still long enough cancels the initial left press cleanly and fires a right click.
void UpdateLongPressUVE(TouchGestureUVE& touch) {
    if (!touch.longPressArmed || touch.fingers != 1 || NowMsUVE() - touch.startMs < kLongPressMsUVE) {
        return;
    }
    touch.longPressArmed = false;
    touch.finished = true;
    CancelGestureButtonUVE(touch);
    Window::DispatchAndroidGlfwCursorUVE(touch.startX, touch.startY);
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

void SyncStorageEnvironmentUVE(ANativeActivity* const activity) {
    if (activity == nullptr) {
        return;
    }
    const Window::AndroidStoragePathsUVE paths = Window::ReadAndroidStoragePathsUVE(activity);
    if (!paths.publicStorageRoot.empty()) {
        setenv("UNIVEX_ANDROID_STORAGE_ROOT", paths.publicStorageRoot.c_str(), 1);
    }
    if (!paths.publicDocuments.empty()) {
        setenv("UNIVEX_ANDROID_PUBLIC_DOCS", paths.publicDocuments.c_str(), 1);
    }
    if (!paths.publicDownloads.empty()) {
        setenv("UNIVEX_ANDROID_DOWNLOADS", paths.publicDownloads.c_str(), 1);
    }
    if (!paths.appExternalDocuments.empty()) {
        setenv("UNIVEX_ANDROID_APP_EXT_DOCS", paths.appExternalDocuments.c_str(), 1);
    }
    setenv("UNIVEX_ANDROID_ALL_FILES", Window::HasAndroidAllFilesAccessUVE(activity) ? "1" : "0", 1);
}

void SyncHardwareInputStateUVE(ActivityStateUVE& state, const bool forceSet) {
    if (state.app == nullptr || state.app->activity == nullptr) {
        return;
    }
    const bool connected = Window::HasAndroidHardwareInputUVE(state.app->activity);
    if (forceSet || connected != state.lastHardwareInputConnected) {
        state.lastHardwareInputConnected = connected;
        Window::SetAndroidGlfwHardwareInputUVE(connected);
    }
}

void HandleCommandUVE(android_app* const app, const int32_t command) {
    auto& state = *static_cast<ActivityStateUVE*>(app->userData);
    state.lastInteractionMs = NowMsUVE();
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
        // Rotation and layout changes move the cutouts to other edges.
        state.safeArea = Window::ReadAndroidSafeAreaUVE(app->activity);
        break;
    case APP_CMD_CONFIG_CHANGED:
        state.pixelsPerDp = ReadPixelsPerDpUVE(app->config);
        Window::SetAndroidGlfwContentScaleUVE(state.pixelsPerDp);
        state.safeArea = Window::ReadAndroidSafeAreaUVE(app->activity);
        SyncHardwareInputStateUVE(state, false);
        break;
    case APP_CMD_RESUME:
        state.resumed = true;
        state.safeArea = Window::ReadAndroidSafeAreaUVE(app->activity);
        SyncHardwareInputStateUVE(state, false);
        SyncStorageEnvironmentUVE(app->activity);
        break;
    case APP_CMD_PAUSE:
        state.resumed = false;
        break;
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_LOST_FOCUS:
        Window::SetAndroidGlfwFocusUVE(command == APP_CMD_GAINED_FOCUS);
        if (command == APP_CMD_GAINED_FOCUS) {
            state.safeArea = Window::ReadAndroidSafeAreaUVE(app->activity);
            SyncHardwareInputStateUVE(state, false);
            SyncStorageEnvironmentUVE(app->activity);
        }
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
    state.pixelsPerDp = ReadPixelsPerDpUVE(app->config);
    state.touch = TouchGestureUVE{};
    state.keyboardShown = false;
    state.imeDrainTailFrames = 0;
    state.lastInteractionMs = NowMsUVE();
    Window::ResetAndroidGlfwWindowUVE();
    Window::SetAndroidGlfwActivityUVE(app->activity);
    Window::SetAndroidGlfwContentScaleUVE(state.pixelsPerDp);
    Window::SetAndroidGlfwSurfaceUVE(app->window);
    state.safeArea = Window::ReadAndroidSafeAreaUVE(app->activity);
    SyncHardwareInputStateUVE(state, true);
    SyncStorageEnvironmentUVE(app->activity);
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

bool PumpEventsUVE(ActivityStateUVE& state, const int idleWaitMs) {
    android_app* const app = state.app;
    int firstPollWaitMs = 0;
    if (idleWaitMs > 0 && state.resumed && app->window != nullptr && state.touch.fingers == 0 &&
        state.touch.claimedPointerIds.empty() && !state.keyboardShown && state.imeDrainTailFrames <= 0 &&
        !state.pendingPick && (NowMsUVE() - state.lastInteractionMs) >= kIdleWakeHoldMsUVE) {
        firstPollWaitMs = idleWaitMs;
    }
    bool firstPoll = true;
    for (;;) {
        const bool active = state.resumed && app->window != nullptr;
        const int timeoutMs = !active ? -1 : (firstPoll ? firstPollWaitMs : 0);
        firstPoll = false;
        android_poll_source* source = nullptr;
        const int result = ALooper_pollOnce(timeoutMs, nullptr, nullptr, reinterpret_cast<void**>(&source));
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

bool BeforeFrameUVE(ActivityStateUVE& state, Core::EngineCoreUVE& core, const char* const renderedMarker,
                    const int idleWaitMs) {
    UpdateLongPressUVE(state.touch);
    UpdateSoftKeyboardUVE(state);
    UpdateFilePickUVE(state);
    state.windowManager = dynamic_cast<Window::AndroidWindowManagerUVE*>(&core.GetServicesUVE().GetWindowManagerUVE());
    if (++state.framesStarted == 2U && renderedMarker != nullptr) {
        __android_log_print(ANDROID_LOG_INFO, kLogTagUVE, "%s", renderedMarker);
    }
    if ((state.framesStarted % 30U) == 0U && state.app != nullptr && state.app->activity != nullptr) {
        SyncHardwareInputStateUVE(state, false);
        state.safeArea = Window::ReadAndroidSafeAreaUVE(state.app->activity);
    }
    const int effectiveIdleMs = state.framesStarted > 4U ? idleWaitMs : 0;
    return PumpEventsUVE(state, effectiveIdleMs) && !Window::AndroidGlfwCloseRequestedUVE();
}

bool StartFilePickUVE(ActivityStateUVE& state, const std::vector<std::string>& suffixes,
                      std::function<void(const std::filesystem::path&)> onPicked,
                      std::function<void(const std::string&)> onError) {
    if (state.app == nullptr || !Window::StartAndroidFilePickUVE(state.app->activity)) {
        return false;
    }
    state.pendingPick = std::move(onPicked);
    state.pendingPickError = std::move(onError);
    state.pickSuffixes = suffixes;
    return true;
}

void DetachEngineUVE(ActivityStateUVE& state) {
    state.windowManager = nullptr;
}

void EnterDataDirectoryUVE(const android_app* const app) {
    const std::filesystem::path data(app->activity->internalDataPath);
    setenv("HOME", data.c_str(), 1);
    SyncStorageEnvironmentUVE(app->activity);
    std::error_code error;
    std::filesystem::current_path(data, error);
    if (error) {
        __android_log_print(ANDROID_LOG_WARN, kLogTagUVE, "UVE_ANDROID_HOST_WARNING the data directory could not be entered: %s",
                            error.message().c_str());
    }
}

bool UnpackAssetsUVE(AAssetManager* const assets, const std::filesystem::path& destination) {
    const std::string indexText = ReadAssetUVE(assets, kAssetIndexUVE);
    if (indexText.empty()) {
        return false;
    }

    std::vector<std::string> entries;
    std::uint64_t hash = kFnvOffsetBasisUVE;
    HashBytesUVE(hash, indexText.data(), indexText.size());
    bool allMatch = true;
    std::istringstream scan(indexText);
    std::string line;
    while (std::getline(scan, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::filesystem::path relative(line);
        if (line.empty() || relative.is_absolute() || line.find("..") != std::string::npos) {
            continue;
        }
        entries.push_back(line);
        AAsset* const asset = AAssetManager_open(assets, line.c_str(), AASSET_MODE_BUFFER);
        if (asset == nullptr) {
            allMatch = false;
            continue;
        }
        const auto length = static_cast<std::size_t>(AAsset_getLength(asset));
        const auto* data = static_cast<const char*>(AAsset_getBuffer(asset));
        HashBytesUVE(hash, line.data(), line.size());
        if (data != nullptr && length > 0U) {
            HashBytesUVE(hash, data, length);
        }
        AAsset_close(asset);

        std::error_code statError;
        const std::filesystem::path target = destination / relative;
        if (!std::filesystem::is_regular_file(target, statError) ||
            std::filesystem::file_size(target, statError) != length || statError) {
            allMatch = false;
        }
    }
    if (entries.empty()) {
        return false;
    }

    const std::string expectedStamp = std::to_string(hash) + ":" + std::to_string(entries.size());
    const std::filesystem::path stampPath = destination / kAssetStampFileUVE;
    if (allMatch) {
        std::ifstream stampIn(stampPath, std::ios::binary);
        std::string currentStamp;
        if (std::getline(stampIn, currentStamp) && currentStamp == expectedStamp) {
            return true;
        }
    }

    std::error_code error;
    std::filesystem::remove_all(destination, error);
    bool any = false;
    for (const std::string& name : entries) {
        const std::filesystem::path target = destination / std::filesystem::path(name);
        std::filesystem::create_directories(target.parent_path(), error);
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        output << ReadAssetUVE(assets, name);
        if (!output) {
            return false;
        }
        any = true;
    }
    if (any) {
        std::ofstream stampOut(stampPath, std::ios::binary | std::ios::trunc);
        stampOut << expectedStamp << '\n';
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
