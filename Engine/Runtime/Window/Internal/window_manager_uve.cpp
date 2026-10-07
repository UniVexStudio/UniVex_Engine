// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/window/window_manager_uve.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#endif

#if defined(__linux__) && defined(UVE_HAS_SYSTEMD)
#include <cerrno>
#include <fcntl.h>
#include <systemd/sd-bus.h>
#include <unistd.h>
#endif

#include "uve/logging/logging_macros_uve.h"
#include "uve/input/key_code_uve.h"
#include "uve/input/mouse_button_uve.h"
#include "uve/window/window_events_uve.h"
#include "uve/window/content_scale_policy_uve.h"
#include "uve/window/cursor_policy_uve.h"
#include "uve/window/monitor_selection_policy_uve.h"
#include "uve/window/window_mode_policy_uve.h"
#include "uve/window/vsync_policy_uve.h"
#include "uve/window/monitor_info_validation_uve.h"
#include "uve/window/window_desc_validation_uve.h"
#include "uve/window/window_size_policy_uve.h"
#include "uve/window/window_style_policy_uve.h"
#include "uve/window/framebuffer_size_validation_uve.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace UVE::Window {

namespace {

/// Process-wide GLFW init refcount: only the first live WindowManagerUVE calls glfwInit(), only
/// the last one destroyed calls glfwTerminate(). Correct even though this codebase only ever
/// constructs one WindowManagerUVE at a time today — matches how a shared library-level resource
/// should be initialized exactly once regardless of how many owners reference it.
int g_glfwRefCount = 0;

void GlfwErrorCallbackUVE(int errorCode, const char* description) {
    UVE_ERROR("WindowManagerUVE: GLFW error {}: {}", errorCode,
              description != nullptr ? description : "(no description)");
}

[[nodiscard]] GLFWmonitor* FindMonitorUVE(const std::string_view requestedName) {
    if (requestedName.empty()) {
        return glfwGetPrimaryMonitor();
    }
    int count = 0;
    GLFWmonitor** const monitors = glfwGetMonitors(&count);
    if (monitors != nullptr && count > 0) {
        std::vector<std::string_view> monitorNames;
        monitorNames.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            const char* const name = glfwGetMonitorName(monitors[index]);
            monitorNames.emplace_back(name != nullptr ? std::string_view{name} : std::string_view{});
        }
        const std::optional<std::size_t> selectedIndex = FindMonitorIndexUVE(requestedName, monitorNames);
        if (selectedIndex.has_value()) {
            return monitors[*selectedIndex];
        }
    }
    UVE_WARNING("WindowManagerUVE: configured monitor \"{}\" was not found; using the primary monitor",
                requestedName);
    return glfwGetPrimaryMonitor();
}

} // namespace

struct WindowManagerUVE::ImplUVE {
    Events::IEventSystemUVE* eventSystem;
    WindowDescUVE desc;
    GLFWwindow* window = nullptr;
    GLFWcursor* cursor = nullptr;
    GLFWmonitor* targetMonitor = nullptr;
    bool valid = false;
    // True iff this instance's constructor incremented g_glfwRefCount and has not yet undone
    // that increment — the destructor's only signal for whether it owes a decrement, since a
    // glfwInit() failure (never incremented) and a glfwCreateWindow() failure (incremented, then
    // already decremented inline) must not be double-counted.
    bool ownsGlfwRefCount = false;
    bool vsyncEnabled = true;
    Platform::VSyncModeUVE requestedVsyncMode = Platform::VSyncModeUVE::On;
    Platform::VSyncModeUVE effectiveVsyncMode = Platform::VSyncModeUVE::On;
    Platform::WindowModeUVE windowMode = Platform::WindowModeUVE::Windowed;
    bool fullscreen = false;
    bool focused = true;
    bool transparent = false;
    bool highDpiAware = true;
    bool displaySleepAllowed = true;
    bool displaySleepWarningLogged = false;
#if defined(__APPLE__)
    IOPMAssertionID displaySleepAssertion = kIOPMNullAssertionID;
#elif defined(__linux__) && defined(UVE_HAS_SYSTEMD)
    int displaySleepInhibitorFd = -1;
#endif
    double contentScaleOverride = 0.0;
    Input::IInputSystemUVE* inputSystem = nullptr;
    float scrollDelta = 0.0F;
    int windowedX = 0;
    int windowedY = 0;
    int windowedWidth = 0;
    int windowedHeight = 0;

    explicit ImplUVE(Events::IEventSystemUVE& eventSystemIn, const WindowDescUVE& descIn)
        : eventSystem(&eventSystemIn), desc(descIn), requestedVsyncMode(descIn.vsyncMode),
          effectiveVsyncMode(descIn.vsyncMode), windowMode(descIn.mode), transparent(descIn.transparent),
          highDpiAware(descIn.highDpiAware), displaySleepAllowed(descIn.allowDisplaySleep),
          contentScaleOverride(descIn.contentScaleOverride) {}

    void ApplyVSyncUVE(Platform::VSyncModeUVE requestedMode);
    void ApplyCursorInputModeUVE();

    static ImplUVE& FromWindowUVE(GLFWwindow* glfwWindow) {
        return *static_cast<ImplUVE*>(glfwGetWindowUserPointer(glfwWindow));
    }

