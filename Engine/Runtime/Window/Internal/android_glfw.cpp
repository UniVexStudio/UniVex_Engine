// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The part of the GLFW API the editor host and the launchers use, answered on Android by the one
// activity surface. The EGL context and buffer swaps belong to AndroidWindowManagerUVE; this file
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
    bool pressed = false;   // the left button (DispatchAndroidGlfwPointerUVE)
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

GLFWwindow g_windowUVE;
GLFWmonitor g_monitorUVE;
GLFWvidmode g_modeUVE{};
ANativeActivity* g_activityUVE = nullptr;
std::string g_clipboardUVE;
bool g_hardwareInputActiveUVE = false;
int g_heldModsUVE = 0;
int g_stickyModsUVE = 0;
std::vector<UVE::Window::AndroidTouchContactUVE> g_touchContactsUVE;
std::function<bool(float, float)> g_touchClaimHookUVE;
const auto g_startUVE = std::chrono::steady_clock::now();
/// The GLES library, for entry points eglGetProcAddress does not return.
inline constexpr const char* kGlesLibraryUVE = "libGLESv3.so";
/// GLFW_HOVERED: a touch screen has no pointer outside the window.
inline constexpr int kTrueUVE = GLFW_TRUE;

[[nodiscard]] int SurfaceWidthUVE() {
    return g_windowUVE.surface != nullptr ? ANativeWindow_getWidth(g_windowUVE.surface) : 0;
}

[[nodiscard]] int SurfaceHeightUVE() {
    return g_windowUVE.surface != nullptr ? ANativeWindow_getHeight(g_windowUVE.surface) : 0;
}

} // namespace

namespace UVE::Window {

GLFWwindow* AndroidGlfwWindowUVE() {
    return &g_windowUVE;
}

void SetAndroidGlfwActivityUVE(ANativeActivity* const activity) {
    g_activityUVE = activity;
}

void SetAndroidGlfwSurfaceUVE(ANativeWindow* const surface) {
    g_windowUVE.surface = surface;
}

void SetAndroidGlfwFocusUVE(const bool focused) {
    if (g_windowUVE.focused == focused) {
        return;
    }
    g_windowUVE.focused = focused;
    if (g_windowUVE.focus != nullptr) {
        g_windowUVE.focus(&g_windowUVE, focused ? GLFW_TRUE : GLFW_FALSE);
    }
}

void SetAndroidGlfwContentScaleUVE(const float scale) {
    g_windowUVE.contentScale = scale;
}

void DispatchAndroidGlfwPointerUVE(const AndroidPointerPhaseUVE phase, const float x, const float y) {
    GLFWwindow& w = g_windowUVE;
    w.cursorX = x;
    w.cursorY = y;
    if (w.cursorPos != nullptr) {
        w.cursorPos(&w, x, y);
    }
    const bool down = phase == AndroidPointerPhaseUVE::Down || phase == AndroidPointerPhaseUVE::Move;
    if (down != w.pressed) {
        w.pressed = down;
        w.buttons[GLFW_MOUSE_BUTTON_LEFT] = down;
        if (w.mouseButton != nullptr) {
            w.mouseButton(&w, GLFW_MOUSE_BUTTON_LEFT, down ? GLFW_PRESS : GLFW_RELEASE, g_heldModsUVE | g_stickyModsUVE);
        }
    }
}

void DispatchAndroidGlfwCursorUVE(const float x, const float y) {
    GLFWwindow& w = g_windowUVE;
    w.cursorX = x;
    w.cursorY = y;
    if (w.cursorPos != nullptr) {
        w.cursorPos(&w, x, y);
    }
}

void DispatchAndroidGlfwMouseButtonUVE(const int button, const bool down, const float x, const float y) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) {
        return;
    }
    DispatchAndroidGlfwCursorUVE(x, y);
    GLFWwindow& w = g_windowUVE;
    auto& held = w.buttons[static_cast<std::size_t>(button)];
    if (held == down) {
        return;
    }
    held = down;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        w.pressed = down;
    }
    if (w.mouseButton != nullptr) {
        w.mouseButton(&w, button, down ? GLFW_PRESS : GLFW_RELEASE, g_heldModsUVE | g_stickyModsUVE);
    }
}

void DispatchAndroidGlfwScrollUVE(const float dy, const float dx) {
    if (g_windowUVE.scroll != nullptr) {
        g_windowUVE.scroll(&g_windowUVE, static_cast<double>(dx), static_cast<double>(dy));
    }
}

