// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include "uve/rhi/i_render_device_uve.h"
namespace UVE::Render {
/// Borrowed view of the backend's existing default surface/swapchain. Window/context creation,
/// native image acquisition, resize/rebuild and presentation remain owned by that backend;
/// this does not allocate a competing swapchain or make default-framebuffer handles ownable.
/// Device/window must outlive the view. Owner-thread only. Success means a present request was
/// accepted, not that scan-out completed. Minimized/non-drawable surfaces return NotReady.
class RenderSurfaceUVE final {
  public:
    explicit RenderSurfaceUVE(IRenderDeviceUVE &device) noexcept : m_device(device) {}
    [[nodiscard]] RenderOperationResultUVE DescribeUVE(RenderSurfaceInfoUVE &output) const {
        return m_device.GetSurfaceInfoUVE(output);
    }
    [[nodiscard]] RenderOperationResultUVE PresentUVE() {
        RenderSurfaceInfoUVE info;
        const auto result = DescribeUVE(info);
        if (result != RenderOperationResultUVE::Success)
            return result;
        if (!info.drawable)
            return RenderOperationResultUVE::NotReady;
        m_device.PresentUVE();
        return m_device.IsUsableUVE() ? RenderOperationResultUVE::Success
                                      : RenderOperationResultUVE::DeviceUnavailable;
    }

  private:
    IRenderDeviceUVE &m_device;
};
} // namespace UVE::Render
