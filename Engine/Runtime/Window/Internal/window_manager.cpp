// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "window_manager.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
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

#include "logging_macros.h"
#include "key_code.h"
#include "mouse_button.h"
#include "window_events.h"
#include "content_scale_policy.h"
#include "cursor_policy.h"
#include "monitor_selection_policy.h"
#include "window_mode_policy.h"
#include "vsync_policy.h"
#include "monitor_info_validation.h"
#include "window_desc_validation.h"
#include "window_size_policy.h"
#include "window_style_policy.h"
#include "framebuffer_size_validation.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace UVE::Window {

namespace {

/// Process-wide GLFW init refcount: only the first live WindowManager calls glfwInit(), only
/// the last one destroyed calls glfwTerminate(). Correct even though this codebase only ever
/// constructs one WindowManager at a time today — matches how a shared library-level resource
/// should be initialized exactly once regardless of how many owners reference it.
int g_glfwRefCount = 0;
bool g_suppressGlfwErrors = false;
std::string g_lastSuppressedGlfwError;

void GlfwErrorCallback(int errorCode, const char* description) {
    if (g_suppressGlfwErrors) {
        g_lastSuppressedGlfwError = description != nullptr ? description : "(no description)";
        return;
    }
    UVE_ERROR("WindowManager: GLFW error {}: {}", errorCode,
              description != nullptr ? description : "(no description)");
}

[[nodiscard]] GLFWmonitor* FindMonitor(const std::string_view requestedName) {
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
        const std::optional<std::size_t> selectedIndex = FindMonitorIndex(requestedName, monitorNames);
        if (selectedIndex.has_value()) {
            return monitors[*selectedIndex];
        }
    }
    UVE_WARNING("WindowManager: configured monitor \"{}\" was not found; using the primary monitor",
                requestedName);
    return glfwGetPrimaryMonitor();
}

} // namespace

struct WindowManager::Impl {
    Events::IEventSystem* eventSystem;
    WindowDesc desc;
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
    Platform::VSyncMode requestedVsyncMode = Platform::VSyncMode::On;
    Platform::VSyncMode effectiveVsyncMode = Platform::VSyncMode::On;
    Platform::WindowMode windowMode = Platform::WindowMode::Windowed;
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
    Input::IInputSystem* inputSystem = nullptr;
    float scrollDelta = 0.0F;
    int windowedX = 0;
    int windowedY = 0;
    int windowedWidth = 0;
    int windowedHeight = 0;

    explicit Impl(Events::IEventSystem& eventSystemIn, const WindowDesc& descIn)
        : eventSystem(&eventSystemIn), desc(descIn), requestedVsyncMode(descIn.vsyncMode),
          effectiveVsyncMode(descIn.vsyncMode), windowMode(descIn.mode), transparent(descIn.transparent),
          highDpiAware(descIn.highDpiAware), displaySleepAllowed(descIn.allowDisplaySleep),
          contentScaleOverride(descIn.contentScaleOverride) {}

    void ApplyVSync(Platform::VSyncMode requestedMode);
    void ApplyCursorInputMode();

    static Impl& FromWindow(GLFWwindow* glfwWindow) {
        return *static_cast<Impl*>(glfwGetWindowUserPointer(glfwWindow));
    }

    static void CloseCallback(GLFWwindow* glfwWindow) {
        glfwSetWindowShouldClose(glfwWindow, GLFW_TRUE);
        FromWindow(glfwWindow).eventSystem->Publish(WindowCloseRequestedEvent{});
    }

    static void FramebufferSizeCallback(GLFWwindow* glfwWindow, int width, int height) {
        std::uint32_t framebufferWidth = 0U;
        std::uint32_t framebufferHeight = 0U;
        if (!ValidateFramebufferSize(width, height, framebufferWidth, framebufferHeight)) {
            return;
        }
        // Publishes only — never touches GL state here. GlRenderDevice polls the window's
        // current size once per frame instead, per the approved design.
        FromWindow(glfwWindow).eventSystem->Publish(
            WindowResizedEvent{framebufferWidth, framebufferHeight});
    }

