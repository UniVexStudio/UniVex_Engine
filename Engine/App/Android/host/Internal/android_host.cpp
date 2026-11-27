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
inline constexpr float kBaselineDensity = 160.0F;
/// Touch gestures (TouchGesture). Distances are density-independent pixels.
inline constexpr float kTouchSlopDp = 6.0F;                 // finger stays inside this to keep long-press armed
inline constexpr std::int64_t kLongPressMs = 450;           // held still this long: a right click
inline constexpr float kPinchDpPerWheelNotch = 36.0F;       // fingers spreading this far: 1.0 wheel unit (continuous)
inline constexpr int kMultiFingerButton = GLFW_MOUSE_BUTTON_MIDDLE;
inline constexpr float kCancelOffscreenPos = -100000.0F;
inline constexpr std::int64_t kIdleWakeHoldMs = 600;        // keep full-rate frames after touch/IME for fling decay

[[nodiscard]] std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Centre of the fingers on the screen, and the spread of the first two.
struct TouchPoints final {
    int count = 0;
    float x = 0.0F;
    float y = 0.0F;
    float spread = 0.0F;
};

[[nodiscard]] bool IsPointerClaimed(const TouchGesture& touch, const std::int32_t id) {
    return std::find(touch.claimedPointerIds.begin(), touch.claimedPointerIds.end(), id) !=
           touch.claimedPointerIds.end();
}

void EraseClaimedPointer(TouchGesture& touch, const std::int32_t id) {
    touch.claimedPointerIds.erase(std::remove(touch.claimedPointerIds.begin(), touch.claimedPointerIds.end(), id),
                                  touch.claimedPointerIds.end());
}

[[nodiscard]] bool IsHardwareMouseEvent(const AInputEvent* const event) {
    const std::int32_t source = AInputEvent_getSource(event);
    if ((source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE) {
        return true;
    }
    return AMotionEvent_getPointerCount(event) > 0 &&
           AMotionEvent_getToolType(event, 0) == AMOTION_EVENT_TOOL_TYPE_MOUSE;
}

[[nodiscard]] int GlfwMods(int32_t metaState);

void HandleMouseEvent(ActivityState& state, const AInputEvent* const event) {
    state.lastInteractionMs = NowMs();
    Window::SetAndroidGlfwHardwareInput(true);
    // Ctrl/Shift/Alt held on a keyboard while clicking: Ctrl+click toggles a selection, and so on.
    Window::SetAndroidGlfwHeldMods(GlfwMods(AMotionEvent_getMetaState(event)));
    const float x = AMotionEvent_getX(event, 0);
    const float y = AMotionEvent_getY(event, 0);
    const std::int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    if (action == AMOTION_EVENT_ACTION_SCROLL) {
        const float vscroll = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
        const float hscroll = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, 0);
        if (std::abs(vscroll) > 1.0e-4F || std::abs(hscroll) > 1.0e-4F) {
            Window::DispatchAndroidGlfwScroll(vscroll, hscroll);
        }
        return;
    }
    Window::DispatchAndroidGlfwCursor(x, y);
    const std::int32_t buttons = AMotionEvent_getButtonState(event);
    const std::int32_t prev = state.touch.mouseButtonMask;
    const auto syncButton = [&](const std::int32_t androidMask, const int glfwButton) {
        const bool wasDown = (prev & androidMask) != 0;
        const bool isDown = (buttons & androidMask) != 0;
        if (wasDown != isDown) {
            Window::DispatchAndroidGlfwMouseButton(glfwButton, isDown, x, y);
        }
    };
    syncButton(AMOTION_EVENT_BUTTON_PRIMARY, GLFW_MOUSE_BUTTON_LEFT);
    syncButton(AMOTION_EVENT_BUTTON_SECONDARY, GLFW_MOUSE_BUTTON_RIGHT);
    syncButton(AMOTION_EVENT_BUTTON_TERTIARY, GLFW_MOUSE_BUTTON_MIDDLE);
    state.touch.mouseButtonMask = buttons;
}