    static void CloseCallbackUVE(GLFWwindow* glfwWindow) {
        glfwSetWindowShouldClose(glfwWindow, GLFW_TRUE);
        FromWindowUVE(glfwWindow).eventSystem->Publish(WindowCloseRequestedEventUVE{});
    }

    static void FramebufferSizeCallbackUVE(GLFWwindow* glfwWindow, int width, int height) {
        std::uint32_t framebufferWidth = 0U;
        std::uint32_t framebufferHeight = 0U;
        if (!ValidateFramebufferSizeUVE(width, height, framebufferWidth, framebufferHeight)) {
            return;
        }
        // Publishes only — never touches GL state here. GlRenderDeviceUVE polls the window's
        // current size once per frame instead, per the approved design.
        FromWindowUVE(glfwWindow).eventSystem->Publish(
            WindowResizedEventUVE{framebufferWidth, framebufferHeight});
    }

    static void FocusCallbackUVE(GLFWwindow* glfwWindow, int focused) {
        ImplUVE& impl = FromWindowUVE(glfwWindow);
        impl.focused = focused == GLFW_TRUE;
        impl.eventSystem->Publish(WindowFocusChangedEventUVE{impl.focused});
    }

    static void ScrollCallbackUVE(GLFWwindow* glfwWindow, double /*xOffset*/, double yOffset) {
        FromWindowUVE(glfwWindow).scrollDelta += static_cast<float>(yOffset);
    }

    static int ToGlfwKeyUVE(const Input::KeyCodeUVE key) noexcept {
        using Input::KeyCodeUVE;
        const int value = static_cast<int>(key);
        if (value >= static_cast<int>(KeyCodeUVE::A) && value <= static_cast<int>(KeyCodeUVE::Z)) {
            return GLFW_KEY_A + value - static_cast<int>(KeyCodeUVE::A);
        }
        if (value >= static_cast<int>(KeyCodeUVE::Num0) && value <= static_cast<int>(KeyCodeUVE::Num9)) {
            return GLFW_KEY_0 + value - static_cast<int>(KeyCodeUVE::Num0);
        }
        if (value >= static_cast<int>(KeyCodeUVE::F1) && value <= static_cast<int>(KeyCodeUVE::F12)) {
            return GLFW_KEY_F1 + value - static_cast<int>(KeyCodeUVE::F1);
        }
        switch (key) {
        case KeyCodeUVE::Up: return GLFW_KEY_UP;
        case KeyCodeUVE::Down: return GLFW_KEY_DOWN;
        case KeyCodeUVE::Left: return GLFW_KEY_LEFT;
        case KeyCodeUVE::Right: return GLFW_KEY_RIGHT;
        case KeyCodeUVE::Space: return GLFW_KEY_SPACE;
        case KeyCodeUVE::Enter: return GLFW_KEY_ENTER;
        case KeyCodeUVE::Escape: return GLFW_KEY_ESCAPE;
        case KeyCodeUVE::Tab: return GLFW_KEY_TAB;
        case KeyCodeUVE::Backspace: return GLFW_KEY_BACKSPACE;
        case KeyCodeUVE::LeftShift: return GLFW_KEY_LEFT_SHIFT;
        case KeyCodeUVE::RightShift: return GLFW_KEY_RIGHT_SHIFT;
        case KeyCodeUVE::LeftCtrl: return GLFW_KEY_LEFT_CONTROL;
        case KeyCodeUVE::RightCtrl: return GLFW_KEY_RIGHT_CONTROL;
        case KeyCodeUVE::LeftAlt: return GLFW_KEY_LEFT_ALT;
        case KeyCodeUVE::RightAlt: return GLFW_KEY_RIGHT_ALT;
        default: return GLFW_KEY_UNKNOWN;
        }
    }

    static int ToGlfwMouseButtonUVE(const Input::MouseButtonUVE button) noexcept {
        switch (button) {
        case Input::MouseButtonUVE::Left: return GLFW_MOUSE_BUTTON_LEFT;
        case Input::MouseButtonUVE::Right: return GLFW_MOUSE_BUTTON_RIGHT;
        case Input::MouseButtonUVE::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
        default: return -1;
        }
    }

    void PollInputUVE() noexcept {
        if (inputSystem == nullptr || window == nullptr) {
            return;
        }
        for (int value = static_cast<int>(Input::KeyCodeUVE::A);
             value < static_cast<int>(Input::KeyCodeUVE::Count); ++value) {
            const auto key = static_cast<Input::KeyCodeUVE>(value);
            const int glfwKey = ToGlfwKeyUVE(key);
            if (glfwKey != GLFW_KEY_UNKNOWN) {
                inputSystem->SetKeyStateUVE(key, glfwGetKey(window, glfwKey) != GLFW_RELEASE);
            }
        }
        for (int value = 0; value < static_cast<int>(Input::MouseButtonUVE::Count); ++value) {
            const auto button = static_cast<Input::MouseButtonUVE>(value);
            const int glfwButton = ToGlfwMouseButtonUVE(button);
            inputSystem->SetMouseButtonStateUVE(
                button, glfwGetMouseButton(window, glfwButton) != GLFW_RELEASE);
        }
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        inputSystem->SetMousePositionUVE(
            Math::Vector2UVE{static_cast<float>(cursorX), static_cast<float>(cursorY)});
        inputSystem->SetMouseScrollDeltaUVE(scrollDelta);
        scrollDelta = 0.0F;
    }
};