    static void FocusCallback(GLFWwindow* glfwWindow, int focused) {
        Impl& impl = FromWindow(glfwWindow);
        impl.focused = focused == GLFW_TRUE;
        impl.eventSystem->Publish(WindowFocusChangedEvent{impl.focused});
    }

    static void ScrollCallback(GLFWwindow* glfwWindow, double /*xOffset*/, double yOffset) {
        FromWindow(glfwWindow).scrollDelta += static_cast<float>(yOffset);
    }

    static int ToGlfwKey(const Input::KeyCode key) noexcept {
        using Input::KeyCode;
        const int value = static_cast<int>(key);
        if (value >= static_cast<int>(KeyCode::A) && value <= static_cast<int>(KeyCode::Z)) {
            return GLFW_KEY_A + value - static_cast<int>(KeyCode::A);
        }
        if (value >= static_cast<int>(KeyCode::Num0) && value <= static_cast<int>(KeyCode::Num9)) {
            return GLFW_KEY_0 + value - static_cast<int>(KeyCode::Num0);
        }
        if (value >= static_cast<int>(KeyCode::F1) && value <= static_cast<int>(KeyCode::F12)) {
            return GLFW_KEY_F1 + value - static_cast<int>(KeyCode::F1);
        }
        switch (key) {
        case KeyCode::Up: return GLFW_KEY_UP;
        case KeyCode::Down: return GLFW_KEY_DOWN;
        case KeyCode::Left: return GLFW_KEY_LEFT;
        case KeyCode::Right: return GLFW_KEY_RIGHT;
        case KeyCode::Space: return GLFW_KEY_SPACE;
        case KeyCode::Enter: return GLFW_KEY_ENTER;
        case KeyCode::Escape: return GLFW_KEY_ESCAPE;
        case KeyCode::Tab: return GLFW_KEY_TAB;
        case KeyCode::Backspace: return GLFW_KEY_BACKSPACE;
        case KeyCode::LeftShift: return GLFW_KEY_LEFT_SHIFT;
        case KeyCode::RightShift: return GLFW_KEY_RIGHT_SHIFT;
        case KeyCode::LeftCtrl: return GLFW_KEY_LEFT_CONTROL;
        case KeyCode::RightCtrl: return GLFW_KEY_RIGHT_CONTROL;
        case KeyCode::LeftAlt: return GLFW_KEY_LEFT_ALT;
        case KeyCode::RightAlt: return GLFW_KEY_RIGHT_ALT;
        default: return GLFW_KEY_UNKNOWN;
        }
    }

    static int ToGlfwMouseButton(const Input::MouseButton button) noexcept {
        switch (button) {
        case Input::MouseButton::Left: return GLFW_MOUSE_BUTTON_LEFT;
        case Input::MouseButton::Right: return GLFW_MOUSE_BUTTON_RIGHT;
        case Input::MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
        default: return -1;
        }
    }

    void PollInput() noexcept {
        if (inputSystem == nullptr || window == nullptr) {
            return;
        }
        for (int value = static_cast<int>(Input::KeyCode::A);
             value < static_cast<int>(Input::KeyCode::Count); ++value) {
            const auto key = static_cast<Input::KeyCode>(value);
            const int glfwKey = ToGlfwKey(key);
            if (glfwKey != GLFW_KEY_UNKNOWN) {
                inputSystem->SetKeyState(key, glfwGetKey(window, glfwKey) != GLFW_RELEASE);
            }
        }
        for (int value = 0; value < static_cast<int>(Input::MouseButton::Count); ++value) {
            const auto button = static_cast<Input::MouseButton>(value);
            const int glfwButton = ToGlfwMouseButton(button);
            inputSystem->SetMouseButtonState(
                button, glfwGetMouseButton(window, glfwButton) != GLFW_RELEASE);
        }
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        inputSystem->SetMousePosition(
            Math::Vector2{static_cast<float>(cursorX), static_cast<float>(cursorY)});
        inputSystem->SetMouseScrollDelta(scrollDelta);
        scrollDelta = 0.0F;
    }
};