[[nodiscard]] TouchPoints ReadUnclaimedPoints(const AInputEvent* const event, const TouchGesture& touch,
                                                    const std::int32_t ignorePointerId = -1) {
    TouchPoints points;
    const std::size_t count = AMotionEvent_getPointerCount(event);
    float firstX = 0.0F;
    float firstY = 0.0F;
    float secondX = 0.0F;
    float secondY = 0.0F;
    for (std::size_t i = 0U; i < count; ++i) {
        const std::int32_t id = AMotionEvent_getPointerId(event, i);
        if (id == ignorePointerId || IsPointerClaimed(touch, id)) {
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

void PublishTouchContacts(const AInputEvent* const event, const TouchGesture& touch,
                             const std::int32_t liftedPointerId = -1, const bool clearAll = false) {
    if (clearAll) {
        Window::SetAndroidGlfwTouchContacts({});
        return;
    }
    const std::size_t count = AMotionEvent_getPointerCount(event);
    std::vector<Window::AndroidTouchContact> contacts;
    contacts.reserve(count);
    for (std::size_t i = 0U; i < count; ++i) {
        const std::int32_t id = AMotionEvent_getPointerId(event, i);
        if (id == liftedPointerId) {
            continue;
        }
        contacts.push_back(Window::AndroidTouchContact{id, AMotionEvent_getX(event, i), AMotionEvent_getY(event, i),
                                                          IsPointerClaimed(touch, id)});
    }
    Window::SetAndroidGlfwTouchContacts(std::move(contacts));
}

void ReleaseGestureButton(TouchGesture& touch) {
    if (touch.button >= 0) {
        Window::DispatchAndroidGlfwMouseButton(touch.button, false, touch.lastX, touch.lastY);
        touch.button = -1;
    }
}

/// Cancels a held mouse button without firing a widget click or viewport selection at its press spot,
/// and without sending a bogus offscreen cursor move that would slam a captured Splitter or Slider.
void CancelGestureButton(TouchGesture& touch) {
    if (touch.button >= 0) {
        Window::DispatchAndroidGlfwMouseButtonRelease(touch.button);
        touch.button = -1;
    }
}

/// Two+ finger gesture: cancels any single-finger left press cleanly and holds Middle (Pan) at the
/// fingers' midpoint while tracking spread for continuous pinch Dolly Zoom.
void StartMultiFinger(TouchGesture& touch, const TouchPoints& points) {
    touch.longPressArmed = false;
    CancelGestureButton(touch);
    touch.button = kMultiFingerButton;
    touch.lastX = points.x;
    touch.lastY = points.y;
    touch.pinchDistance = points.spread;
    Window::DispatchAndroidGlfwCursor(points.x, points.y);
    Window::DispatchAndroidGlfwMouseButton(touch.button, true, points.x, points.y);
}

void HandleTouch(ActivityState& state, const AInputEvent* const event) {
    if (IsHardwareMouseEvent(event)) {
        HandleMouseEvent(state, event);
        return;
    }
    state.lastInteractionMs = NowMs();
    if (!state.lastHardwareInputConnected) {
        Window::SetAndroidGlfwHardwareInput(false);
    }
    TouchGesture& touch = state.touch;
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
        touch = TouchGesture{};
        touch.mouseButtonMask = prevButtons;
        if (actionPointerId >= 0) {
            const float px = AMotionEvent_getX(event, actionIndex);
            const float py = AMotionEvent_getY(event, actionIndex);
            if (Window::AndroidGlfwClaimTouchAt(px, py)) {
                touch.claimedPointerIds.push_back(actionPointerId);
                PublishTouchContacts(event, touch);
                break;
            }
        }
        const TouchPoints points = ReadUnclaimedPoints(event, touch);
        touch.fingers = points.count;
        touch.longPressArmed = true;
        touch.startX = touch.lastX = points.x;
        touch.startY = touch.lastY = points.y;
        touch.startMs = state.lastInteractionMs;
        touch.button = GLFW_MOUSE_BUTTON_LEFT;
        PublishTouchContacts(event, touch);
        Window::DispatchAndroidGlfwCursor(points.x, points.y);
        Window::DispatchAndroidGlfwMouseButton(GLFW_MOUSE_BUTTON_LEFT, true, points.x, points.y);
        break;
    }
    case AMOTION_EVENT_ACTION_POINTER_DOWN: {
        if (actionPointerId >= 0) {
            const float px = AMotionEvent_getX(event, actionIndex);
            const float py = AMotionEvent_getY(event, actionIndex);
            if (Window::AndroidGlfwClaimTouchAt(px, py)) {
                if (!IsPointerClaimed(touch, actionPointerId)) {
                    touch.claimedPointerIds.push_back(actionPointerId);
                }
                PublishTouchContacts(event, touch);
                break;
            }
        }
        const TouchPoints points = ReadUnclaimedPoints(event, touch);
        touch.fingers = points.count;
        PublishTouchContacts(event, touch);
        if (points.count == 1 && touch.button < 0 && !touch.finished) {
            touch.longPressArmed = true;
            touch.startX = touch.lastX = points.x;
            touch.startY = touch.lastY = points.y;
            touch.startMs = state.lastInteractionMs;
            touch.button = GLFW_MOUSE_BUTTON_LEFT;
            Window::DispatchAndroidGlfwCursor(points.x, points.y);
            Window::DispatchAndroidGlfwMouseButton(GLFW_MOUSE_BUTTON_LEFT, true, points.x, points.y);
        } else if (points.count >= 2 && !touch.finished && touch.button != kMultiFingerButton) {
            StartMultiFinger(touch, points);
        } else if (points.count >= 2 && !touch.finished) {
            touch.lastX = points.x;
            touch.lastY = points.y;
            touch.pinchDistance = points.spread;
            Window::DispatchAndroidGlfwCursor(points.x, points.y);
        }
        break;
    }
    case AMOTION_EVENT_ACTION_MOVE: {
        PublishTouchContacts(event, touch);
        if (touch.finished) {
            break;
        }
        const TouchPoints points = ReadUnclaimedPoints(event, touch);
        if (points.count == 1 && touch.fingers == 1) {
            const float slop = kTouchSlopDp * state.pixelsPerDp;
            if (touch.longPressArmed && std::hypot(points.x - touch.startX, points.y - touch.startY) > slop) {
                touch.longPressArmed = false;
            }
            touch.lastX = points.x;
            touch.lastY = points.y;
            Window::DispatchAndroidGlfwCursor(points.x, points.y);
        } else if (points.count >= 2) {
            touch.lastX = points.x;
            touch.lastY = points.y;
            Window::DispatchAndroidGlfwCursor(points.x, points.y);
            const float notch = kPinchDpPerWheelNotch * state.pixelsPerDp;
            if (points.count == 2 && notch > 0.0F) {
                const float delta = (points.spread - touch.pinchDistance) / notch;
                if (std::abs(delta) >= 0.02F) {
                    Window::DispatchAndroidGlfwScroll(delta);
                    touch.pinchDistance = points.spread;
                }
            }
        }
        break;
    }
    case AMOTION_EVENT_ACTION_POINTER_UP: {
        if (actionPointerId >= 0 && IsPointerClaimed(touch, actionPointerId)) {
            EraseClaimedPointer(touch, actionPointerId);
            PublishTouchContacts(event, touch, actionPointerId);
            break;
        }
        const TouchPoints remaining = ReadUnclaimedPoints(event, touch, actionPointerId);
        touch.fingers = remaining.count;
        PublishTouchContacts(event, touch, actionPointerId);
        ReleaseGestureButton(touch);
        touch.longPressArmed = false;
        if (remaining.count > 0) {
            touch.finished = true;
        }
        break;
    }
    case AMOTION_EVENT_ACTION_UP: {
        const std::int32_t prevButtons = touch.mouseButtonMask;
        ReleaseGestureButton(touch);
        PublishTouchContacts(event, touch, -1, true);
        touch = TouchGesture{};
        touch.mouseButtonMask = prevButtons;
        break;
    }
    case AMOTION_EVENT_ACTION_CANCEL: {
        const std::int32_t prevButtons = touch.mouseButtonMask;
        touch.longPressArmed = false;
        CancelGestureButton(touch);
        PublishTouchContacts(event, touch, -1, true);
        touch = TouchGesture{};
        touch.mouseButtonMask = prevButtons;
        break;
    }
    default:
        break;
    }
}

/// Android key codes and the GLFW keys the interface reads for them.
struct KeyMapping final {
    int32_t android;
    int glfw;
};
inline constexpr std::array<KeyMapping, 21> kNamedKeys{{
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
inline constexpr unsigned int kFirstPrintableCharacter = 0x20U;
inline constexpr unsigned int kDeleteCharacter = 0x7FU;

[[nodiscard]] int GlfwKey(const int32_t keyCode) {
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        return GLFW_KEY_A + (keyCode - AKEYCODE_A);
    }
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        return GLFW_KEY_0 + (keyCode - AKEYCODE_0);
    }
    // A hardware keyboard's F-keys (F5 Play, F2 Rename, ...) and numpad (the view hotkeys).
    if (keyCode >= AKEYCODE_F1 && keyCode <= AKEYCODE_F12) {
        return GLFW_KEY_F1 + (keyCode - AKEYCODE_F1);
    }
    if (keyCode >= AKEYCODE_NUMPAD_0 && keyCode <= AKEYCODE_NUMPAD_9) {
        return GLFW_KEY_KP_0 + (keyCode - AKEYCODE_NUMPAD_0);
    }
    if (keyCode == AKEYCODE_INSERT) {
        return GLFW_KEY_INSERT;
    }
    for (const KeyMapping& key : kNamedKeys) {
        if (key.android == keyCode) {
            return key.glfw;
        }
    }
    return GLFW_KEY_UNKNOWN;
}

[[nodiscard]] int GlfwMods(const int32_t metaState) {
    int mods = 0;
    mods |= (metaState & AMETA_SHIFT_ON) != 0 ? GLFW_MOD_SHIFT : 0;
    mods |= (metaState & AMETA_CTRL_ON) != 0 ? GLFW_MOD_CONTROL : 0;
    mods |= (metaState & AMETA_ALT_ON) != 0 ? GLFW_MOD_ALT : 0;
    return mods;
}

void DispatchUtf8Text(const std::string_view utf8) {
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
        if (cp >= kFirstPrintableCharacter && cp != kDeleteCharacter) {
            Window::DispatchAndroidGlfwChar(cp);
        }
    }
}