void WindowManagerUVE::ImplUVE::ApplyVSyncUVE(const Platform::VSyncModeUVE requestedMode) {
    requestedVsyncMode = requestedMode;
    bool adaptiveIntervalSupported = false;
    if (requestedMode == Platform::VSyncModeUVE::Adaptive) {
        adaptiveIntervalSupported = glfwExtensionSupported("WGL_EXT_swap_control_tear") == GLFW_TRUE ||
                                   glfwExtensionSupported("GLX_EXT_swap_control_tear") == GLFW_TRUE ||
                                   glfwExtensionSupported("EGL_EXT_swap_control_tear") == GLFW_TRUE;
    }
    const OpenGlVSyncResolutionUVE resolution =
        ResolveOpenGlVSyncUVE(requestedMode, adaptiveIntervalSupported);
    if (requestedMode == Platform::VSyncModeUVE::Adaptive &&
        resolution.effectiveMode != Platform::VSyncModeUVE::Adaptive) {
        UVE_WARNING("WindowManagerUVE: adaptive V-sync is unavailable; falling back to On");
    } else if (requestedMode == Platform::VSyncModeUVE::Mailbox) {
        // OpenGL/GLFW exposes a swap interval, not a mailbox queue policy. Vulkan applies Mailbox
        // when the surface advertises it.
        UVE_WARNING("WindowManagerUVE: OpenGL does not expose Mailbox present mode; falling back to Off");
    }
    glfwSwapInterval(resolution.swapInterval);
    effectiveVsyncMode = resolution.effectiveMode;
    vsyncEnabled = resolution.effectiveMode != Platform::VSyncModeUVE::Off;
}

void WindowManagerUVE::ImplUVE::ApplyCursorInputModeUVE() {
    if (window == nullptr) {
        return;
    }
    const CursorInputResolutionUVE resolution =
        ResolveCursorInputPolicyUVE(desc.cursorVisible, desc.cursorConfinedToWindow);
    int glfwCursorMode = GLFW_CURSOR_NORMAL;
    switch (resolution.mode) {
    case CursorInputModeUVE::Normal:
        glfwCursorMode = GLFW_CURSOR_NORMAL;
        break;
    case CursorInputModeUVE::Hidden:
        glfwCursorMode = GLFW_CURSOR_HIDDEN;
        break;
    case CursorInputModeUVE::Confined:
        // GLFW's portable disabled mode captures and hides the pointer.
        glfwCursorMode = GLFW_CURSOR_DISABLED;
        break;
    }
    if (resolution.visibilityCompromised) {
        UVE_WARNING("WindowManagerUVE: GLFW cannot confine a visible cursor; confinement hides it");
    }
    glfwSetInputMode(window, GLFW_CURSOR, glfwCursorMode);
    glfwSetCursor(window, resolution.useCustomCursor ? cursor : nullptr);
}

WindowManagerUVE::WindowManagerUVE(Events::IEventSystemUVE& eventSystem, const WindowDescUVE& desc)
    : m_impl(std::make_unique<ImplUVE>(eventSystem, desc)) {
    if (!ValidateWindowDescUVE(desc)) {
        UVE_ERROR("WindowManagerUVE: rejected invalid window descriptor before GLFW initialization");
        return;
    }
    if (g_glfwRefCount == 0) {
        glfwSetErrorCallback(&GlfwErrorCallbackUVE);
        if (glfwInit() != GLFW_TRUE) {
            UVE_FATAL("WindowManagerUVE: glfwInit() failed");
            return;
        }
    }
    ++g_glfwRefCount;
    m_impl->ownsGlfwRefCount = true;

    m_impl->targetMonitor = FindMonitorUVE(desc.monitorName);
    const GLFWvidmode* const monitorMode = m_impl->targetMonitor != nullptr
                                               ? glfwGetVideoMode(m_impl->targetMonitor)
                                               : nullptr;
    int monitorX = 0;
    int monitorY = 0;
    if (m_impl->targetMonitor != nullptr) {
        glfwGetMonitorPos(m_impl->targetMonitor, &monitorX, &monitorY);
    }
    const Platform::WindowModeUVE effectiveMode = ResolveInitialWindowModeUVE(
        desc.mode, m_impl->targetMonitor != nullptr && monitorMode != nullptr);
    m_impl->windowMode = effectiveMode;
    if (effectiveMode != desc.mode) {
        UVE_WARNING("WindowManagerUVE: requested mode {} requires an available monitor video mode; using windowed",
                    Platform::GetWindowModeNameUVE(desc.mode));
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, static_cast<int>(desc.glVersionMajor));
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, static_cast<int>(desc.glVersionMinor));
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // GLFW_OPENGL_FORWARD_COMPAT is deliberately NOT set: it changes platform-specific context
    // behavior and has previously broken X11 resize callback delivery on the Mesa test host.
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    const bool initiallyDecorated = ShouldDecorateWindowUVE(desc.borderless, effectiveMode);
    glfwWindowHint(GLFW_DECORATED, initiallyDecorated ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, desc.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, desc.transparent ? GLFW_TRUE : GLFW_FALSE);
