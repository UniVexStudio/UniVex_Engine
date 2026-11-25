#include "android_window_manager.h"
#include <array>
#include <android/native_window.h>
#include <EGL/egl.h>
#include "i_event_system.h"
#include "logging_macros.h"
#include "window_events.h"

namespace UVE::Window {
namespace {

/// One candidate for the surface and context, most specific first.
///
/// The strict request - 8/8/8/8 colour, a 24-bit depth buffer and an 8-bit stencil buffer, for both
/// a window and a pbuffer surface - is what nearly every device answers, and it is what the engine
/// wants: the pbuffer is how a destroyed surface is survived, and the depth/stencil pair is what
/// the 3D passes describe. A few drivers cannot satisfy all of it at once, and a driver that cannot
/// is not a driver the engine has to refuse: it answers EGL with zero configurations, the surface is
/// never created, and the app shows a black window with nothing in it.
///
/// Each entry below drops one requirement. The depth buffer stays the longest, because the 3D
/// renderer needs one; only the last resort trades 24 bits for 16.
struct EglConfigAttempt {
    const char* label;
    EGLint redSize;
    EGLint greenSize;
    EGLint blueSize;
    EGLint alphaSize;
    EGLint depthSize;
    EGLint stencilSize;
};

constexpr std::array<EglConfigAttempt, 4> kEglConfigAttempts{{
    {"rgba8 depth24 stencil8", 8, 8, 8, 8, 24, 8},
    {"rgba8 depth24", 8, 8, 8, 8, 24, 0},
    {"rgb8 depth24", 8, 8, 8, 0, 24, 0},
    {"rgb8 depth16", 8, 8, 8, 0, 16, 0},
}};

[[nodiscard]] EGLConfig ChooseEglConfig(const EGLDisplay display, const EglConfigAttempt& attempt) {
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                                 EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                 EGL_RED_SIZE, attempt.redSize, EGL_GREEN_SIZE, attempt.greenSize,
                                 EGL_BLUE_SIZE, attempt.blueSize, EGL_ALPHA_SIZE, attempt.alphaSize,
                                 EGL_DEPTH_SIZE, attempt.depthSize, EGL_STENCIL_SIZE, attempt.stencilSize,
                                 EGL_NONE};
    EGLint count = 0;
    EGLConfig config = nullptr;
    if (eglChooseConfig(display, attributes, &config, 1, &count) == EGL_TRUE && count > 0) {
        return config;
    }
    return nullptr;
}

} // namespace
struct AndroidWindowManager::Impl {
    Impl(Events::IEventSystem& events, ANativeWindow* window, bool vsync)
        : m_events(events), m_window(window), m_vsync(vsync) {}
    Events::IEventSystem& m_events;
    bool m_focused = true;
    std::uint32_t m_lastWidth = 0U;
    std::uint32_t m_lastHeight = 0U;
    ANativeWindow* m_window = nullptr;
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_surface = EGL_NO_SURFACE;
    EGLSurface m_idleSurface = EGL_NO_SURFACE;
    EGLConfig m_config = nullptr;
    bool m_vsync = true;
    bool m_contextLost = false;
    std::uint64_t m_presentations = 0U;
};
AndroidWindowManager::AndroidWindowManager(Events::IEventSystem& events, void* native,
                                               const WindowDesc& desc)
    : m_impl(std::make_unique<Impl>(events, static_cast<ANativeWindow*>(native),
          desc.vsyncModeExplicit ? desc.vsyncMode != Platform::VSyncMode::Off : desc.vsyncEnabled)) {
    if (m_impl->m_window == nullptr) return;
    ANativeWindow_acquire(m_impl->m_window);
    m_impl->m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_impl->m_display == EGL_NO_DISPLAY || eglInitialize(m_impl->m_display, nullptr, nullptr) != EGL_TRUE) return;
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) return;
    for (const EglConfigAttempt& attempt : kEglConfigAttempts) {
        m_impl->m_config = ChooseEglConfig(m_impl->m_display, attempt);
        if (m_impl->m_config != nullptr) {
            UVE_INFO("AndroidWindowManager: EGL config {}", attempt.label);
            break;
        }
        UVE_WARNING("AndroidWindowManager: no EGL config for {}", attempt.label);
    }
    if (m_impl->m_config == nullptr) {
        // Nothing matched: say so, because the only other symptom is a black window.
        UVE_ERROR("AndroidWindowManager: no usable EGL config (EGL error 0x{:X}); the window will "
                  "stay black", static_cast<unsigned>(eglGetError()));
        return;
    }
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    m_impl->m_context = eglCreateContext(m_impl->m_display, m_impl->m_config, EGL_NO_CONTEXT, contextAttributes);
    if (m_impl->m_context == EGL_NO_CONTEXT) return;
    const EGLint idleAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    m_impl->m_idleSurface = eglCreatePbufferSurface(m_impl->m_display, m_impl->m_config, idleAttributes);
    if (m_impl->m_idleSurface == EGL_NO_SURFACE || !TryRecoverSurface()) m_impl->m_contextLost = true;
}
AndroidWindowManager::~AndroidWindowManager() {
    if (m_impl->m_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_impl->m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_impl->m_surface != EGL_NO_SURFACE) eglDestroySurface(m_impl->m_display, m_impl->m_surface);
        if (m_impl->m_idleSurface != EGL_NO_SURFACE) eglDestroySurface(m_impl->m_display, m_impl->m_idleSurface);
        if (m_impl->m_context != EGL_NO_CONTEXT) eglDestroyContext(m_impl->m_display, m_impl->m_context);
        eglTerminate(m_impl->m_display);
    }
    if (m_impl->m_window != nullptr) ANativeWindow_release(m_impl->m_window);
}
bool AndroidWindowManager::TryRecoverSurface() noexcept {
    if (!IsValid() || m_impl->m_display == EGL_NO_DISPLAY || m_impl->m_window == nullptr) return false;
    if (m_impl->m_surface != EGL_NO_SURFACE) return true;
    EGLint format = 0;
    if (eglGetConfigAttrib(m_impl->m_display, m_impl->m_config, EGL_NATIVE_VISUAL_ID, &format) != EGL_TRUE) return false;
    if (ANativeWindow_setBuffersGeometry(m_impl->m_window, 0, 0, format) != 0) return false;
    m_impl->m_surface = eglCreateWindowSurface(m_impl->m_display, m_impl->m_config, m_impl->m_window, nullptr);
    if (m_impl->m_surface == EGL_NO_SURFACE) return false;
    if (eglMakeCurrent(m_impl->m_display, m_impl->m_surface, m_impl->m_surface, m_impl->m_context) != EGL_TRUE) {
        m_impl->m_contextLost = eglGetError() == EGL_CONTEXT_LOST;
        eglDestroySurface(m_impl->m_display, m_impl->m_surface);
        m_impl->m_surface = EGL_NO_SURFACE;
        return false;
    }
    eglSwapInterval(m_impl->m_display, m_impl->m_vsync ? 1 : 0);
    return true;
}
void AndroidWindowManager::PollEvents() {
    const auto width = GetWidth();
    const auto height = GetHeight();
    if (width != m_impl->m_lastWidth || height != m_impl->m_lastHeight) {
        m_impl->m_lastWidth = width; m_impl->m_lastHeight = height;
        m_impl->m_events.Publish(WindowResizedEvent{width, height});
    }
}
void AndroidWindowManager::SetFocused(const bool focused) {
    if (m_impl->m_focused == focused) return;
    m_impl->m_focused = focused;
    m_impl->m_events.Publish(WindowFocusChangedEvent{focused});
}
void AndroidWindowManager::DetachNativeWindow() {
    if (m_impl->m_display != EGL_NO_DISPLAY && m_impl->m_context != EGL_NO_CONTEXT && !m_impl->m_contextLost) {
        if (eglMakeCurrent(m_impl->m_display, m_impl->m_idleSurface, m_impl->m_idleSurface, m_impl->m_context) != EGL_TRUE) m_impl->m_contextLost = true;
    }
    if (m_impl->m_surface != EGL_NO_SURFACE) eglDestroySurface(m_impl->m_display, m_impl->m_surface);
    m_impl->m_surface = EGL_NO_SURFACE;
    if (m_impl->m_window != nullptr) ANativeWindow_release(m_impl->m_window);
    m_impl->m_window = nullptr;
}
bool AndroidWindowManager::AttachNativeWindow(void* window) {
    DetachNativeWindow();
    m_impl->m_window = static_cast<ANativeWindow*>(window);
    if (m_impl->m_window != nullptr) ANativeWindow_acquire(m_impl->m_window);
    return TryRecoverSurface();
}
void AndroidWindowManager::SwapBuffers() {
    if (m_impl->m_surface == EGL_NO_SURFACE || !IsValid()) return;
    if (eglSwapBuffers(m_impl->m_display, m_impl->m_surface) == EGL_TRUE) {
        ++m_impl->m_presentations;
        return;
    }
    m_impl->m_contextLost = eglGetError() == EGL_CONTEXT_LOST;
    eglMakeCurrent(m_impl->m_display, m_impl->m_idleSurface, m_impl->m_idleSurface, m_impl->m_context);
    eglDestroySurface(m_impl->m_display, m_impl->m_surface);
    m_impl->m_surface = EGL_NO_SURFACE;
}
void AndroidWindowManager::SetVSyncEnabled(const bool enabled) {
    if (m_impl->m_display != EGL_NO_DISPLAY && eglSwapInterval(m_impl->m_display, enabled ? 1 : 0) == EGL_TRUE) m_impl->m_vsync = enabled;
}
std::uint32_t AndroidWindowManager::GetWidth() const noexcept {
    EGLint value = 0;
    if (m_impl->m_surface == EGL_NO_SURFACE || eglQuerySurface(m_impl->m_display, m_impl->m_surface, EGL_WIDTH, &value) != EGL_TRUE) return 0U;
    return value > 0 ? static_cast<std::uint32_t>(value) : 0U;
}
std::uint32_t AndroidWindowManager::GetHeight() const noexcept {
    EGLint value = 0;
    if (m_impl->m_surface == EGL_NO_SURFACE || eglQuerySurface(m_impl->m_display, m_impl->m_surface, EGL_HEIGHT, &value) != EGL_TRUE) return 0U;
    return value > 0 ? static_cast<std::uint32_t>(value) : 0U;
}
}

namespace UVE::Window {
bool AndroidWindowManager::IsValid() const noexcept {
    return m_impl->m_context != EGL_NO_CONTEXT && !m_impl->m_contextLost;
}
bool AndroidWindowManager::IsFocused() const noexcept { return m_impl->m_focused; }
bool AndroidWindowManager::IsVSyncEnabled() const noexcept { return m_impl->m_vsync; }
void* AndroidWindowManager::GetNativeWindowHandle() const noexcept {
    return IsValid() ? m_impl->m_window : nullptr;
}
}

namespace UVE::Window {
std::uint64_t AndroidWindowManager::GetPresentationCount() const noexcept {
    return m_impl->m_presentations;
}
}
