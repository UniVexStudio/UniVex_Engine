#include "uve/window/android_window_manager_uve.h"
#include <android/native_window.h>
#include <EGL/egl.h>
#include "uve/events/i_event_system_uve.h"
#include "uve/window/window_events_uve.h"
namespace UVE::Window {
struct AndroidWindowManagerUVE::Impl {
    Impl(Events::IEventSystemUVE& events, ANativeWindow* window, bool vsync)
        : m_events(events), m_window(window), m_vsync(vsync) {}
    Events::IEventSystemUVE& m_events;
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
AndroidWindowManagerUVE::AndroidWindowManagerUVE(Events::IEventSystemUVE& events, void* native,
                                               const WindowDescUVE& desc)
    : m_impl(std::make_unique<Impl>(events, static_cast<ANativeWindow*>(native),
          desc.vsyncModeExplicit ? desc.vsyncMode != Platform::VSyncModeUVE::Off : desc.vsyncEnabled)) {
    if (m_impl->m_window == nullptr) return;
    ANativeWindow_acquire(m_impl->m_window);
    m_impl->m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_impl->m_display == EGL_NO_DISPLAY || eglInitialize(m_impl->m_display, nullptr, nullptr) != EGL_TRUE) return;
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) return;
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLint count = 0;
    if (eglChooseConfig(m_impl->m_display, attributes, &m_impl->m_config, 1, &count) != EGL_TRUE || count == 0) return;
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    m_impl->m_context = eglCreateContext(m_impl->m_display, m_impl->m_config, EGL_NO_CONTEXT, contextAttributes);
    if (m_impl->m_context == EGL_NO_CONTEXT) return;
    const EGLint idleAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    m_impl->m_idleSurface = eglCreatePbufferSurface(m_impl->m_display, m_impl->m_config, idleAttributes);
    if (m_impl->m_idleSurface == EGL_NO_SURFACE || !TryRecoverSurfaceUVE()) m_impl->m_contextLost = true;
}
AndroidWindowManagerUVE::~AndroidWindowManagerUVE() {
    if (m_impl->m_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_impl->m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_impl->m_surface != EGL_NO_SURFACE) eglDestroySurface(m_impl->m_display, m_impl->m_surface);
        if (m_impl->m_idleSurface != EGL_NO_SURFACE) eglDestroySurface(m_impl->m_display, m_impl->m_idleSurface);
        if (m_impl->m_context != EGL_NO_CONTEXT) eglDestroyContext(m_impl->m_display, m_impl->m_context);
        eglTerminate(m_impl->m_display);
    }
    if (m_impl->m_window != nullptr) ANativeWindow_release(m_impl->m_window);
}
bool AndroidWindowManagerUVE::TryRecoverSurfaceUVE() noexcept {
    if (!IsValidUVE() || m_impl->m_display == EGL_NO_DISPLAY || m_impl->m_window == nullptr) return false;
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
void AndroidWindowManagerUVE::PollEventsUVE() {
    const auto width = GetWidthUVE();
    const auto height = GetHeightUVE();
    if (width != m_impl->m_lastWidth || height != m_impl->m_lastHeight) {
        m_impl->m_lastWidth = width; m_impl->m_lastHeight = height;
        m_impl->m_events.Publish(WindowResizedEventUVE{width, height});
    }
}
void AndroidWindowManagerUVE::SetFocusedUVE(const bool focused) {
    if (m_impl->m_focused == focused) return;
    m_impl->m_focused = focused;
    m_impl->m_events.Publish(WindowFocusChangedEventUVE{focused});
}
void AndroidWindowManagerUVE::DetachNativeWindowUVE() {
    if (m_impl->m_display != EGL_NO_DISPLAY && m_impl->m_context != EGL_NO_CONTEXT && !m_impl->m_contextLost) {
        if (eglMakeCurrent(m_impl->m_display, m_impl->m_idleSurface, m_impl->m_idleSurface, m_impl->m_context) != EGL_TRUE) m_impl->m_contextLost = true;
    }
    if (m_impl->m_surface != EGL_NO_SURFACE) eglDestroySurface(m_impl->m_display, m_impl->m_surface);
    m_impl->m_surface = EGL_NO_SURFACE;
    if (m_impl->m_window != nullptr) ANativeWindow_release(m_impl->m_window);
    m_impl->m_window = nullptr;
}
bool AndroidWindowManagerUVE::AttachNativeWindowUVE(void* window) {
    DetachNativeWindowUVE();
    m_impl->m_window = static_cast<ANativeWindow*>(window);
    if (m_impl->m_window != nullptr) ANativeWindow_acquire(m_impl->m_window);
    return TryRecoverSurfaceUVE();
}
void AndroidWindowManagerUVE::SwapBuffersUVE() {
    if (m_impl->m_surface == EGL_NO_SURFACE || !IsValidUVE()) return;
    if (eglSwapBuffers(m_impl->m_display, m_impl->m_surface) == EGL_TRUE) {
        ++m_impl->m_presentations;
        return;
    }
    m_impl->m_contextLost = eglGetError() == EGL_CONTEXT_LOST;
    eglMakeCurrent(m_impl->m_display, m_impl->m_idleSurface, m_impl->m_idleSurface, m_impl->m_context);
    eglDestroySurface(m_impl->m_display, m_impl->m_surface);
    m_impl->m_surface = EGL_NO_SURFACE;
}
void AndroidWindowManagerUVE::SetVSyncEnabledUVE(const bool enabled) {
    if (m_impl->m_display != EGL_NO_DISPLAY && eglSwapInterval(m_impl->m_display, enabled ? 1 : 0) == EGL_TRUE) m_impl->m_vsync = enabled;
}
std::uint32_t AndroidWindowManagerUVE::GetWidthUVE() const noexcept {
    EGLint value = 0;
    if (m_impl->m_surface == EGL_NO_SURFACE || eglQuerySurface(m_impl->m_display, m_impl->m_surface, EGL_WIDTH, &value) != EGL_TRUE) return 0U;
    return value > 0 ? static_cast<std::uint32_t>(value) : 0U;
}
std::uint32_t AndroidWindowManagerUVE::GetHeightUVE() const noexcept {
    EGLint value = 0;
    if (m_impl->m_surface == EGL_NO_SURFACE || eglQuerySurface(m_impl->m_display, m_impl->m_surface, EGL_HEIGHT, &value) != EGL_TRUE) return 0U;
    return value > 0 ? static_cast<std::uint32_t>(value) : 0U;
}
}

namespace UVE::Window {
bool AndroidWindowManagerUVE::IsValidUVE() const noexcept {
    return m_impl->m_context != EGL_NO_CONTEXT && !m_impl->m_contextLost;
}
bool AndroidWindowManagerUVE::IsFocusedUVE() const noexcept { return m_impl->m_focused; }
bool AndroidWindowManagerUVE::IsVSyncEnabledUVE() const noexcept { return m_impl->m_vsync; }
void* AndroidWindowManagerUVE::GetNativeWindowHandleUVE() const noexcept {
    return IsValidUVE() ? m_impl->m_window : nullptr;
}
}

namespace UVE::Window {
std::uint64_t AndroidWindowManagerUVE::GetPresentationCountUVE() const noexcept {
    return m_impl->m_presentations;
}
}