#ifdef GLFW_SCALE_TO_MONITOR
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, desc.perMonitorScaling ? GLFW_TRUE : GLFW_FALSE);
#endif
#ifdef GLFW_COCOA_RETINA_FRAMEBUFFER
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, desc.highDpiAware ? GLFW_TRUE : GLFW_FALSE);
#endif

    const bool exclusive = effectiveMode == Platform::WindowModeUVE::ExclusiveFullscreen;
    if (exclusive && monitorMode != nullptr) {
        glfwWindowHint(GLFW_REFRESH_RATE, monitorMode->refreshRate);
    }
    int createWidth = static_cast<int>(desc.width);
    int createHeight = static_cast<int>(desc.height);
    GLFWmonitor* createMonitor = exclusive ? m_impl->targetMonitor : nullptr;
    if ((effectiveMode == Platform::WindowModeUVE::Fullscreen || exclusive) && monitorMode != nullptr) {
        createWidth = monitorMode->width;
        createHeight = monitorMode->height;
    }
    m_impl->window = glfwCreateWindow(createWidth, createHeight, desc.title.c_str(), createMonitor, nullptr);
    if (m_impl->window == nullptr) {
        UVE_FATAL("WindowManagerUVE: glfwCreateWindow() failed requesting OpenGL {}.{} Core Profile",
                  desc.glVersionMajor, desc.glVersionMinor);
        --g_glfwRefCount;
        m_impl->ownsGlfwRefCount = false;
        if (g_glfwRefCount == 0) {
            glfwTerminate();
        }
        return;
    }

    if (!exclusive && effectiveMode == Platform::WindowModeUVE::Fullscreen && monitorMode != nullptr) {
        glfwSetWindowPos(m_impl->window, monitorX, monitorY);
    } else if (!exclusive && !desc.initialPositionSpecified && m_impl->targetMonitor != nullptr &&
               monitorMode != nullptr) {
        glfwSetWindowPos(m_impl->window,
                         monitorX + (monitorMode->width - createWidth) / 2,
                         monitorY + (monitorMode->height - createHeight) / 2);
    } else if (!exclusive && desc.initialPositionSpecified) {
        glfwSetWindowPos(m_impl->window, desc.initialPositionX, desc.initialPositionY);
    }
    const WindowSizeLimitsUVE sizeLimits = ResolveWindowSizeLimitsUVE(
        desc.minimumWidth, desc.minimumHeight, desc.maximumWidth, desc.maximumHeight, GLFW_DONT_CARE);
    glfwSetWindowSizeLimits(m_impl->window, sizeLimits.minimumWidth, sizeLimits.minimumHeight,
                            sizeLimits.maximumWidth, sizeLimits.maximumHeight);
    if (!exclusive && effectiveMode == Platform::WindowModeUVE::Maximized) {
        // Preserve the normal geometry before the maximize call changes glfwGetWindowSize/Pos
        // to the monitor-sized presentation bounds.
        glfwGetWindowPos(m_impl->window, &m_impl->windowedX, &m_impl->windowedY);
        glfwGetWindowSize(m_impl->window, &m_impl->windowedWidth, &m_impl->windowedHeight);
        glfwMaximizeWindow(m_impl->window);
    }

    if (!desc.icons.empty()) {
        std::vector<GLFWimage> glfwIcons;
        glfwIcons.reserve(desc.icons.size());
        for (const WindowIconUVE& icon : desc.icons) {
            auto* const pixels = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(icon.rgba8.data()));
            glfwIcons.push_back(GLFWimage{static_cast<int>(icon.width), static_cast<int>(icon.height), pixels});
        }
        // GLFW copies the image data during this call, so descriptor-owned pixel vectors may
        // go out of scope immediately after window construction.
        glfwSetWindowIcon(m_impl->window, static_cast<int>(glfwIcons.size()), glfwIcons.data());
    }
    if (!desc.cursorRgba8.empty()) {
        auto* const pixels = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(desc.cursorRgba8.data()));
        const GLFWimage image{static_cast<int>(desc.cursorImageWidth), static_cast<int>(desc.cursorImageHeight), pixels};
        m_impl->cursor = glfwCreateCursor(&image, static_cast<int>(desc.cursorHotspotX),
                                          static_cast<int>(desc.cursorHotspotY));
        if (m_impl->cursor == nullptr) {
            UVE_WARNING("WindowManagerUVE: GLFW rejected the configured cursor image; using the system cursor");
        }
    }

    glfwSetWindowUserPointer(m_impl->window, m_impl.get());
    glfwSetWindowCloseCallback(m_impl->window, &ImplUVE::CloseCallbackUVE);
    glfwSetFramebufferSizeCallback(m_impl->window, &ImplUVE::FramebufferSizeCallbackUVE);
    glfwSetWindowFocusCallback(m_impl->window, &ImplUVE::FocusCallbackUVE);
    glfwSetScrollCallback(m_impl->window, &ImplUVE::ScrollCallbackUVE);
    glfwSetWindowAttrib(m_impl->window, GLFW_FLOATING, desc.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
    m_impl->ApplyCursorInputModeUVE();

    glfwMakeContextCurrent(m_impl->window);
    m_impl->ApplyVSyncUVE(desc.vsyncModeExplicit
                              ? desc.vsyncMode
                              : (desc.vsyncEnabled ? Platform::VSyncModeUVE::On
                                                   : Platform::VSyncModeUVE::Off));

    m_impl->fullscreen = exclusive || effectiveMode == Platform::WindowModeUVE::Fullscreen;
    m_impl->focused = glfwGetWindowAttrib(m_impl->window, GLFW_FOCUSED) == GLFW_TRUE;
    if (!m_impl->fullscreen && effectiveMode != Platform::WindowModeUVE::Maximized) {
        glfwGetWindowPos(m_impl->window, &m_impl->windowedX, &m_impl->windowedY);
        glfwGetWindowSize(m_impl->window, &m_impl->windowedWidth, &m_impl->windowedHeight);
    } else if (m_impl->fullscreen) {
        m_impl->windowedX = desc.initialPositionSpecified ? desc.initialPositionX
                                                          : monitorX + (monitorMode != nullptr ? (monitorMode->width - static_cast<int>(desc.width)) / 2 : 0);
        m_impl->windowedY = desc.initialPositionSpecified ? desc.initialPositionY
                                                          : monitorY + (monitorMode != nullptr ? (monitorMode->height - static_cast<int>(desc.height)) / 2 : 0);
        m_impl->windowedWidth = static_cast<int>(desc.width);
        m_impl->windowedHeight = static_cast<int>(desc.height);
    }
    m_impl->valid = true;
    SetDisplaySleepAllowedUVE(desc.allowDisplaySleep);
    UVE_INFO("WindowManagerUVE: created {}x{} window \"{}\" in mode {} using OpenGL {}.{} Core",
             createWidth, createHeight, desc.title, Platform::GetWindowModeNameUVE(m_impl->windowMode),
             desc.glVersionMajor, desc.glVersionMinor);
}