void WindowManager::Impl::ApplyVSync(const Platform::VSyncMode requestedMode) {
    requestedVsyncMode = requestedMode;
    bool adaptiveIntervalSupported = false;
    if (requestedMode == Platform::VSyncMode::Adaptive) {
        adaptiveIntervalSupported = glfwExtensionSupported("WGL_EXT_swap_control_tear") == GLFW_TRUE ||
                                   glfwExtensionSupported("GLX_EXT_swap_control_tear") == GLFW_TRUE ||
                                   glfwExtensionSupported("EGL_EXT_swap_control_tear") == GLFW_TRUE;
    }
    const OpenGlVSyncResolution resolution =
        ResolveOpenGlVSync(requestedMode, adaptiveIntervalSupported);
    if (requestedMode == Platform::VSyncMode::Adaptive &&
        resolution.effectiveMode != Platform::VSyncMode::Adaptive) {
        UVE_WARNING("WindowManager: adaptive V-sync is unavailable; falling back to On");
    } else if (requestedMode == Platform::VSyncMode::Mailbox) {
        // OpenGL/GLFW exposes a swap interval, not a mailbox queue policy. Vulkan applies Mailbox
        // when the surface advertises it.
        UVE_WARNING("WindowManager: OpenGL does not expose Mailbox present mode; falling back to Off");
    }
    glfwSwapInterval(resolution.swapInterval);
    effectiveVsyncMode = resolution.effectiveMode;
    vsyncEnabled = resolution.effectiveMode != Platform::VSyncMode::Off;
}

void WindowManager::Impl::ApplyCursorInputMode() {
    if (window == nullptr) {
        return;
    }
    const CursorInputResolution resolution =
        ResolveCursorInputPolicy(desc.cursorVisible, desc.cursorConfinedToWindow);
    int glfwCursorMode = GLFW_CURSOR_NORMAL;
    switch (resolution.mode) {
    case CursorInputMode::Normal:
        glfwCursorMode = GLFW_CURSOR_NORMAL;
        break;
    case CursorInputMode::Hidden:
        glfwCursorMode = GLFW_CURSOR_HIDDEN;
        break;
    case CursorInputMode::Confined:
        // GLFW's portable disabled mode captures and hides the pointer.
        glfwCursorMode = GLFW_CURSOR_DISABLED;
        break;
    }
    if (resolution.visibilityCompromised) {
        UVE_WARNING("WindowManager: GLFW cannot confine a visible cursor; confinement hides it");
    }
    glfwSetInputMode(window, GLFW_CURSOR, glfwCursorMode);
    glfwSetCursor(window, resolution.useCustomCursor ? cursor : nullptr);
}

