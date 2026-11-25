// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The part of the GLFW API the editor host and the launchers use, answered on Android by the one
// activity surface. The EGL context and buffer swaps belong to AndroidWindowManager; this file
// only reports the surface, routes input to the registered callbacks, and refuses what a phone has
// no equivalent for (more OS windows, moving or resizing the window).

#include "android_glfw.h"

#include <EGL/egl.h>
#include <android/native_window.h>
#include <dlfcn.h>

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <utility>

#include "android_activity.h"

struct GLFWwindow {
    ANativeWindow* surface = nullptr;
    float contentScale = 1.0F;
    bool focused = true;
    bool shouldClose = false;
    bool pressed = false;   // the left button (DispatchAndroidGlfwPointer)
    bool textInput = false; // the interface wants the soft keyboard
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> buttons{};
    double cursorX = 0.0;
    double cursorY = 0.0;
    void* user = nullptr;
    GLFWcursorposfun cursorPos = nullptr;
    GLFWmousebuttonfun mouseButton = nullptr;
    GLFWscrollfun scroll = nullptr;
    GLFWkeyfun key = nullptr;
    GLFWcharfun character = nullptr;
    GLFWcursorenterfun cursorEnter = nullptr;
    GLFWwindowclosefun close = nullptr;
    GLFWwindowfocusfun focus = nullptr;
};

struct GLFWcursor {
    int shape = 0;
};

struct GLFWmonitor {};

namespace {

GLFWwindow g_window;
GLFWmonitor g_monitor;
GLFWvidmode g_mode{};
ANativeActivity* g_activity = nullptr;
std::string g_clipboard;
bool g_hardwareInputActive = false;
int g_heldMods = 0;
int g_stickyMods = 0;
std::vector<UVE::Window::AndroidTouchContact> g_touchContacts;
std::function<bool(float, float)> g_touchClaimHook;
const auto g_start = std::chrono::steady_clock::now();
/// The GLES library, for entry points eglGetProcAddress does not return.
inline constexpr const char* kGlesLibrary = "libGLESv3.so";
/// GLFW_HOVERED: a touch screen has no pointer outside the window.
inline constexpr int kTrue = GLFW_TRUE;

[[nodiscard]] int SurfaceWidth() {
    return g_window.surface != nullptr ? ANativeWindow_getWidth(g_window.surface) : 0;
}

[[nodiscard]] int SurfaceHeight() {
    return g_window.surface != nullptr ? ANativeWindow_getHeight(g_window.surface) : 0;
}

} // namespace

namespace UVE::Window {

GLFWwindow* AndroidGlfwWindow() {
    return &g_window;
}

void SetAndroidGlfwActivity(ANativeActivity* const activity) {
    g_activity = activity;
}

void SetAndroidGlfwSurface(ANativeWindow* const surface) {
    g_window.surface = surface;
}

void SetAndroidGlfwFocus(const bool focused) {
    if (g_window.focused == focused) {
        return;
    }
    g_window.focused = focused;
    if (g_window.focus != nullptr) {
        g_window.focus(&g_window, focused ? GLFW_TRUE : GLFW_FALSE);
    }
}

void SetAndroidGlfwContentScale(const float scale) {
    g_window.contentScale = scale;
}

void DispatchAndroidGlfwPointer(const AndroidPointerPhase phase, const float x, const float y) {
    GLFWwindow& w = g_window;
    w.cursorX = x;
    w.cursorY = y;
    if (w.cursorPos != nullptr) {
        w.cursorPos(&w, x, y);
    }
    const bool down = phase == AndroidPointerPhase::Down || phase == AndroidPointerPhase::Move;
    if (down != w.pressed) {
        w.pressed = down;
        w.buttons[GLFW_MOUSE_BUTTON_LEFT] = down;
        if (w.mouseButton != nullptr) {
            w.mouseButton(&w, GLFW_MOUSE_BUTTON_LEFT, down ? GLFW_PRESS : GLFW_RELEASE, g_heldMods | g_stickyMods);
        }
    }
}

void DispatchAndroidGlfwCursor(const float x, const float y) {
    GLFWwindow& w = g_window;
    w.cursorX = x;
    w.cursorY = y;
    if (w.cursorPos != nullptr) {
        w.cursorPos(&w, x, y);
    }
}

void DispatchAndroidGlfwMouseButton(const int button, const bool down, const float x, const float y) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) {
        return;
    }
    DispatchAndroidGlfwCursor(x, y);
    GLFWwindow& w = g_window;
    auto& held = w.buttons[static_cast<std::size_t>(button)];
    if (held == down) {
        return;
    }
    held = down;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        w.pressed = down;
    }
    if (w.mouseButton != nullptr) {
        w.mouseButton(&w, button, down ? GLFW_PRESS : GLFW_RELEASE, g_heldMods | g_stickyMods);
    }
}