WindowManagerUVE::~WindowManagerUVE() {
    SetDisplaySleepAllowedUVE(true);
    if (m_impl->cursor != nullptr) {
        glfwDestroyCursor(m_impl->cursor);
    }
    if (m_impl->window != nullptr) {
        glfwDestroyWindow(m_impl->window);
    }
    if (m_impl->ownsGlfwRefCount) {
        --g_glfwRefCount;
        if (g_glfwRefCount == 0) {
            glfwTerminate();
        }
    }
}

bool WindowManagerUVE::IsValidUVE() const noexcept {
    return m_impl->valid;
}

void WindowManagerUVE::AttachInputSystemUVE(Input::IInputSystemUVE* inputSystem) noexcept {
    m_impl->inputSystem = inputSystem;
}

void WindowManagerUVE::PollEventsUVE() {
    if (m_impl->valid) {
        glfwPollEvents();
        m_impl->PollInputUVE();
    }
}

void WindowManagerUVE::SwapBuffersUVE() {
    if (m_impl->valid) {
        glfwSwapBuffers(m_impl->window);
    }
}

bool WindowManagerUVE::IsCloseRequestedUVE() const noexcept {
    return m_impl->valid && glfwWindowShouldClose(m_impl->window) == GLFW_TRUE;
}

void WindowManagerUVE::SetVSyncEnabledUVE(const bool enabled) {
    SetVSyncModeUVE(enabled ? Platform::VSyncModeUVE::On : Platform::VSyncModeUVE::Off);
}

bool WindowManagerUVE::IsVSyncEnabledUVE() const noexcept {
    return m_impl->vsyncEnabled;
}

void WindowManagerUVE::SetVSyncModeUVE(const Platform::VSyncModeUVE mode) {
    if (Platform::GetVSyncModeNameUVE(mode).empty()) {
        UVE_WARNING("WindowManagerUVE: ignored invalid V-sync mode request");
        return;
    }
    m_impl->requestedVsyncMode = mode;
    if (m_impl->valid) {
        m_impl->ApplyVSyncUVE(mode);
    } else {
        m_impl->effectiveVsyncMode = mode;
        m_impl->vsyncEnabled = mode != Platform::VSyncModeUVE::Off;
    }
}

Platform::VSyncModeUVE WindowManagerUVE::GetVSyncModeUVE() const noexcept {
    return m_impl->effectiveVsyncMode;
}

void WindowManagerUVE::SetFullscreenUVE(const bool fullscreen) {
    SetWindowModeUVE(fullscreen ? Platform::WindowModeUVE::Fullscreen
                                : Platform::WindowModeUVE::Windowed);
}

bool WindowManagerUVE::IsFullscreenUVE() const noexcept {
    return m_impl->fullscreen;
}