/// A hardware or soft keyboard key: the GLFW key and, on press, the character it types.
[[nodiscard]] int32_t HandleKey(ActivityState& state, const AInputEvent* const event) {
    state.lastInteractionMs = NowMs();
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
    const int glfwKey = GlfwKey(keyCode);
    if (glfwKey != GLFW_KEY_UNKNOWN && AInputEvent_getDeviceId(event) > 0 &&
        keyCode != AKEYCODE_ESCAPE && keyCode != AKEYCODE_VOLUME_UP && keyCode != AKEYCODE_VOLUME_DOWN &&
        (AInputEvent_getSource(event) & AINPUT_SOURCE_KEYBOARD) == AINPUT_SOURCE_KEYBOARD) {
        Window::SetAndroidGlfwHardwareInput(true);
    }
    const int32_t metaState = AKeyEvent_getMetaState(event);
    Window::SetAndroidGlfwHeldMods(GlfwMods(metaState));   // so a click right after sees Ctrl/Shift held
    if (glfwKey != GLFW_KEY_UNKNOWN) {
        const int glfwAction = action == AKEY_EVENT_ACTION_UP ? GLFW_RELEASE
                               : AKeyEvent_getRepeatCount(event) > 0 ? GLFW_REPEAT
                                                                      : GLFW_PRESS;
        Window::DispatchAndroidGlfwKey(glfwKey, glfwAction, GlfwMods(metaState));
    }
    if (action == AKEY_EVENT_ACTION_DOWN && keyCode != AKEYCODE_ESCAPE) {
        const unsigned int character = Window::AndroidKeyCharacter(state.app->activity, action, keyCode, metaState);
        if (character >= kFirstPrintableCharacter && character != kDeleteCharacter) {
            Window::DispatchAndroidGlfwChar(character);
        }
    }
    return glfwKey != GLFW_KEY_UNKNOWN ? 1 : 0;
}