void DispatchAndroidGlfwCharUVE(const unsigned int codepoint) {
    if (g_windowUVE.character != nullptr) {
        g_windowUVE.character(&g_windowUVE, codepoint);
    }
}

void DispatchAndroidGlfwKeyUVE(const int glfwKey, const int action, const int mods) {
    if (g_windowUVE.key != nullptr) {
        g_windowUVE.key(&g_windowUVE, glfwKey, 0, action, mods | g_stickyModsUVE);
    }
}

void SetAndroidGlfwHardwareInputUVE(const bool active) {
    g_hardwareInputActiveUVE = active;
}

bool AndroidGlfwHasHardwareInputUVE() {
    return g_hardwareInputActiveUVE;
}

void SetAndroidGlfwHeldModsUVE(const int glfwMods) {
    g_heldModsUVE = glfwMods;
}

void SetAndroidGlfwStickyModsUVE(const int glfwMods) {
    g_stickyModsUVE = glfwMods;
}

int AndroidGlfwStickyModsUVE() {
    return g_stickyModsUVE;
}

void SetAndroidGlfwTouchContactsUVE(std::vector<AndroidTouchContactUVE> contacts) {
    g_touchContactsUVE = std::move(contacts);
}

std::vector<AndroidTouchContactUVE> AndroidGlfwTouchContactsUVE() {
    return g_touchContactsUVE;
}

void SetAndroidGlfwTouchClaimHookUVE(std::function<bool(float x, float y)> hook) {
    g_touchClaimHookUVE = std::move(hook);
}

bool AndroidGlfwClaimTouchAtUVE(const float x, const float y) {
    return g_touchClaimHookUVE && g_touchClaimHookUVE(x, y);
}

bool AndroidGlfwHasAllFilesAccessUVE() {
    return HasAndroidAllFilesAccessUVE(g_activityUVE);
}

void RequestAndroidGlfwAllFilesAccessUVE() {
    RequestAndroidAllFilesAccessUVE(g_activityUVE);
}

AndroidStoragePathsUVE ReadAndroidGlfwStoragePathsUVE() {
    return ReadAndroidStoragePathsUVE(g_activityUVE);
}

AndroidSafeAreaUVE ReadAndroidGlfwSafeAreaUVE() {
    return ReadAndroidSafeAreaUVE(g_activityUVE);
}

void SetAndroidGlfwTextInputUVE(const bool wanted) {
    g_windowUVE.textInput = wanted;
}

bool AndroidGlfwTextInputWantedUVE() {
    return g_windowUVE.textInput;
}

bool AndroidGlfwCloseRequestedUVE() {
    return g_windowUVE.shouldClose;
}

void ResetAndroidGlfwWindowUVE() {
    GLFWwindow& w = g_windowUVE;
    w.shouldClose = false;
    w.pressed = false;
    w.buttons.fill(false);
    w.textInput = false;
    g_touchContactsUVE.clear();
    g_touchClaimHookUVE = nullptr;
}

} // namespace UVE::Window

// --- The GLFW API -------------------------------------------------------------------------------

double glfwGetTime(void) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_startUVE).count();
}

GLFWglproc glfwGetProcAddress(const char* const name) {
    if (const __eglMustCastToProperFunctionPointerType proc = eglGetProcAddress(name)) {
        return reinterpret_cast<GLFWglproc>(proc);
    }
    static void* const gles = dlopen(kGlesLibraryUVE, RTLD_NOW | RTLD_LOCAL);
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
    return &g_windowUVE;
}
void glfwSwapBuffers(GLFWwindow*) {}
void glfwSwapInterval(int) {}

void glfwGetFramebufferSize(GLFWwindow*, int* const width, int* const height) {
    if (width != nullptr) *width = SurfaceWidthUVE();
    if (height != nullptr) *height = SurfaceHeightUVE();
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
        return kTrueUVE;
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
    g_clipboardUVE = text != nullptr ? text : "";
    UVE::Window::WriteAndroidClipboardUVE(g_activityUVE, g_clipboardUVE.c_str());
}
const char* glfwGetClipboardString(GLFWwindow*) {
    if (std::optional<std::string> systemText = UVE::Window::ReadAndroidClipboardUVE(g_activityUVE);
        systemText.has_value()) {
        g_clipboardUVE = std::move(*systemText);
    }
    return g_clipboardUVE.c_str();
}

GLFWmonitor* glfwGetPrimaryMonitor(void) {
    return &g_monitorUVE;
}
const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*) {
    g_modeUVE.width = SurfaceWidthUVE();
    g_modeUVE.height = SurfaceHeightUVE();
    return &g_modeUVE;
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