void DispatchAndroidGlfwScroll(const float dy, const float dx) {
    if (g_window.scroll != nullptr) {
        g_window.scroll(&g_window, static_cast<double>(dx), static_cast<double>(dy));
    }
}

void DispatchAndroidGlfwChar(const unsigned int codepoint) {
    if (g_window.character != nullptr) {
        g_window.character(&g_window, codepoint);
    }
}

void DispatchAndroidGlfwKey(const int glfwKey, const int action, const int mods) {
    if (g_window.key != nullptr) {
        g_window.key(&g_window, glfwKey, 0, action, mods | g_stickyMods);
    }
}

void SetAndroidGlfwHardwareInput(const bool active) {
    g_hardwareInputActive = active;
}

bool AndroidGlfwHasHardwareInput() {
    return g_hardwareInputActive;
}

void SetAndroidGlfwHeldMods(const int glfwMods) {
    g_heldMods = glfwMods;
}

void SetAndroidGlfwStickyMods(const int glfwMods) {
    g_stickyMods = glfwMods;
}

int AndroidGlfwStickyMods() {
    return g_stickyMods;
}

void SetAndroidGlfwTouchContacts(std::vector<AndroidTouchContact> contacts) {
    g_touchContacts = std::move(contacts);
}

std::vector<AndroidTouchContact> AndroidGlfwTouchContacts() {
    return g_touchContacts;
}

void SetAndroidGlfwTouchClaimHook(std::function<bool(float x, float y)> hook) {
    g_touchClaimHook = std::move(hook);
}

bool AndroidGlfwClaimTouchAt(const float x, const float y) {
    return g_touchClaimHook && g_touchClaimHook(x, y);
}

bool AndroidGlfwHasAllFilesAccess() {
    return HasAndroidAllFilesAccess(g_activity);
}

void RequestAndroidGlfwAllFilesAccess() {
    RequestAndroidAllFilesAccess(g_activity);
}

AndroidStoragePaths ReadAndroidGlfwStoragePaths() {
    return ReadAndroidStoragePaths(g_activity);
}

AndroidSafeArea ReadAndroidGlfwSafeArea() {
    return ReadAndroidSafeArea(g_activity);
}

void SetAndroidGlfwTextInput(const bool wanted) {
    g_window.textInput = wanted;
}

bool AndroidGlfwTextInputWanted() {
    return g_window.textInput;
}

bool AndroidGlfwCloseRequested() {
    return g_window.shouldClose;
}

void ResetAndroidGlfwWindow() {
    GLFWwindow& w = g_window;
    w.shouldClose = false;
    w.pressed = false;
    w.buttons.fill(false);
    w.textInput = false;
    g_touchContacts.clear();
    g_touchClaimHook = nullptr;
}

} // namespace UVE::Window

// --- The GLFW API -------------------------------------------------------------------------------

double glfwGetTime(void) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
}

GLFWglproc glfwGetProcAddress(const char* const name) {
    if (const __eglMustCastToProperFunctionPointerType proc = eglGetProcAddress(name)) {
        return reinterpret_cast<GLFWglproc>(proc);
    }
    static void* const gles = dlopen(kGlesLibrary, RTLD_NOW | RTLD_LOCAL);
    return gles != nullptr ? reinterpret_cast<GLFWglproc>(dlsym(gles, name)) : nullptr;
}

// One window only: further OS windows are refused, and their callers already handle that.
GLFWwindow* glfwCreateWindow(int, int, const char*, GLFWmonitor*, GLFWwindow*) {
    return nullptr;
}
void glfwDestroyWindow(GLFWwindow*) {}
void glfwDefaultWindowHints(void) {}
void glfwWindowHint(int, int) {}
void glfwMakeContextCurrent(GLFWwindow*) {}
GLFWwindow* glfwGetCurrentContext(void) {
    return &g_window;
}
void glfwSwapBuffers(GLFWwindow*) {}
void glfwSwapInterval(int) {}

