// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/window/null_window_manager_uve.h"
#include "uve/window/content_scale_policy_uve.h"

#include "uve/window/window_title_format_uve.h"

namespace UVE::Window {

NullWindowManagerUVE::NullWindowManagerUVE(const WindowDescUVE& desc)
    : m_width(desc.width), m_height(desc.height),
      m_vsyncEnabled(desc.vsyncModeExplicit ? desc.vsyncMode != Platform::VSyncModeUVE::Off : desc.vsyncEnabled),
      m_vsyncMode(desc.vsyncModeExplicit ? desc.vsyncMode
                                        : (desc.vsyncEnabled ? Platform::VSyncModeUVE::On
                                                             : Platform::VSyncModeUVE::Off)),
      m_windowMode(desc.mode), m_contentScaleOverride(desc.contentScaleOverride),
      m_fullscreen(desc.mode == Platform::WindowModeUVE::Fullscreen ||
                   desc.mode == Platform::WindowModeUVE::ExclusiveFullscreen),
      m_displaySleepAllowed(desc.allowDisplaySleep), m_windowTitle(desc.title) {}

bool NullWindowManagerUVE::IsValidUVE() const noexcept {
    return true;
}

void NullWindowManagerUVE::AttachInputSystemUVE(Input::IInputSystemUVE* /*inputSystem*/) noexcept {}

void NullWindowManagerUVE::PollEventsUVE() {
    // No real OS event queue to pump.
}

void NullWindowManagerUVE::SwapBuffersUVE() {
    // No real back buffer to present.
}

bool NullWindowManagerUVE::IsCloseRequestedUVE() const noexcept {
    return false;
}

void NullWindowManagerUVE::SetVSyncEnabledUVE(const bool enabled) {
    m_vsyncEnabled = enabled;
    m_vsyncMode = enabled ? Platform::VSyncModeUVE::On : Platform::VSyncModeUVE::Off;
}

bool NullWindowManagerUVE::IsVSyncEnabledUVE() const noexcept {
    return m_vsyncEnabled;
}

void NullWindowManagerUVE::SetVSyncModeUVE(const Platform::VSyncModeUVE mode) {
    if (Platform::GetVSyncModeNameUVE(mode).empty()) {
        return;
    }
    m_vsyncMode = mode;
    m_vsyncEnabled = mode != Platform::VSyncModeUVE::Off;
}

Platform::VSyncModeUVE NullWindowManagerUVE::GetVSyncModeUVE() const noexcept {
    return m_vsyncMode;
}

void NullWindowManagerUVE::SetFullscreenUVE(const bool fullscreen) {
    m_fullscreen = fullscreen;
    m_windowMode = fullscreen ? Platform::WindowModeUVE::Fullscreen : Platform::WindowModeUVE::Windowed;
}

bool NullWindowManagerUVE::IsFullscreenUVE() const noexcept {
    return m_fullscreen;
}

void NullWindowManagerUVE::SetWindowModeUVE(const Platform::WindowModeUVE mode) {
    if (Platform::GetWindowModeNameUVE(mode).empty()) {
        return;
    }
    m_windowMode = mode;
    m_fullscreen = mode == Platform::WindowModeUVE::Fullscreen ||
                   mode == Platform::WindowModeUVE::ExclusiveFullscreen;
}

Platform::WindowModeUVE NullWindowManagerUVE::GetWindowModeUVE() const noexcept {
    return m_windowMode;
}

void NullWindowManagerUVE::SetWindowTitleUVE(const std::string_view title) {
    if (title.empty() || title.size() > kMaximumWindowTitleBytesUVE ||
        title.find('\0') != std::string_view::npos) {
        return;
    }
    m_windowTitle.assign(title);
}

std::string_view NullWindowManagerUVE::GetWindowTitleUVE() const noexcept {
    return m_windowTitle;
}

void NullWindowManagerUVE::SetDisplaySleepAllowedUVE(const bool allowed) noexcept {
    m_displaySleepAllowed = allowed;
}

bool NullWindowManagerUVE::IsDisplaySleepAllowedUVE() const noexcept {
    return m_displaySleepAllowed;
}

void NullWindowManagerUVE::GetContentScaleUVE(float& outX, float& outY) const noexcept {
    const float scale = ResolveContentScaleOverrideUVE(m_contentScaleOverride).value_or(1.0F);
    outX = scale;
    outY = scale;
}

std::uint32_t NullWindowManagerUVE::GetWidthUVE() const noexcept {
    return m_width;
}

std::uint32_t NullWindowManagerUVE::GetHeightUVE() const noexcept {
    return m_height;
}

std::vector<MonitorInfoUVE> NullWindowManagerUVE::EnumerateMonitorsUVE() const {
    return {};
}

void* NullWindowManagerUVE::GetNativeWindowHandleUVE() const noexcept {
    return nullptr;
}

std::string_view NullWindowManagerUVE::GetBackendNameUVE() const noexcept {
    return "Null";
}

} // namespace UVE::Window
