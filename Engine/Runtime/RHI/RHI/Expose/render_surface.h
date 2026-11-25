// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include "i_render_device.h"
namespace UVE::Render {
/// Borrowed view of the backend's existing default surface/swapchain. Window/context creation,
/// native image acquisition, resize/rebuild and presentation remain owned by that backend;
/// this does not allocate a competing swapchain or make default-framebuffer handles ownable.
/// Device/window must outlive the view. Owner-thread only. Success means a present request was
/// accepted, not that scan-out completed. Minimized/non-drawable surfaces return NotReady.
class RenderSurface final {
  public:
    explicit RenderSurface(IRenderDevice &device) noexcept : m_device(device) {}
    [[nodiscard]] RenderOperationResult Describe(RenderSurfaceInfo &output) const {
        return m_device.GetSurfaceInfo(output);
    }
    [[nodiscard]] RenderOperationResult Present() {
        RenderSurfaceInfo info;
        const auto result = Describe(info);
        if (result != RenderOperationResult::Success)
            return result;
        if (!info.drawable)
            return RenderOperationResult::NotReady;
        m_device.Present();
        return m_device.IsUsable() ? RenderOperationResult::Success
                                      : RenderOperationResult::DeviceUnavailable;
    }

  private:
    IRenderDevice &m_device;
};
} // namespace UVE::Render