/// Shows the soft keyboard while the interface wants typed text, hides it after, and drains any
/// committed text/actions sent by Android soft keyboards through UniVexActivity's InputConnection.
void UpdateSoftKeyboard(ActivityState& state) {
    const bool wanted = Window::AndroidGlfwTextInputWanted();
    if (wanted != state.keyboardShown) {
        state.keyboardShown = wanted;
        state.lastInteractionMs = NowMs();
        if (!wanted) {
            state.imeDrainTailFrames = 12;
        }
        if (!Window::SetAndroidSoftKeyboard(state.app->activity, wanted)) {
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
    const std::vector<std::string> events = Window::DrainAndroidImeEvents(state.app->activity);
    if (!events.empty()) {
        state.lastInteractionMs = NowMs();
    }
    for (const std::string& item : events) {
        if (item.size() < 2U || item[1] != ':') {
            continue;
        }
        if (item[0] == 'T') {
            DispatchUtf8Text(std::string_view(item).substr(2U));
        } else if (item[0] == 'K') {
            const int32_t androidKey = std::atoi(item.c_str() + 2);
            const int glfwKey = GlfwKey(androidKey);
            if (glfwKey != GLFW_KEY_UNKNOWN) {
                Window::DispatchAndroidGlfwKey(glfwKey, GLFW_PRESS, 0);
                Window::DispatchAndroidGlfwKey(glfwKey, GLFW_RELEASE, 0);
            }
        }
    }
}

[[nodiscard]] float ReadPixelsPerDp(AConfiguration* const config) {
    if (config == nullptr) {
        return 1.0F;
    }
    const int32_t density = AConfiguration_getDensity(config);
    if (density <= 0 || density == ACONFIGURATION_DENSITY_NONE || density == ACONFIGURATION_DENSITY_ANY) {
        return 1.0F;
    }
    return static_cast<float>(density) / kBaselineDensity;
}

[[nodiscard]] std::string ToLowerAscii(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

[[nodiscard]] bool HasGzipMagic(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    unsigned char header[2]{0U, 0U};
    if (!file.read(reinterpret_cast<char*>(header), 2)) {
        return false;
    }
    return header[0] == 0x1FU && header[1] == 0x8BU;
}

[[nodiscard]] bool MatchesPickedFile(const std::filesystem::path& path, const std::vector<std::string>& suffixes) {
    if (suffixes.empty()) {
        return true;
    }
    const std::string lowered = ToLowerAscii(path.filename().string());
    bool expectsUvPackage = false;
    for (const std::string& suffix : suffixes) {
        const std::string wanted = ToLowerAscii(suffix);
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
    return expectsUvPackage && HasGzipMagic(path);
}

inline constexpr std::string_view kPickErrorPrefix = "error:";
inline constexpr std::string_view kAssetStampFile = ".asset_stamp";
inline constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void HashBytes(std::uint64_t& hash, const char* const data, const std::size_t length) {
    for (std::size_t index = 0U; index < length; ++index) {
        hash ^= static_cast<unsigned char>(data[index]);
        hash *= kFnvPrime;
    }
}

/// Hands a finished system file pick to whoever asked for it.
void UpdateFilePick(ActivityState& state) {
    if (!state.pendingPick) {
        return;
    }
    const std::optional<std::string> picked = Window::TakeAndroidPickedFile(state.app->activity);
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
    if (picked->starts_with(kPickErrorPrefix)) {
        const std::string reason = picked->substr(kPickErrorPrefix.size());
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "UVE_ANDROID_HOST_WARNING file pick failed: %s",
                            reason.c_str());
        if (onError) {
            onError(reason);
        }
        return;
    }
    const std::filesystem::path pickedPath(*picked);
    if (!MatchesPickedFile(pickedPath, state.pickSuffixes)) {
        const std::string filename = pickedPath.filename().string();
        std::error_code removeError;
        std::filesystem::remove(pickedPath, removeError);
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "UVE_ANDROID_HOST_WARNING the picked file is not one that was asked for: %s", picked->c_str());
        if (onError) {
            onError("The chosen file '" + filename + "' is not a UniVex engine package (.uvpackage).");
        }
        return;
    }
    onPicked(pickedPath);
}

/// Hands a finished multi-file or folder SAF pick to whoever asked for it.
void UpdateMultiPick(ActivityState& state) {
    if (!state.pendingMultiPick) {
        return;
    }
    const std::optional<std::string> picked = Window::TakeAndroidPickedPaths(state.app->activity);
    if (!picked.has_value()) {
        return; // still running
    }
    const auto onPicked = std::move(state.pendingMultiPick);
    const auto onError = std::move(state.pendingMultiPickError);
    const std::vector<std::string> suffixes = std::move(state.multiPickSuffixes);
    const bool pickingFolders = state.multiPickFolders;
    state.pendingMultiPick = nullptr;
    state.pendingMultiPickError = nullptr;
    state.multiPickSuffixes.clear();
    state.multiPickFolders = false;
    if (picked->empty()) {
        return; // cancelled
    }
    if (picked->starts_with(kPickErrorPrefix)) {
        const std::string reason = picked->substr(kPickErrorPrefix.size());
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "UVE_ANDROID_HOST_WARNING multi pick failed: %s",
                            reason.c_str());
        if (onError) {
            onError(reason);
        }
        return;
    }
    std::vector<std::filesystem::path> paths;
    std::size_t start = 0U;
    while (start <= picked->size()) {
        const std::size_t sep = picked->find('\x1E', start);
        const std::string piece =
            sep == std::string::npos ? picked->substr(start) : picked->substr(start, sep - start);
        if (!piece.empty()) {
            const std::filesystem::path p(piece);
            if (pickingFolders || suffixes.empty() || MatchesPickedFile(p, suffixes)) {
                paths.push_back(p);
            } else {
                std::error_code rmErr;
                std::filesystem::remove(p, rmErr);
            }
        }
        if (sep == std::string::npos) {
            break;
        }
        start = sep + 1U;
    }
    if (paths.empty()) {
        if (onError) {
            onError("None of the selected files matched supported asset types.");
        }
        return;
    }
    onPicked(paths);
}

/// A finger held still long enough cancels the initial left press cleanly and fires a right click.
void UpdateLongPress(TouchGesture& touch) {
    if (!touch.longPressArmed || touch.fingers != 1 || NowMs() - touch.startMs < kLongPressMs) {
        return;
    }
    touch.longPressArmed = false;
    touch.finished = true;
    CancelGestureButton(touch);
    Window::DispatchAndroidGlfwCursor(touch.startX, touch.startY);
    Window::DispatchAndroidGlfwMouseButton(GLFW_MOUSE_BUTTON_RIGHT, true, touch.startX, touch.startY);
    Window::DispatchAndroidGlfwMouseButton(GLFW_MOUSE_BUTTON_RIGHT, false, touch.startX, touch.startY);
}

[[nodiscard]] std::string ReadAsset(AAssetManager* const assets, const std::string& name) {
    AAsset* const asset = AAssetManager_open(assets, name.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        return {};
    }
    const auto* data = static_cast<const char*>(AAsset_getBuffer(asset));
    std::string text = data != nullptr ? std::string(data, static_cast<std::size_t>(AAsset_getLength(asset))) : "";
    AAsset_close(asset);
    return text;
}

void SyncStorageEnvironment(ANativeActivity* const activity) {
    if (activity == nullptr) {
        return;
    }
    const Window::AndroidStoragePaths paths = Window::ReadAndroidStoragePaths(activity);
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
    setenv("UNIVEX_ANDROID_ALL_FILES", Window::HasAndroidAllFilesAccess(activity) ? "1" : "0", 1);
}

void SyncHardwareInputState(ActivityState& state, const bool forceSet) {
    if (state.app == nullptr || state.app->activity == nullptr) {
        return;
    }
    const bool connected = Window::HasAndroidHardwareInput(state.app->activity);
    if (forceSet || connected != state.lastHardwareInputConnected) {
        state.lastHardwareInputConnected = connected;
        Window::SetAndroidGlfwHardwareInput(connected);
    }
}

void CancelActiveTouches(ActivityState& state) {
    CancelGestureButton(state.touch);
    const std::int32_t heldMouse = state.touch.mouseButtonMask;
    if ((heldMouse & AMOTION_EVENT_BUTTON_PRIMARY) != 0) {
        Window::DispatchAndroidGlfwMouseButtonRelease(GLFW_MOUSE_BUTTON_LEFT);
    }
    if ((heldMouse & AMOTION_EVENT_BUTTON_SECONDARY) != 0) {
        Window::DispatchAndroidGlfwMouseButtonRelease(GLFW_MOUSE_BUTTON_RIGHT);
    }
    if ((heldMouse & AMOTION_EVENT_BUTTON_TERTIARY) != 0) {
        Window::DispatchAndroidGlfwMouseButtonRelease(GLFW_MOUSE_BUTTON_MIDDLE);
    }
    state.touch = TouchGesture{};
    Window::SetAndroidGlfwTouchContacts({});
}

void HandleCommand(android_app* const app, const int32_t command) {
    auto& state = *static_cast<ActivityState*>(app->userData);
    state.lastInteractionMs = NowMs();
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        state.safeArea = Window::ReadAndroidSafeArea(app->activity);
        Window::SetAndroidGlfwSurface(app->window);
        if (state.windowManager != nullptr && !state.windowManager->AttachNativeWindow(app->window)) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "UVE_ANDROID_HOST_FAILED the surface could not be reattached");
            ANativeActivity_finish(app->activity);
        }
        break;
    case APP_CMD_TERM_WINDOW:
        CancelActiveTouches(state);
        // The surface is destroyed once this returns, so stop drawing to it now.
        if (state.windowManager != nullptr) {
            state.windowManager->DetachNativeWindow();
        }
        Window::SetAndroidGlfwSurface(nullptr);
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
        // Rotation and layout changes move the cutouts to other edges.
        state.safeArea = Window::ReadAndroidSafeArea(app->activity);
        break;
    case APP_CMD_CONFIG_CHANGED:
        state.pixelsPerDp = ReadPixelsPerDp(app->config);
        Window::SetAndroidGlfwContentScale(state.pixelsPerDp);
        state.safeArea = Window::ReadAndroidSafeArea(app->activity);
        SyncHardwareInputState(state, false);
        break;
    case APP_CMD_RESUME:
        state.resumed = true;
        state.safeArea = Window::ReadAndroidSafeArea(app->activity);
        SyncHardwareInputState(state, false);
        SyncStorageEnvironment(app->activity);
        break;
    case APP_CMD_PAUSE:
        state.resumed = false;
        CancelActiveTouches(state);
        break;
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_LOST_FOCUS:
        Window::SetAndroidGlfwFocus(command == APP_CMD_GAINED_FOCUS);
        if (command == APP_CMD_GAINED_FOCUS) {
            state.safeArea = Window::ReadAndroidSafeArea(app->activity);
            SyncHardwareInputState(state, false);
            SyncStorageEnvironment(app->activity);
        } else {
            CancelActiveTouches(state);
        }
        if (state.windowManager != nullptr) {
            state.windowManager->SetFocused(command == APP_CMD_GAINED_FOCUS);
        }
        break;
    default:
        break;
    }
}

