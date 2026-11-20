// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "i_xr_runtime.h"

namespace UVE::Platform {

MockXrRuntimeUVE::MockXrRuntimeUVE(const float ipdMetres) noexcept
    : m_ipdMetres(ipdMetres > 0.0F ? ipdMetres : 0.064F) {
    m_headPose.tracked = true;
    m_controllers[0].pose.position = {-0.3F, 1.2F, -0.4F};
    m_controllers[0].pose.tracked = true;
    m_controllers[1].pose.position = {0.3F, 1.2F, -0.4F};
    m_controllers[1].pose.tracked = true;
}

bool MockXrRuntimeUVE::BeginFrameUVE() {
    return m_active;
}

void MockXrRuntimeUVE::EndFrameUVE() {}

void MockXrRuntimeUVE::SetControllerStateUVE(const XrHandUVE hand,
                                             const XrControllerStateUVE& state) noexcept {
    m_controllers[hand == XrHandUVE::Left ? 0U : 1U] = state;
}

XrEyeViewUVE MockXrRuntimeUVE::GetEyeViewUVE(const XrEyeUVE eye) const noexcept {
    XrEyeViewUVE view{};
    view.pose = m_headPose;
    const float halfIpd = 0.5F * m_ipdMetres;
    view.pose.position[0] += (eye == XrEyeUVE::Left ? -halfIpd : halfIpd);
    return view;
}

XrControllerStateUVE MockXrRuntimeUVE::GetControllerStateUVE(const XrHandUVE hand) const noexcept {
    return m_controllers[hand == XrHandUVE::Left ? 0U : 1U];
}

} // namespace UVE::Platform