void glfwGetFramebufferSize(GLFWwindow*, int* const width, int* const height) {
    if (width != nullptr) *width = SurfaceWidth();
    if (height != nullptr) *height = SurfaceHeight();
}
void glfwGetWindowSize(GLFWwindow* const window, int* const width, int* const height) {
    glfwGetFramebufferSize(window, width, height);
}
void glfwGetWindowContentScale(GLFWwindow* const window, float* const x, float* const y) {
    if (x != nullptr) *x = window->contentScale;
    if (y != nullptr) *y = window->contentScale;
}
void glfwGetWindowPos(GLFWwindow*, int* const x, int* const y) {
    if (x != nullptr) *x = 0;
    if (y != nullptr) *y = 0;
}
// The surface fills the screen; it cannot be moved, resized, minimized or restored.
void glfwSetWindowPos(GLFWwindow*, int, int) {}
void glfwSetWindowSize(GLFWwindow*, int, int) {}
void glfwSetWindowSizeLimits(GLFWwindow*, int, int, int, int) {}
void glfwMaximizeWindow(GLFWwindow*) {}
void glfwIconifyWindow(GLFWwindow*) {}
void glfwRestoreWindow(GLFWwindow*) {}
void glfwShowWindow(GLFWwindow*) {}
void glfwFocusWindow(GLFWwindow*) {}

int glfwGetWindowAttrib(GLFWwindow* const window, const int attribute) {
    switch (attribute) {
    case GLFW_FOCUSED:
        return window->focused ? GLFW_TRUE : GLFW_FALSE;
    case GLFW_HOVERED:
    case GLFW_MAXIMIZED:
    case GLFW_VISIBLE:
        return kTrue;
    default:
        return 0;
    }
}

int glfwWindowShouldClose(GLFWwindow* const window) {
    return window->shouldClose ? GLFW_TRUE : GLFW_FALSE;
}
void glfwSetWindowShouldClose(GLFWwindow* const window, const int value) {
    window->shouldClose = value != GLFW_FALSE;
}
void glfwSetWindowUserPointer(GLFWwindow* const window, void* const pointer) {
    window->user = pointer;
}
void* glfwGetWindowUserPointer(GLFWwindow* const window) {
    return window->user;
}

void glfwGetCursorPos(GLFWwindow* const window, double* const x, double* const y) {
    if (x != nullptr) *x = window->cursorX;
    if (y != nullptr) *y = window->cursorY;
}
// No pointer to hide or capture on a touch screen.
void glfwSetInputMode(GLFWwindow*, int, int) {}
int glfwRawMouseMotionSupported(void) {
    return GLFW_FALSE;
}
GLFWcursor* glfwCreateStandardCursor(const int shape) {
    return new GLFWcursor{shape};
}
void glfwDestroyCursor(GLFWcursor* const cursor) {
    delete cursor;
}
void glfwSetCursor(GLFWwindow*, GLFWcursor*) {}

void glfwSetClipboardString(GLFWwindow*, const char* const text) {
    g_clipboard = text != nullptr ? text : "";
    UVE::Window::WriteAndroidClipboard(g_activity, g_clipboard.c_str());
}
const char* glfwGetClipboardString(GLFWwindow*) {
    if (std::optional<std::string> systemText = UVE::Window::ReadAndroidClipboard(g_activity);
        systemText.has_value()) {
        g_clipboard = std::move(*systemText);
    }
    return g_clipboard.c_str();
}

GLFWmonitor* glfwGetPrimaryMonitor(void) {
    return &g_monitor;
}
const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*) {
    g_mode.width = SurfaceWidth();
    g_mode.height = SurfaceHeight();
    return &g_mode;
}

GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* const w, const GLFWcursorposfun f) {
    const GLFWcursorposfun previous = w->cursorPos;
    w->cursorPos = f;
    return previous;
}
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* const w, const GLFWmousebuttonfun f) {
    const GLFWmousebuttonfun previous = w->mouseButton;
    w->mouseButton = f;
    return previous;
}
GLFWscrollfun glfwSetScrollCallback(GLFWwindow* const w, const GLFWscrollfun f) {
    const GLFWscrollfun previous = w->scroll;
    w->scroll = f;
    return previous;
}
GLFWkeyfun glfwSetKeyCallback(GLFWwindow* const w, const GLFWkeyfun f) {
    const GLFWkeyfun previous = w->key;
    w->key = f;
    return previous;
}
GLFWcharfun glfwSetCharCallback(GLFWwindow* const w, const GLFWcharfun f) {
    const GLFWcharfun previous = w->character;
    w->character = f;
    return previous;
}
GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow* const w, const GLFWcursorenterfun f) {
    const GLFWcursorenterfun previous = w->cursorEnter;
    w->cursorEnter = f;
    return previous;
}
GLFWwindowclosefun glfwSetWindowCloseCallback(GLFWwindow* const w, const GLFWwindowclosefun f) {
    const GLFWwindowclosefun previous = w->close;
    w->close = f;
    return previous;
}
GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow* const w, const GLFWwindowfocusfun f) {
    const GLFWwindowfocusfun previous = w->focus;
    w->focus = f;
    return previous;
}
