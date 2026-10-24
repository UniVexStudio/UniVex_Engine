#pragma once
#include "uve/window/i_window_manager_uve.h"
#include "uve/window/window_desc_uve.h"
#include <memory>
namespace UVE::Events { class IEventSystemUVE; }
namespace UVE::Window {
class AndroidWindowManagerUVE final : public IWindowManagerUVE {
public:
    AndroidWindowManagerUVE(Events::IEventSystemUVE&, void* window, const WindowDescUVE&);
    ~AndroidWindowManagerUVE() override;
    AndroidWindowManagerUVE(const AndroidWindowManagerUVE&) = delete;
    AndroidWindowManagerUVE& operator=(const AndroidWindowManagerUVE&) = delete;
    bool IsValidUVE() const noexcept override;
    void AttachInputSystemUVE(Input::IInputSystemUVE*) noexcept override {}
    void PollEventsUVE() override;
    void SetFocusedUVE(bool focused);
    bool IsFocusedUVE() const noexcept override;
    void SwapBuffersUVE() override;
    bool AttachNativeWindowUVE(void* window);
    // Successful EGL swaps; visible pixels are verified separately by the native smoke test.
    std::uint64_t GetPresentationCountUVE() const noexcept;
    void DetachNativeWindowUVE();
    bool TryRecoverSurfaceUVE() noexcept override;
    bool IsCloseRequestedUVE() const noexcept override { return false; }
    void SetVSyncEnabledUVE(bool enabled) override;
    bool IsVSyncEnabledUVE() const noexcept override;
    void SetFullscreenUVE(bool) override {}
    bool IsFullscreenUVE() const noexcept override { return true; }
    std::uint32_t GetWidthUVE() const noexcept override;
    std::uint32_t GetHeightUVE() const noexcept override;
    std::vector<MonitorInfoUVE> EnumerateMonitorsUVE() const override { return {}; }
    void* GetNativeWindowHandleUVE() const noexcept override;
    std::string_view GetBackendNameUVE() const noexcept override { return "Android EGL/GLES"; }
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
