#pragma once
#include "i_window_manager.h"
#include "window_desc.h"
#include <memory>
namespace UVE::Events { class IEventSystem; }
namespace UVE::Window {
class AndroidWindowManager final : public IWindowManager {
public:
    AndroidWindowManager(Events::IEventSystem&, void* window, const WindowDesc&);
    ~AndroidWindowManager() override;
    AndroidWindowManager(const AndroidWindowManager&) = delete;
    AndroidWindowManager& operator=(const AndroidWindowManager&) = delete;
    bool IsValid() const noexcept override;
    void AttachInputSystem(Input::IInputSystem*) noexcept override {}
    void PollEvents() override;
    void SetFocused(bool focused);
    bool IsFocused() const noexcept override;
    void SwapBuffers() override;
    bool AttachNativeWindow(void* window);
    // Successful EGL swaps; visible pixels are verified separately by the native smoke test.
    std::uint64_t GetPresentationCount() const noexcept;
    void DetachNativeWindow();
    bool TryRecoverSurface() noexcept override;
    bool IsCloseRequested() const noexcept override { return false; }
    void SetVSyncEnabled(bool enabled) override;
    bool IsVSyncEnabled() const noexcept override;
    void SetFullscreen(bool) override {}
    bool IsFullscreen() const noexcept override { return true; }
    std::uint32_t GetWidth() const noexcept override;
    std::uint32_t GetHeight() const noexcept override;
    std::vector<MonitorInfo> EnumerateMonitors() const override { return {}; }
    void* GetNativeWindowHandle() const noexcept override;
    std::string_view GetBackendName() const noexcept override { return "Android EGL/GLES"; }
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
