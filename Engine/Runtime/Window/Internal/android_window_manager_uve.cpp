#include "uve/window/android_window_manager_uve.h"
#include <android/native_window.h>
#include "uve/events/i_event_system_uve.h"
#include "uve/window/window_events_uve.h"
namespace UVE::Window {
AndroidWindowManagerUVE::AndroidWindowManagerUVE(Events::IEventSystemUVE& events, void* native,
                                               const WindowDescUVE& desc)
    : m_events(events), m_window(static_cast<ANativeWindow*>(native)), m_vsync(desc.vsyncEnabled) {
    if (m_window == nullptr) return;
    ANativeWindow_acquire(m_window);
    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_display == EGL_NO_DISPLAY || eglInitialize(m_display, nullptr, nullptr) != EGL_TRUE) return;
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) return;
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLint count = 0;
    if (eglChooseConfig(m_display, attributes, &m_config, 1, &count) != EGL_TRUE || count == 0) return;
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttributes);
    if (m_context == EGL_NO_CONTEXT) return;
    const EGLint idleAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    m_idleSurface = eglCreatePbufferSurface(m_display, m_config, idleAttributes);
    if (m_idleSurface == EGL_NO_SURFACE || !TryRecoverSurfaceUVE()) m_contextLost = true;
}
AndroidWindowManagerUVE::~AndroidWindowManagerUVE() {
    if (m_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_surface != EGL_NO_SURFACE) eglDestroySurface(m_display, m_surface);
        if (m_idleSurface != EGL_NO_SURFACE) eglDestroySurface(m_display, m_idleSurface);
        if (m_context != EGL_NO_CONTEXT) eglDestroyContext(m_display, m_context);
        eglTerminate(m_display);
    }
    if (m_window != nullptr) ANativeWindow_release(m_window);
}
bool AndroidWindowManagerUVE::TryRecoverSurfaceUVE() noexcept {
    if (!IsValidUVE() || m_display == EGL_NO_DISPLAY || m_window == nullptr) return false;
    if (m_surface != EGL_NO_SURFACE) return true;
    EGLint format = 0;
    if (eglGetConfigAttrib(m_display, m_config, EGL_NATIVE_VISUAL_ID, &format) != EGL_TRUE) return false;
    if (ANativeWindow_setBuffersGeometry(m_window, 0, 0, format) != 0) return false;
    m_surface = eglCreateWindowSurface(m_display, m_config, m_window, nullptr);
    if (m_surface == EGL_NO_SURFACE) return false;
    if (eglMakeCurrent(m_display, m_surface, m_surface, m_context) != EGL_TRUE) {
        m_contextLost = eglGetError() == EGL_CONTEXT_LOST;
        eglDestroySurface(m_display, m_surface);
        m_surface = EGL_NO_SURFACE;
        return false;
    }
    eglSwapInterval(m_display, m_vsync ? 1 : 0);
    return true;
}
void AndroidWindowManagerUVE::PollEventsUVE() {
    const auto width = GetWidthUVE();
    const auto height = GetHeightUVE();
    if (width != m_lastWidth || height != m_lastHeight) {
        m_lastWidth = width; m_lastHeight = height;
        m_events.Publish(WindowResizedEventUVE{width, height});
    }
}
void AndroidWindowManagerUVE::SetFocusedUVE(const bool focused) {
    if (m_focused == focused) return;
    m_focused = focused;
    m_events.Publish(WindowFocusChangedEventUVE{focused});
}
void AndroidWindowManagerUVE::DetachNativeWindowUVE() {
    if (m_display != EGL_NO_DISPLAY && m_context != EGL_NO_CONTEXT && !m_contextLost) {
        if (eglMakeCurrent(m_display, m_idleSurface, m_idleSurface, m_context) != EGL_TRUE) m_contextLost = true;
    }
    if (m_surface != EGL_NO_SURFACE) eglDestroySurface(m_display, m_surface);
    m_surface = EGL_NO_SURFACE;
    if (m_window != nullptr) ANativeWindow_release(m_window);
    m_window = nullptr;
}
bool AndroidWindowManagerUVE::AttachNativeWindowUVE(void* window) {
    DetachNativeWindowUVE();
    m_window = static_cast<ANativeWindow*>(window);
    if (m_window != nullptr) ANativeWindow_acquire(m_window);
    return TryRecoverSurfaceUVE();
}
void AndroidWindowManagerUVE::SwapBuffersUVE() {
    if (m_surface == EGL_NO_SURFACE || !IsValidUVE()) return;
    if (eglSwapBuffers(m_display, m_surface) == EGL_TRUE) return;
    m_contextLost = eglGetError() == EGL_CONTEXT_LOST;
    eglMakeCurrent(m_display, m_idleSurface, m_idleSurface, m_context);
    eglDestroySurface(m_display, m_surface);
    m_surface = EGL_NO_SURFACE;
}
void AndroidWindowManagerUVE::SetVSyncEnabledUVE(const bool enabled) {
    if (m_display != EGL_NO_DISPLAY && eglSwapInterval(m_display, enabled ? 1 : 0) == EGL_TRUE) m_vsync = enabled;
}
std::uint32_t AndroidWindowManagerUVE::GetWidthUVE() const noexcept {
    EGLint value = 0;
    if (m_surface == EGL_NO_SURFACE || eglQuerySurface(m_display, m_surface, EGL_WIDTH, &value) != EGL_TRUE) return 0U;
    return value > 0 ? static_cast<std::uint32_t>(value) : 0U;
}
std::uint32_t AndroidWindowManagerUVE::GetHeightUVE() const noexcept {
    EGLint value = 0;
    if (m_surface == EGL_NO_SURFACE || eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &value) != EGL_TRUE) return 0U;
    return value > 0 ? static_cast<std::uint32_t>(value) : 0U;
}
}