int32_t HandleInput(android_app* const app, AInputEvent* const event) {
    auto& state = *static_cast<ActivityState*>(app->userData);
    switch (AInputEvent_getType(event)) {
    case AINPUT_EVENT_TYPE_MOTION:
        HandleTouch(state, event);
        return 1;
    case AINPUT_EVENT_TYPE_KEY:
        return HandleKey(state, event);
    default:
        return 0;
    }
}

} // namespace

void Attach(ActivityState& state, android_app* const app) {
    state.app = app;
    state.windowManager = nullptr;
    state.framesStarted = 0;
    // Attached while the activity is already in the foreground (the editor taking over from the Hub).
    state.resumed = state.resumed || app->window != nullptr;
    app->userData = &state;
    app->onAppCmd = HandleCommand;
    app->onInputEvent = HandleInput;
    Online::SetAndroidJavaVm(app->activity->vm);
    state.pixelsPerDp = ReadPixelsPerDp(app->config);
    state.touch = TouchGesture{};
    state.keyboardShown = false;
    state.imeDrainTailFrames = 0;
    state.lastInteractionMs = NowMs();
    Window::ResetAndroidGlfwWindow();
    Window::SetAndroidGlfwActivity(app->activity);
    Window::SetAndroidGlfwContentScale(state.pixelsPerDp);
    Window::SetAndroidGlfwSurface(app->window);
    state.safeArea = Window::ReadAndroidSafeArea(app->activity);
    SyncHardwareInputState(state, true);
    SyncStorageEnvironment(app->activity);
}