WindowManager::WindowManager(Events::IEventSystem& eventSystem, const WindowDesc& desc)
    : m_impl(std::make_unique<Impl>(eventSystem, desc)) {
    if (!ValidateWindowDesc(desc)) {
        UVE_ERROR("WindowManager: rejected invalid window descriptor before GLFW initialization");
        return;
    }
    if (g_glfwRefCount == 0) {
        glfwSetErrorCallback(&GlfwErrorCallback);
        if (glfwInit() != GLFW_TRUE) {
            UVE_FATAL("WindowManager: glfwInit() failed");
            return;
        }
    }
    ++g_glfwRefCount;
    m_impl->ownsGlfwRefCount = true;

    m_impl->targetMonitor = FindMonitor(desc.monitorName);
    const GLFWvidmode* const monitorMode = m_impl->targetMonitor != nullptr
                                               ? glfwGetVideoMode(m_impl->targetMonitor)
                                               : nullptr;
    int monitorX = 0;
    int monitorY = 0;
    if (m_impl->targetMonitor != nullptr) {
        glfwGetMonitorPos(m_impl->targetMonitor, &monitorX, &monitorY);
    }
    const Platform::WindowMode effectiveMode = ResolveInitialWindowMode(
        desc.mode, m_impl->targetMonitor != nullptr && monitorMode != nullptr);
    m_impl->windowMode = effectiveMode;
    if (effectiveMode != desc.mode) {
        UVE_WARNING("WindowManager: requested mode {} requires an available monitor video mode; using windowed",
                    Platform::GetWindowModeName(desc.mode));
    }

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    // On macOS, GLFW requires forward-compatible contexts for OpenGL 3.2+ Core Profile.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    // GLFW_OPENGL_FORWARD_COMPAT is deliberately NOT set on Linux/Windows: it changes platform-specific context
    // behavior and has previously broken X11 resize callback delivery on the Mesa test host.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    const bool initiallyDecorated = ShouldDecorateWindow(desc.borderless, effectiveMode);
    glfwWindowHint(GLFW_DECORATED, initiallyDecorated ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, desc.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, desc.transparent ? GLFW_TRUE : GLFW_FALSE);
#ifdef GLFW_SCALE_TO_MONITOR
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, desc.perMonitorScaling ? GLFW_TRUE : GLFW_FALSE);
#endif
#ifdef GLFW_COCOA_RETINA_FRAMEBUFFER
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, desc.highDpiAware ? GLFW_TRUE : GLFW_FALSE);
#endif

    const bool exclusive = effectiveMode == Platform::WindowMode::ExclusiveFullscreen;
    if (exclusive && monitorMode != nullptr) {
        glfwWindowHint(GLFW_REFRESH_RATE, monitorMode->refreshRate);
    }
    int createWidth = static_cast<int>(desc.width);
    int createHeight = static_cast<int>(desc.height);
    GLFWmonitor* createMonitor = exclusive ? m_impl->targetMonitor : nullptr;
    if ((effectiveMode == Platform::WindowMode::Fullscreen || exclusive) && monitorMode != nullptr) {
        createWidth = monitorMode->width;
        createHeight = monitorMode->height;
    }

    struct GlVersionCandidate {
        std::uint32_t major;
        std::uint32_t minor;
    };
    std::vector<GlVersionCandidate> candidates;
    candidates.push_back({desc.glVersionMajor, desc.glVersionMinor});

    constexpr std::array<GlVersionCandidate, 5> kFallbackList = {{
        {4, 6}, {4, 5}, {4, 3}, {4, 1}, {3, 3}
    }};
    for (const auto& candidate : kFallbackList) {
        if (candidate.major < desc.glVersionMajor ||
            (candidate.major == desc.glVersionMajor && candidate.minor < desc.glVersionMinor)) {
            candidates.push_back(candidate);
        }
    }

    g_suppressGlfwErrors = true;
    g_lastSuppressedGlfwError.clear();
    for (const auto& candidate : candidates) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, static_cast<int>(candidate.major));
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, static_cast<int>(candidate.minor));
        m_impl->window = glfwCreateWindow(createWidth, createHeight, desc.title.c_str(), createMonitor, nullptr);
        if (m_impl->window != nullptr) {
            if (candidate.major != desc.glVersionMajor || candidate.minor != desc.glVersionMinor) {
                UVE_INFO("WindowManager: requested OpenGL {}.{} Core Profile unavailable; successfully fell back to {}.{}",
                         desc.glVersionMajor, desc.glVersionMinor, candidate.major, candidate.minor);
                m_impl->desc.glVersionMajor = candidate.major;
                m_impl->desc.glVersionMinor = candidate.minor;
            }
            break;
        }
    }
    g_suppressGlfwErrors = false;

    if (m_impl->window == nullptr) {
        UVE_FATAL("WindowManager: glfwCreateWindow() failed requesting OpenGL {}.{} Core Profile (and fallbacks down to 3.3): {}",
                  desc.glVersionMajor, desc.glVersionMinor,
                  g_lastSuppressedGlfwError.empty() ? "unknown GLFW error" : g_lastSuppressedGlfwError);
        --g_glfwRefCount;
        m_impl->ownsGlfwRefCount = false;
        if (g_glfwRefCount == 0) {
            glfwTerminate();
        }
        return;
    }

    if (!exclusive && effectiveMode == Platform::WindowMode::Fullscreen && monitorMode != nullptr) {
        glfwSetWindowPos(m_impl->window, monitorX, monitorY);
    } else if (!exclusive && !desc.initialPositionSpecified && m_impl->targetMonitor != nullptr &&
               monitorMode != nullptr) {
        glfwSetWindowPos(m_impl->window,
                         monitorX + (monitorMode->width - createWidth) / 2,
                         monitorY + (monitorMode->height - createHeight) / 2);
    } else if (!exclusive && desc.initialPositionSpecified) {
        glfwSetWindowPos(m_impl->window, desc.initialPositionX, desc.initialPositionY);
    }
    const WindowSizeLimits sizeLimits = ResolveWindowSizeLimits(
        desc.minimumWidth, desc.minimumHeight, desc.maximumWidth, desc.maximumHeight, GLFW_DONT_CARE);
    glfwSetWindowSizeLimits(m_impl->window, sizeLimits.minimumWidth, sizeLimits.minimumHeight,
                            sizeLimits.maximumWidth, sizeLimits.maximumHeight);
    if (!exclusive && effectiveMode == Platform::WindowMode::Maximized) {
        // Preserve the normal geometry before the maximize call changes glfwGetWindowSize/Pos
        // to the monitor-sized presentation bounds.
        glfwGetWindowPos(m_impl->window, &m_impl->windowedX, &m_impl->windowedY);
        glfwGetWindowSize(m_impl->window, &m_impl->windowedWidth, &m_impl->windowedHeight);
        glfwMaximizeWindow(m_impl->window);
    }

    if (!desc.icons.empty()) {
        std::vector<GLFWimage> glfwIcons;
        glfwIcons.reserve(desc.icons.size());
        for (const WindowIcon& icon : desc.icons) {
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
            UVE_WARNING("WindowManager: GLFW rejected the configured cursor image; using the system cursor");
        }
    }

    glfwSetWindowUserPointer(m_impl->window, m_impl.get());
    glfwSetWindowCloseCallback(m_impl->window, &Impl::CloseCallback);
    glfwSetFramebufferSizeCallback(m_impl->window, &Impl::FramebufferSizeCallback);
    glfwSetWindowFocusCallback(m_impl->window, &Impl::FocusCallback);
    glfwSetScrollCallback(m_impl->window, &Impl::ScrollCallback);
    glfwSetWindowAttrib(m_impl->window, GLFW_FLOATING, desc.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
    m_impl->ApplyCursorInputMode();

    glfwMakeContextCurrent(m_impl->window);
    m_impl->ApplyVSync(desc.vsyncModeExplicit
                              ? desc.vsyncMode
                              : (desc.vsyncEnabled ? Platform::VSyncMode::On
                                                   : Platform::VSyncMode::Off));

    m_impl->fullscreen = exclusive || effectiveMode == Platform::WindowMode::Fullscreen;
    m_impl->focused = glfwGetWindowAttrib(m_impl->window, GLFW_FOCUSED) == GLFW_TRUE;
    if (!m_impl->fullscreen && effectiveMode != Platform::WindowMode::Maximized) {
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
    SetDisplaySleepAllowed(desc.allowDisplaySleep);
    UVE_INFO("WindowManager: created {}x{} window \"{}\" in mode {} using OpenGL {}.{} Core",
             createWidth, createHeight, desc.title, Platform::GetWindowModeName(m_impl->windowMode),
             m_impl->desc.glVersionMajor, m_impl->desc.glVersionMinor);
}

WindowManager::~WindowManager() {
    SetDisplaySleepAllowed(true);
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

bool WindowManager::IsValid() const noexcept {
    return m_impl->valid;
}

void WindowManager::AttachInputSystem(Input::IInputSystem* inputSystem) noexcept {
    m_impl->inputSystem = inputSystem;
}

void WindowManager::PollEvents() {
    if (m_impl->valid) {
        glfwPollEvents();
        m_impl->PollInput();
    }
}

void WindowManager::SwapBuffers() {
    if (m_impl->valid) {
        glfwSwapBuffers(m_impl->window);
    }
}

bool WindowManager::IsCloseRequested() const noexcept {
    return m_impl->valid && glfwWindowShouldClose(m_impl->window) == GLFW_TRUE;
}

void WindowManager::SetVSyncEnabled(const bool enabled) {
    SetVSyncMode(enabled ? Platform::VSyncMode::On : Platform::VSyncMode::Off);
}

bool WindowManager::IsVSyncEnabled() const noexcept {
    return m_impl->vsyncEnabled;
}

void WindowManager::SetVSyncMode(const Platform::VSyncMode mode) {
    if (Platform::GetVSyncModeName(mode).empty()) {
        UVE_WARNING("WindowManager: ignored invalid V-sync mode request");
        return;
    }
    m_impl->requestedVsyncMode = mode;
    if (m_impl->valid) {
        m_impl->ApplyVSync(mode);
    } else {
        m_impl->effectiveVsyncMode = mode;
        m_impl->vsyncEnabled = mode != Platform::VSyncMode::Off;
    }
}

Platform::VSyncMode WindowManager::GetVSyncMode() const noexcept {
    return m_impl->effectiveVsyncMode;
}

void WindowManager::SetFullscreen(const bool fullscreen) {
    SetWindowMode(fullscreen ? Platform::WindowMode::Fullscreen
                                : Platform::WindowMode::Windowed);
}

bool WindowManager::IsFullscreen() const noexcept {
    return m_impl->fullscreen;
}

void WindowManager::SetWindowMode(const Platform::WindowMode mode) {
    if (!m_impl->valid || Platform::GetWindowModeName(mode).empty() || mode == m_impl->windowMode) {
        return;
    }
    GLFWmonitor* monitor = m_impl->targetMonitor != nullptr ? m_impl->targetMonitor : glfwGetPrimaryMonitor();
    const GLFWvidmode* const videoMode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
    if ((mode == Platform::WindowMode::Fullscreen || mode == Platform::WindowMode::ExclusiveFullscreen) &&
        (monitor == nullptr || videoMode == nullptr)) {
        UVE_ERROR("WindowManager: cannot enter fullscreen without a monitor video mode");
        return;
    }

    const bool currentlyMaximized = m_impl->windowMode == Platform::WindowMode::Maximized ||
                                    glfwGetWindowAttrib(m_impl->window, GLFW_MAXIMIZED) == GLFW_TRUE;
    if (ShouldCaptureWindowedGeometry(m_impl->fullscreen, currentlyMaximized)) {
        glfwGetWindowPos(m_impl->window, &m_impl->windowedX, &m_impl->windowedY);
        glfwGetWindowSize(m_impl->window, &m_impl->windowedWidth, &m_impl->windowedHeight);
    }
    int monitorX = 0;
    int monitorY = 0;
    if (monitor != nullptr) {
        glfwGetMonitorPos(monitor, &monitorX, &monitorY);
    }

    bool transitionConfirmed = true;
    if (mode == Platform::WindowMode::ExclusiveFullscreen) {
        glfwSetWindowAttrib(m_impl->window, GLFW_DECORATED,
                            ShouldDecorateWindow(m_impl->desc.borderless, mode) ? GLFW_TRUE : GLFW_FALSE);
        glfwSetWindowMonitor(m_impl->window, monitor, 0, 0, videoMode->width, videoMode->height,
                             videoMode->refreshRate);
        transitionConfirmed = glfwGetWindowMonitor(m_impl->window) == monitor;
    } else if (mode == Platform::WindowMode::Fullscreen) {
        glfwSetWindowAttrib(m_impl->window, GLFW_DECORATED,
                            ShouldDecorateWindow(m_impl->desc.borderless, mode) ? GLFW_TRUE : GLFW_FALSE);
        glfwSetWindowMonitor(m_impl->window, nullptr, monitorX, monitorY, videoMode->width,
                             videoMode->height, GLFW_DONT_CARE);
        transitionConfirmed = glfwGetWindowMonitor(m_impl->window) == nullptr;
    } else {
        const bool decorated = ShouldDecorateWindow(m_impl->desc.borderless, mode);
        glfwSetWindowAttrib(m_impl->window, GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
        if (m_impl->fullscreen || glfwGetWindowMonitor(m_impl->window) != nullptr) {
            glfwSetWindowMonitor(m_impl->window, nullptr, m_impl->windowedX, m_impl->windowedY,
                                 m_impl->windowedWidth, m_impl->windowedHeight, GLFW_DONT_CARE);
        }
        if (mode == Platform::WindowMode::Maximized) {
            glfwMaximizeWindow(m_impl->window);
        } else {
            glfwRestoreWindow(m_impl->window);
            if (mode == Platform::WindowMode::Windowed || mode == Platform::WindowMode::Borderless) {
                glfwSetWindowPos(m_impl->window, m_impl->windowedX, m_impl->windowedY);
                glfwSetWindowSize(m_impl->window, m_impl->windowedWidth, m_impl->windowedHeight);
            }
        }
        transitionConfirmed = glfwGetWindowMonitor(m_impl->window) == nullptr;
    }

    if (!transitionConfirmed) {
        UVE_ERROR("WindowManager: GLFW did not confirm window mode transition to {}",
                  Platform::GetWindowModeName(mode));
        return;
    }
    m_impl->windowMode = mode;
    m_impl->fullscreen = IsFullscreenWindowMode(mode);
}

Platform::WindowMode WindowManager::GetWindowMode() const noexcept {
    return m_impl->windowMode;
}

void WindowManager::SetCursorVisible(const bool visible) {
    m_impl->desc.cursorVisible = visible;
    m_impl->desc.cursorConfinedToWindow = !visible;
    m_impl->ApplyCursorInputMode();
}

void WindowManager::SetWindowTitle(const std::string_view title) {
    if (!m_impl->valid || title.empty() || title.size() > kMaximumWindowTitleBytes ||
        title.find('\0') != std::string_view::npos) {
        return;
    }
    const std::string ownedTitle{title};
    glfwSetWindowTitle(m_impl->window, ownedTitle.c_str());
}

bool WindowManager::IsFocused() const noexcept {
    return m_impl->valid && glfwGetWindowAttrib(m_impl->window, GLFW_FOCUSED) == GLFW_TRUE;
}

void WindowManager::GetContentScale(float& outX, float& outY) const noexcept {
    if (const std::optional<float> overrideScale = ResolveContentScaleOverride(m_impl->contentScaleOverride)) {
        outX = *overrideScale;
        outY = *overrideScale;
        return;
    }
    outX = 1.0F;
    outY = 1.0F;
    if (m_impl->valid && m_impl->highDpiAware) {
        glfwGetWindowContentScale(m_impl->window, &outX, &outY);
        const ContentScale nativeScale = SanitizeNativeContentScale(outX, outY);
        outX = nativeScale.x;
        outY = nativeScale.y;
    }
}

void WindowManager::SetDisplaySleepAllowed(const bool allowed) noexcept {
    m_impl->displaySleepAllowed = allowed;
#if defined(_WIN32)
    const EXECUTION_STATE requestedState = allowed ? ES_CONTINUOUS : (ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
    if (SetThreadExecutionState(requestedState) == 0) {
        UVE_WARNING("WindowManager: Windows rejected the display power request");
    }
#elif defined(__APPLE__)
    if (allowed) {
        if (m_impl->displaySleepAssertion != kIOPMNullAssertionID) {
            const IOReturn result = IOPMAssertionRelease(m_impl->displaySleepAssertion);
            if (result == kIOReturnSuccess) {
                m_impl->displaySleepAssertion = kIOPMNullAssertionID;
            } else {
                UVE_WARNING("WindowManager: macOS could not release display-sleep assertion ({})",
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
                UVE_WARNING("WindowManager: macOS could not prevent idle display sleep ({})",
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
            UVE_WARNING("WindowManager: Linux logind could not inhibit idle display sleep ({}: {})",
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
        UVE_WARNING("WindowManager: this platform has no native display-sleep inhibitor");
        m_impl->displaySleepWarningLogged = true;
    }
#endif
}

std::uint32_t WindowManager::GetWidth() const noexcept {
    if (!m_impl->valid) {
        return 0;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_impl->window, &width, &height);
    std::uint32_t framebufferWidth = 0U;
    std::uint32_t framebufferHeight = 0U;
    return ValidateFramebufferSize(width, height, framebufferWidth, framebufferHeight)
               ? framebufferWidth
               : 0U;
}

std::uint32_t WindowManager::GetHeight() const noexcept {
    if (!m_impl->valid) {
        return 0;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_impl->window, &width, &height);
    std::uint32_t framebufferWidth = 0U;
    std::uint32_t framebufferHeight = 0U;
    return ValidateFramebufferSize(width, height, framebufferWidth, framebufferHeight)
               ? framebufferHeight
               : 0U;
}

std::vector<MonitorInfo> WindowManager::EnumerateMonitors() const {
    if (!m_impl->valid) {
        return {};
    }
    std::vector<MonitorInfo> monitors;
    int monitorCount = 0;
    GLFWmonitor** const glfwMonitors = glfwGetMonitors(&monitorCount);
    if (monitorCount < 0 || (monitorCount > 0 && glfwMonitors == nullptr)) {
        return {};
    }
    if (monitorCount > static_cast<int>(kMaximumMonitorSnapshotEntries)) {
        UVE_ERROR("WindowManager: GLFW returned {} monitors, exceeding the {}-entry snapshot cap",
                  monitorCount, kMaximumMonitorSnapshotEntries);
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
        if (!ValidateMonitorDimensions(mode->width, mode->height, monitorWidth, monitorHeight)) {
            UVE_ERROR("WindowManager: GLFW returned invalid dimensions for monitor {}x{}", mode->width,
                      mode->height);
            return {};
        }
        const char* const name = glfwGetMonitorName(monitor);
        MonitorInfo info;
        info.name = name != nullptr ? name : "";
        info.width = monitorWidth;
        info.height = monitorHeight;
        info.isPrimary = monitor == primaryMonitor;
        info.refreshRate = mode->refreshRate > 0 ? static_cast<std::uint32_t>(mode->refreshRate) : 0U;
        glfwGetMonitorPos(monitor, &info.x, &info.y);
        glfwGetMonitorContentScale(monitor, &info.contentScaleX, &info.contentScaleY);
        monitors.push_back(std::move(info));
    }
    if (!ValidateMonitorSnapshot(monitors)) {
        UVE_ERROR("WindowManager: GLFW returned an invalid monitor snapshot");
        return {};
    }
    return monitors;
}

void* WindowManager::GetNativeWindowHandle() const noexcept {
    return m_impl->valid ? static_cast<void*>(m_impl->window) : nullptr;
}

std::string_view WindowManager::GetBackendName() const noexcept {
    return "GLFW3";
}

// ---------------------------------------------------------------------------
// IVulkanWindowSurface — GLFW WSI forwarding.
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

std::vector<const char*> WindowManager::GetRequiredVulkanInstanceExtensions() const {
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

std::uintptr_t WindowManager::CreateVulkanWindowSurface(const std::uintptr_t vulkanInstance) {
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

void WindowManager::DestroyVulkanWindowSurface(const std::uintptr_t /*vulkanInstance*/,
    const std::uintptr_t /*vulkanSurface*/) {
    // No-op by design: GLFW hands surface destruction back to vkDestroySurfaceKHR, which the
    // Vulkan render device calls through its own function table. The window module cannot own
    // that call because it deliberately never links against (or even includes) Vulkan.
}

void WindowManager::GetVulkanFramebufferSize(std::uint32_t& outWidth,
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
    if (!ValidateFramebufferSize(width, height, outWidth, outHeight)) {
        outWidth = 0U;
        outHeight = 0U;
    }
}

Platform::VSyncMode WindowManager::GetRequestedVSyncMode() const noexcept {
    return m_impl->requestedVsyncMode;
}

bool WindowManager::IsTransparentFramebufferRequested() const noexcept {
    return m_impl->transparent;
}

} // namespace UVE::Window