void WindowManagerUVE::SetWindowModeUVE(const Platform::WindowModeUVE mode) {
    if (!m_impl->valid || Platform::GetWindowModeNameUVE(mode).empty() || mode == m_impl->windowMode) {
        return;
    }
    GLFWmonitor* monitor = m_impl->targetMonitor != nullptr ? m_impl->targetMonitor : glfwGetPrimaryMonitor();
    const GLFWvidmode* const videoMode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
    if ((mode == Platform::WindowModeUVE::Fullscreen || mode == Platform::WindowModeUVE::ExclusiveFullscreen) &&
        (monitor == nullptr || videoMode == nullptr)) {
        UVE_ERROR("WindowManagerUVE: cannot enter fullscreen without a monitor video mode");
        return;
    }

    const bool currentlyMaximized = m_impl->windowMode == Platform::WindowModeUVE::Maximized ||
                                    glfwGetWindowAttrib(m_impl->window, GLFW_MAXIMIZED) == GLFW_TRUE;
    if (ShouldCaptureWindowedGeometryUVE(m_impl->fullscreen, currentlyMaximized)) {
        glfwGetWindowPos(m_impl->window, &m_impl->windowedX, &m_impl->windowedY);
        glfwGetWindowSize(m_impl->window, &m_impl->windowedWidth, &m_impl->windowedHeight);
    }
    int monitorX = 0;
    int monitorY = 0;
    if (monitor != nullptr) {
        glfwGetMonitorPos(monitor, &monitorX, &monitorY);
    }

    bool transitionConfirmed = true;
    if (mode == Platform::WindowModeUVE::ExclusiveFullscreen) {
        glfwSetWindowAttrib(m_impl->window, GLFW_DECORATED,
                            ShouldDecorateWindowUVE(m_impl->desc.borderless, mode) ? GLFW_TRUE : GLFW_FALSE);
        glfwSetWindowMonitor(m_impl->window, monitor, 0, 0, videoMode->width, videoMode->height,
                             videoMode->refreshRate);
        transitionConfirmed = glfwGetWindowMonitor(m_impl->window) == monitor;
    } else if (mode == Platform::WindowModeUVE::Fullscreen) {
        glfwSetWindowAttrib(m_impl->window, GLFW_DECORATED,
                            ShouldDecorateWindowUVE(m_impl->desc.borderless, mode) ? GLFW_TRUE : GLFW_FALSE);
        glfwSetWindowMonitor(m_impl->window, nullptr, monitorX, monitorY, videoMode->width,
                             videoMode->height, GLFW_DONT_CARE);
        transitionConfirmed = glfwGetWindowMonitor(m_impl->window) == nullptr;
    } else {
        const bool decorated = ShouldDecorateWindowUVE(m_impl->desc.borderless, mode);
        glfwSetWindowAttrib(m_impl->window, GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
        if (m_impl->fullscreen || glfwGetWindowMonitor(m_impl->window) != nullptr) {
            glfwSetWindowMonitor(m_impl->window, nullptr, m_impl->windowedX, m_impl->windowedY,
                                 m_impl->windowedWidth, m_impl->windowedHeight, GLFW_DONT_CARE);
        }
        if (mode == Platform::WindowModeUVE::Maximized) {
            glfwMaximizeWindow(m_impl->window);
        } else {
            glfwRestoreWindow(m_impl->window);
            if (mode == Platform::WindowModeUVE::Windowed || mode == Platform::WindowModeUVE::Borderless) {
                glfwSetWindowPos(m_impl->window, m_impl->windowedX, m_impl->windowedY);
                glfwSetWindowSize(m_impl->window, m_impl->windowedWidth, m_impl->windowedHeight);
            }
        }
        transitionConfirmed = glfwGetWindowMonitor(m_impl->window) == nullptr;
    }

    if (!transitionConfirmed) {
        UVE_ERROR("WindowManagerUVE: GLFW did not confirm window mode transition to {}",
                  Platform::GetWindowModeNameUVE(mode));
        return;
    }
    m_impl->windowMode = mode;
    m_impl->fullscreen = IsFullscreenWindowModeUVE(mode);
}

Platform::WindowModeUVE WindowManagerUVE::GetWindowModeUVE() const noexcept {
    return m_impl->windowMode;
}

void WindowManagerUVE::SetWindowTitleUVE(const std::string_view title) {
    if (!m_impl->valid || title.empty() || title.size() > kMaximumWindowTitleBytesUVE ||
        title.find('\0') != std::string_view::npos) {
        return;
    }
    const std::string ownedTitle{title};
    glfwSetWindowTitle(m_impl->window, ownedTitle.c_str());
}

bool WindowManagerUVE::IsFocusedUVE() const noexcept {
    return m_impl->valid && glfwGetWindowAttrib(m_impl->window, GLFW_FOCUSED) == GLFW_TRUE;
}

void WindowManagerUVE::GetContentScaleUVE(float& outX, float& outY) const noexcept {
    if (const std::optional<float> overrideScale = ResolveContentScaleOverrideUVE(m_impl->contentScaleOverride)) {
        outX = *overrideScale;
        outY = *overrideScale;
        return;
    }
    outX = 1.0F;
    outY = 1.0F;
    if (m_impl->valid && m_impl->highDpiAware) {
        glfwGetWindowContentScale(m_impl->window, &outX, &outY);
        const ContentScaleUVE nativeScale = SanitizeNativeContentScaleUVE(outX, outY);
        outX = nativeScale.x;
        outY = nativeScale.y;
    }
}

void WindowManagerUVE::SetDisplaySleepAllowedUVE(const bool allowed) noexcept {
    m_impl->displaySleepAllowed = allowed;
#if defined(_WIN32)
    const EXECUTION_STATE requestedState = allowed ? ES_CONTINUOUS : (ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
    if (SetThreadExecutionState(requestedState) == 0) {
        UVE_WARNING("WindowManagerUVE: Windows rejected the display power request");
    }
#elif defined(__APPLE__)
    if (allowed) {
        if (m_impl->displaySleepAssertion != kIOPMNullAssertionID) {
            const IOReturn result = IOPMAssertionRelease(m_impl->displaySleepAssertion);
            if (result == kIOReturnSuccess) {
                m_impl->displaySleepAssertion = kIOPMNullAssertionID;
            } else {
                UVE_WARNING("WindowManagerUVE: macOS could not release display-sleep assertion ({})",
                            static_cast<int>(result));
            }
        }
        m_impl->displaySleepWarningLogged = false;
        return;
    }
    if (m_impl->displaySleepAssertion == kIOPMNullAssertionID) {
        const IOReturn result = IOPMAssertionCreateWithName(
            kIOPMAssertionTypePreventUserIdleDisplaySleep, kIOPMAssertionLevelOn,
            CFSTR("UniVex Engine is keeping the display active"), &m_impl->displaySleepAssertion);
        if (result != kIOReturnSuccess || m_impl->displaySleepAssertion == kIOPMNullAssertionID) {
            m_impl->displaySleepAssertion = kIOPMNullAssertionID;
            if (!m_impl->displaySleepWarningLogged) {
                UVE_WARNING("WindowManagerUVE: macOS could not prevent idle display sleep ({})",
                            static_cast<int>(result));
                m_impl->displaySleepWarningLogged = true;
            }
        } else {
            m_impl->displaySleepWarningLogged = false;
        }
    }
#elif defined(__linux__) && defined(UVE_HAS_SYSTEMD)
    if (allowed) {
        if (m_impl->displaySleepInhibitorFd >= 0) {
            static_cast<void>(::close(m_impl->displaySleepInhibitorFd));
            m_impl->displaySleepInhibitorFd = -1;
        }
        m_impl->displaySleepWarningLogged = false;
        return;
    }
    if (m_impl->displaySleepInhibitorFd >= 0) {
        return;
    }

    sd_bus* bus = nullptr;
    sd_bus_message* reply = nullptr;
    sd_bus_error error{}; // Zero-initialized SD_BUS_ERROR_NULL without its C compound-literal macro.
    int inhibitorFd = -1;
    int result = sd_bus_open_system(&bus);
    if (result >= 0) {
        result = sd_bus_call_method(bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                                    "org.freedesktop.login1.Manager", "Inhibit", &error, &reply,
                                    "ssss", "idle", "UniVex Engine",
                                    "Application requests an active display", "block");
    }
    if (result >= 0) {
        result = sd_bus_message_read(reply, "h", &inhibitorFd);
    }
    if (result >= 0 && inhibitorFd >= 0) {
        // sd-bus owns the received descriptor through the reply message. Duplicate it before
        // releasing the message; logind drops the inhibition when our retained descriptor closes.
        m_impl->displaySleepInhibitorFd = ::fcntl(inhibitorFd, F_DUPFD_CLOEXEC, 3);
        if (m_impl->displaySleepInhibitorFd < 0) {
            result = -errno;
        }
    }
    if (m_impl->displaySleepInhibitorFd < 0) {
        if (!m_impl->displaySleepWarningLogged) {
            UVE_WARNING("WindowManagerUVE: Linux logind could not inhibit idle display sleep ({}: {})",
                        result, error.message != nullptr ? error.message : "no inhibitor descriptor returned");
            m_impl->displaySleepWarningLogged = true;
        }
    } else {
        m_impl->displaySleepWarningLogged = false;
    }
    if (reply != nullptr) {
        sd_bus_message_unref(reply);
    }
    if (bus != nullptr) {
        sd_bus_unref(bus);
    }
    sd_bus_error_free(&error);
#else
    if (allowed) {
        m_impl->displaySleepWarningLogged = false;
    } else if (!m_impl->displaySleepWarningLogged) {
        UVE_WARNING("WindowManagerUVE: this platform has no native display-sleep inhibitor");
        m_impl->displaySleepWarningLogged = true;
    }
#endif
}

std::uint32_t WindowManagerUVE::GetWidthUVE() const noexcept {
    if (!m_impl->valid) {
        return 0;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_impl->window, &width, &height);
    std::uint32_t framebufferWidth = 0U;
    std::uint32_t framebufferHeight = 0U;
    return ValidateFramebufferSizeUVE(width, height, framebufferWidth, framebufferHeight)
               ? framebufferWidth
               : 0U;
}

std::uint32_t WindowManagerUVE::GetHeightUVE() const noexcept {
    if (!m_impl->valid) {
        return 0;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_impl->window, &width, &height);
    std::uint32_t framebufferWidth = 0U;
    std::uint32_t framebufferHeight = 0U;
    return ValidateFramebufferSizeUVE(width, height, framebufferWidth, framebufferHeight)
               ? framebufferHeight
               : 0U;
}

std::vector<MonitorInfoUVE> WindowManagerUVE::EnumerateMonitorsUVE() const {
    if (!m_impl->valid) {
        return {};
    }
    std::vector<MonitorInfoUVE> monitors;
    int monitorCount = 0;
    GLFWmonitor** const glfwMonitors = glfwGetMonitors(&monitorCount);
    if (monitorCount < 0 || (monitorCount > 0 && glfwMonitors == nullptr)) {
        return {};
    }
    if (monitorCount > static_cast<int>(kMaximumMonitorSnapshotEntriesUVE)) {
        UVE_ERROR("WindowManagerUVE: GLFW returned {} monitors, exceeding the {}-entry snapshot cap",
                  monitorCount, kMaximumMonitorSnapshotEntriesUVE);
        return {};
    }
    GLFWmonitor* const primaryMonitor = glfwGetPrimaryMonitor();

    monitors.reserve(static_cast<std::size_t>(monitorCount));
    for (int index = 0; index < monitorCount; ++index) {
        GLFWmonitor* const monitor = glfwMonitors[index];
        const GLFWvidmode* const mode = glfwGetVideoMode(monitor);
        if (mode == nullptr) {
            continue; // Skip a monitor GLFW couldn't query a mode for rather than crashing.
        }
        std::uint32_t monitorWidth = 0U;
        std::uint32_t monitorHeight = 0U;
        if (!ValidateMonitorDimensionsUVE(mode->width, mode->height, monitorWidth, monitorHeight)) {
            UVE_ERROR("WindowManagerUVE: GLFW returned invalid dimensions for monitor {}x{}", mode->width,
                      mode->height);
            return {};
        }
        const char* const name = glfwGetMonitorName(monitor);
        MonitorInfoUVE info;
        info.name = name != nullptr ? name : "";
        info.width = monitorWidth;
        info.height = monitorHeight;
        info.isPrimary = monitor == primaryMonitor;
        info.refreshRate = mode->refreshRate > 0 ? static_cast<std::uint32_t>(mode->refreshRate) : 0U;
        glfwGetMonitorPos(monitor, &info.x, &info.y);
        glfwGetMonitorContentScale(monitor, &info.contentScaleX, &info.contentScaleY);
        monitors.push_back(std::move(info));
    }
    if (!ValidateMonitorSnapshotUVE(monitors)) {
        UVE_ERROR("WindowManagerUVE: GLFW returned an invalid monitor snapshot");
        return {};
    }
    return monitors;
}

void* WindowManagerUVE::GetNativeWindowHandleUVE() const noexcept {
    return m_impl->valid ? static_cast<void*>(m_impl->window) : nullptr;
}

std::string_view WindowManagerUVE::GetBackendNameUVE() const noexcept {
    return "GLFW3";
}

// ---------------------------------------------------------------------------
// IVulkanWindowSurfaceUVE — GLFW WSI forwarding.
// ---------------------------------------------------------------------------
// glfw3.h is included above in GLFW_INCLUDE_NONE mode (the GL device owns GL header inclusion,
// per the audit's #37). That mode still declares glfwGetRequiredInstanceExtensions() but NOT
// glfwCreateWindowSurface(), which is gated on GLFW_INCLUDE_VULKAN having seen Vulkan header
// types. In the spirit of the confinement boundary — Vulkan SDK headers stay out of the window
// module entirely — the entry point is redeclared here in a C-ABI-exact opaque form:
//   VkInstance (dispatchable handle)          == void*
//   VkSurfaceKHR (non-dispatchable, 64-bit)   == unsigned long long
//   const VkAllocationCallbacks*              == const void*
//   VkResult                                  == int
// These match the platform ABI of the real declaration; the linker binds by name.
extern "C" {
GLFWAPI int glfwCreateWindowSurface(void* /*VkInstance*/ instance,
    GLFWwindow* window, const void* /*VkAllocationCallbacks*/ allocator,
    unsigned long long* /*VkSurfaceKHR*/ surface);
} // extern "C"

std::vector<const char*> WindowManagerUVE::GetRequiredVulkanInstanceExtensionsUVE() const {
    std::vector<const char*> extensions;
    if (m_impl->window == nullptr) {
        return extensions;
    }
    std::uint32_t count = 0;
    // GLFW handles the null-platform case by returning 0 with an error logged internally —
    // an empty vector remains the consumer's "capability unavailable" signal.
    const char** names = glfwGetRequiredInstanceExtensions(&count);
    if (names == nullptr || count == 0U) {
        return extensions;
    }
    extensions.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        if (names[index] != nullptr) {
            extensions.push_back(names[index]);
        }
    }
    return extensions;
}

std::uintptr_t WindowManagerUVE::CreateVulkanWindowSurfaceUVE(const std::uintptr_t vulkanInstance) {
    if (m_impl->window == nullptr || vulkanInstance == 0U) {
        return 0U;
    }
    unsigned long long surface = 0ULL; // VkSurfaceKHR bits as sized in the contract typedef.
    const int result = glfwCreateWindowSurface(reinterpret_cast<void*>(vulkanInstance),
        m_impl->window, nullptr, &surface);
    if (result != 0 /*VK_SUCCESS*/ || surface == 0ULL) { // NOLINT(hicpp-signed-bitwise)
        // VK_SUCCESS is definitively 0 per the Vulkan specification — never dependent on header
        // availability, so the literal is documented rather than pulled from vulkan_core.h.
        return 0U;
    }
    return static_cast<std::uintptr_t>(surface);
}

void WindowManagerUVE::DestroyVulkanWindowSurfaceUVE(const std::uintptr_t /*vulkanInstance*/,
    const std::uintptr_t /*vulkanSurface*/) {
    // No-op by design: GLFW hands surface destruction back to vkDestroySurfaceKHR, which the
    // Vulkan render device calls through its own function table. The window module cannot own
    // that call because it deliberately never links against (or even includes) Vulkan.
}

void WindowManagerUVE::GetVulkanFramebufferSizeUVE(std::uint32_t& outWidth,
    std::uint32_t& outHeight) const {
    outWidth = 0U;
    outHeight = 0U;
    if (m_impl->window == nullptr) {
        return;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_impl->window, &width, &height);
    // Same validation rule as the GL world: clamps stay at zero until a real size reports.
    if (!ValidateFramebufferSizeUVE(width, height, outWidth, outHeight)) {
        outWidth = 0U;
        outHeight = 0U;
    }
}

Platform::VSyncModeUVE WindowManagerUVE::GetRequestedVSyncModeUVE() const noexcept {
    return m_impl->requestedVsyncMode;
}

bool WindowManagerUVE::IsTransparentFramebufferRequestedUVE() const noexcept {
    return m_impl->transparent;
}

} // namespace UVE::Window