bool WaitForWindow(ActivityState& state) {
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

bool PumpEvents(ActivityState& state, const int idleWaitMs) {
    android_app* const app = state.app;
    int firstPollWaitMs = 0;
    if (idleWaitMs > 0 && state.resumed && app->window != nullptr && state.touch.fingers == 0 &&
        state.touch.claimedPointerIds.empty() && !state.keyboardShown && state.imeDrainTailFrames <= 0 &&
        !state.pendingPick && !state.pendingMultiPick && (NowMs() - state.lastInteractionMs) >= kIdleWakeHoldMs) {
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

bool BeforeFrame(ActivityState& state, Core::EngineCore& core, const char* const renderedMarker,
                    const int idleWaitMs) {
    UpdateLongPress(state.touch);
    UpdateSoftKeyboard(state);
    UpdateFilePick(state);
    UpdateMultiPick(state);
    state.windowManager = dynamic_cast<Window::AndroidWindowManager*>(&core.GetServices().GetWindowManager());
    if (++state.framesStarted == 2U && renderedMarker != nullptr) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", renderedMarker);
    }
    if ((state.framesStarted % 30U) == 0U && state.app != nullptr && state.app->activity != nullptr) {
        SyncHardwareInputState(state, false);
        state.safeArea = Window::ReadAndroidSafeArea(state.app->activity);
    }
    const int effectiveIdleMs = state.framesStarted > 4U ? idleWaitMs : 0;
    return PumpEvents(state, effectiveIdleMs) && !Window::AndroidGlfwCloseRequested();
}

bool StartFilePick(ActivityState& state, const std::vector<std::string>& suffixes,
                      std::function<void(const std::filesystem::path&)> onPicked,
                      std::function<void(const std::string&)> onError) {
    if (state.app == nullptr || !Window::StartAndroidFilePick(state.app->activity)) {
        return false;
    }
    state.pendingPick = std::move(onPicked);
    state.pendingPickError = std::move(onError);
    state.pickSuffixes = suffixes;
    return true;
}

bool StartMultiFilePick(ActivityState& state, const std::vector<std::string>& suffixes,
                           std::function<void(const std::vector<std::filesystem::path>&)> onPicked,
                           std::function<void(const std::string&)> onError) {
    if (state.app == nullptr || !Window::StartAndroidMultiFilePick(state.app->activity)) {
        return false;
    }
    state.pendingMultiPick = std::move(onPicked);
    state.pendingMultiPickError = std::move(onError);
    state.multiPickSuffixes = suffixes;
    state.multiPickFolders = false;
    return true;
}

bool StartFolderPick(ActivityState& state,
                        std::function<void(const std::vector<std::filesystem::path>&)> onPicked,
                        std::function<void(const std::string&)> onError) {
    if (state.app == nullptr || !Window::StartAndroidFolderPick(state.app->activity)) {
        return false;
    }
    state.pendingMultiPick = std::move(onPicked);
    state.pendingMultiPickError = std::move(onError);
    state.multiPickSuffixes.clear();
    state.multiPickFolders = true;
    return true;
}

void DetachEngine(ActivityState& state) {
    state.windowManager = nullptr;
}

void EnterDataDirectory(const android_app* const app) {
    const std::filesystem::path data(app->activity->internalDataPath);
    setenv("HOME", data.c_str(), 1);
    SyncStorageEnvironment(app->activity);
    std::error_code error;
    std::filesystem::current_path(data, error);
    if (error) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "UVE_ANDROID_HOST_WARNING the data directory could not be entered: %s",
                            error.message().c_str());
    }
}

bool UnpackAssets(AAssetManager* const assets, const std::filesystem::path& destination) {
    const std::string indexText = ReadAsset(assets, kAssetIndex);
    if (indexText.empty()) {
        return false;
    }

    std::vector<std::string> entries;
    std::uint64_t hash = kFnvOffsetBasis;
    HashBytes(hash, indexText.data(), indexText.size());
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
        HashBytes(hash, line.data(), line.size());
        if (data != nullptr && length > 0U) {
            HashBytes(hash, data, length);
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
    const std::filesystem::path stampPath = destination / kAssetStampFile;
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
        output << ReadAsset(assets, name);
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

void DrainUntilDestroyed(android_app* const app) {
    while (app->destroyRequested == 0) {
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0 && source != nullptr) {
            source->process(app, source);
        }
    }
}

} // namespace UVE::AndroidHost
