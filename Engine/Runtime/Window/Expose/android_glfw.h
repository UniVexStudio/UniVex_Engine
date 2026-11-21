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
/// Text the soft keyboard typed (UTF-32 code point).
void DispatchAndroidGlfwCharUVE(unsigned int codepoint);
/// A key from the soft keyboard or a hardware keyboard, as a GLFW key code.
void DispatchAndroidGlfwKeyUVE(int glfwKey, int action, int mods);

/// True once something called glfwSetWindowShouldClose on the window.
[[nodiscard]] bool AndroidGlfwCloseRequestedUVE();

} // namespace UVE::Window
