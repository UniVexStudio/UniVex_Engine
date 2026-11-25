// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// The GLFW API on Android, for the code that is written against GLFW (the editor host and the
// launchers). There is one window: the activity's surface, drawn through the EGL context the
// Android window manager owns. The app feeds it touches; one finger acts as the left mouse button.

#include <GLFW/glfw3.h>

#include <cstdint>
#include <functional>
#include <vector>

#include "android_activity.h"

struct ANativeActivity;
struct ANativeWindow;

namespace UVE::Window {

/// One live finger contact on the Android surface, in physical surface pixels.
struct AndroidTouchContact final {
    std::int32_t id = -1;
    float x = 0.0F;
    float y = 0.0F;
    bool claimed = false;
};

/// The one GLFW window, standing for the activity's surface.
[[nodiscard]] GLFWwindow* AndroidGlfwWindow();
/// Binds the active Android activity so GLFW clipboard calls talk to Android's ClipboardManager.
void SetAndroidGlfwActivity(ANativeActivity* activity);
/// The surface the window draws to (null while the activity has none).
void SetAndroidGlfwSurface(ANativeWindow* surface);
void SetAndroidGlfwFocus(bool focused);
/// Screen density relative to the 160 dpi baseline, reported as the window's content scale.
void SetAndroidGlfwContentScale(float scale);

enum class AndroidPointerPhase { Down, Move, Up, Cancel };
/// A touch of the primary finger, in surface pixels.
void DispatchAndroidGlfwPointer(AndroidPointerPhase phase, float x, float y);
/// The pointer moved to (x, y), in surface pixels, with no button change (gestures).
void DispatchAndroidGlfwCursor(float x, float y);
/// A mouse button (GLFW_MOUSE_BUTTON_*) pressed or released at (x, y): what touch gestures stand for.
void DispatchAndroidGlfwMouseButton(int button, bool down, float x, float y);
/// A scroll of the mouse wheel by `dy` notches (a pinch), and `dx` sideways (a mouse's horizontal wheel).
void DispatchAndroidGlfwScroll(float dy, float dx = 0.0F);
/// Text the soft keyboard typed (UTF-32 code point).
void DispatchAndroidGlfwChar(unsigned int codepoint);
/// A key from the soft keyboard or a hardware keyboard, as a GLFW key code.
void DispatchAndroidGlfwKey(int glfwKey, int action, int mods);

/// Modifiers (GLFW_MOD_*) every key and mouse-button event carries: the ones held on a hardware
/// keyboard, set from each input event's meta state, OR the sticky ones latched by the on-screen
/// Controls panel (Ctrl/Shift/Alt for touch).
void SetAndroidGlfwHeldMods(int glfwMods);
void SetAndroidGlfwStickyMods(int glfwMods);
[[nodiscard]] int AndroidGlfwStickyMods();

/// True while a hardware mouse or external keyboard is connected or actively used on Android.
void SetAndroidGlfwHardwareInput(bool active);
[[nodiscard]] bool AndroidGlfwHasHardwareInput();

/// Multi-touch contact stream for the on-screen Android viewport touch controller.
void SetAndroidGlfwTouchContacts(std::vector<AndroidTouchContact> contacts);
[[nodiscard]] std::vector<AndroidTouchContact> AndroidGlfwTouchContacts();
/// Registers a hit-test callback (in physical surface pixels) that returns true when a finger
/// landing at (x, y) belongs to the on-screen viewport touch controller.
void SetAndroidGlfwTouchClaimHook(std::function<bool(float x, float y)> hook);
[[nodiscard]] bool AndroidGlfwClaimTouchAt(float x, float y);

/// Storage permission and public external directory queries routed through the bound activity.
[[nodiscard]] bool AndroidGlfwHasAllFilesAccess();
void RequestAndroidGlfwAllFilesAccess();
[[nodiscard]] AndroidStoragePaths ReadAndroidGlfwStoragePaths();
[[nodiscard]] AndroidSafeArea ReadAndroidGlfwSafeArea();

/// The interface wants typed text (a text field has focus): the app shows the soft keyboard.
void SetAndroidGlfwTextInput(bool wanted);
[[nodiscard]] bool AndroidGlfwTextInputWanted();

/// True once something called glfwSetWindowShouldClose on the window.
[[nodiscard]] bool AndroidGlfwCloseRequested();
/// Starts the window afresh for an app taking over the activity (the editor opened a second time
/// from the Hub): no close request, no held buttons, no text input.
void ResetAndroidGlfwWindow();

} // namespace UVE::Window
