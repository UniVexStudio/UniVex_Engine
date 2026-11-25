// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "i_xr_runtime.h"

namespace UVE::Platform {

MockXrRuntime::MockXrRuntime(const float ipdMetres) noexcept
    : m_ipdMetres(ipdMetres > 0.0F ? ipdMetres : 0.064F) {
    m_headPose.tracked = true;
    m_controllers[0].pose.position = {-0.3F, 1.2F, -0.4F};
    m_controllers[0].pose.tracked = true;
    m_controllers[1].pose.position = {0.3F, 1.2F, -0.4F};
    m_controllers[1].pose.tracked = true;
}

bool MockXrRuntime::BeginFrame() {
    return m_active;
}

void MockXrRuntime::EndFrame() {}

void MockXrRuntime::SetControllerState(const XrHand hand,
                                             const XrControllerState& state) noexcept {
    m_controllers[hand == XrHand::Left ? 0U : 1U] = state;
}

XrEyeView MockXrRuntime::GetEyeView(const XrEye eye) const noexcept {
    XrEyeView view{};
    view.pose = m_headPose;
    const float halfIpd = 0.5F * m_ipdMetres;
    view.pose.position[0] += (eye == XrEye::Left ? -halfIpd : halfIpd);
    return view;
}

XrControllerState MockXrRuntime::GetControllerState(const XrHand hand) const noexcept {
    return m_controllers[hand == XrHand::Left ? 0U : 1U];
}

} // namespace UVE::Platform
