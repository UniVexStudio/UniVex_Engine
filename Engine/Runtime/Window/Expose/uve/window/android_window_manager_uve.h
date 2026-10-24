#pragma once
#include "uve/window/i_window_manager_uve.h"
#include "uve/window/window_desc_uve.h"
#include <EGL/egl.h>
struct ANativeWindow;
namespace UVE::Events { class IEventSystemUVE; }
namespace UVE::Window {
class AndroidWindowManagerUVE final : public IWindowManagerUVE {
public:
    AndroidWindowManagerUVE(Events::IEventSystemUVE&, void* window, const WindowDescUVE&);
    ~AndroidWindowManagerUVE() override;
    AndroidWindowManagerUVE(const AndroidWindowManagerUVE&) = delete;
    AndroidWindowManagerUVE& operator=(const AndroidWindowManagerUVE&) = delete;
    bool IsValidUVE() const noexcept override { return m_context != EGL_NO_CONTEXT && !m_contextLost; }
    void AttachInputSystemUVE(Input::IInputSystemUVE*) noexcept override {}
    void PollEventsUVE() override;
    void SetFocusedUVE(bool focused);
    bool IsFocusedUVE() const noexcept override { return m_focused; }
    void SwapBuffersUVE() override;
    bool AttachNativeWindowUVE(void* window);
    void DetachNativeWindowUVE();
    bool TryRecoverSurfaceUVE() noexcept override;
    bool IsCloseRequestedUVE() const noexcept override { return false; }
    void SetVSyncEnabledUVE(bool enabled) override;
    bool IsVSyncEnabledUVE() const noexcept override { return m_vsync; }
    void SetFullscreenUVE(bool) override {}
    bool IsFullscreenUVE() const noexcept override { return true; }
    std::uint32_t GetWidthUVE() const noexcept override;
    std::uint32_t GetHeightUVE() const noexcept override;
    std::vector<MonitorInfoUVE> EnumerateMonitorsUVE() const override { return {}; }
    void* GetNativeWindowHandleUVE() const noexcept override { return m_window; }
    std::string_view GetBackendNameUVE() const noexcept override { return "Android EGL/GLES"; }
private:
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
};
}
