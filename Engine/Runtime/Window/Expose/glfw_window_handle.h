// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <GLFW/glfw3.h>

#if defined(__ANDROID__)
#include "android_glfw.h"
#endif

namespace UVE::Window {

/// The GLFW window behind a window manager's native handle, for code written against GLFW (the
/// editor host). On the desktop the handle is the GLFW window; on Android it is the activity's
/// surface, which the engine's Android implementation of the GLFW API stands in for.
[[nodiscard]] inline GLFWwindow* GlfwWindowFromNativeHandle(void* const nativeHandle) {
#if defined(__ANDROID__)
    return nativeHandle != nullptr ? AndroidGlfwWindow() : nullptr;
#else
    return static_cast<GLFWwindow*>(nativeHandle);
#endif
}

} // namespace UVE::Window
