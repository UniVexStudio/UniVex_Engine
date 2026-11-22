// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// The GLFW API on Android, for the code that is written against GLFW (the editor host and the
// launchers). There is one window: the activity's surface, drawn through the EGL context the
// Android window manager owns. The app feeds it touches; one finger acts as the left mouse button.

#include <GLFW/glfw3.h>

struct ANativeWindow;

namespace UVE::Window {

/// The one GLFW window, standing for the activity's surface.
[[nodiscard]] GLFWwindow* AndroidGlfwWindowUVE();
/// The surface the window draws to (null while the activity has none).
void SetAndroidGlfwSurfaceUVE(ANativeWindow* surface);
void SetAndroidGlfwFocusUVE(bool focused);
/// Screen density relative to the 160 dpi baseline, reported as the window's content scale.
void SetAndroidGlfwContentScaleUVE(float scale);

enum class AndroidPointerPhaseUVE { Down, Move, Up, Cancel };
/// A touch of the primary finger, in surface pixels.
void DispatchAndroidGlfwPointerUVE(AndroidPointerPhaseUVE phase, float x, float y);
/// The pointer moved to (x, y), in surface pixels, with no button change (gestures).
void DispatchAndroidGlfwCursorUVE(float x, float y);
/// A mouse button (GLFW_MOUSE_BUTTON_*) pressed or released at (x, y): what touch gestures stand for.
void DispatchAndroidGlfwMouseButtonUVE(int button, bool down, float x, float y);
/// A scroll of the mouse wheel by `dy` notches (a pinch).
void DispatchAndroidGlfwScrollUVE(float dy);
/// Text the soft keyboard typed (UTF-32 code point).
void DispatchAndroidGlfwCharUVE(unsigned int codepoint);
/// A key from the soft keyboard or a hardware keyboard, as a GLFW key code.
void DispatchAndroidGlfwKeyUVE(int glfwKey, int action, int mods);

/// The interface wants typed text (a text field has focus): the app shows the soft keyboard.
void SetAndroidGlfwTextInputUVE(bool wanted);
[[nodiscard]] bool AndroidGlfwTextInputWantedUVE();

/// True once something called glfwSetWindowShouldClose on the window.
[[nodiscard]] bool AndroidGlfwCloseRequestedUVE();
/// Starts the window afresh for an app taking over the activity (the editor opened a second time
/// from the Hub): no close request, no held buttons, no text input.
void ResetAndroidGlfwWindowUVE();

} // namespace UVE::Window
