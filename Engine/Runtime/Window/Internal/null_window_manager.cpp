// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "null_window_manager.h"
#include "content_scale_policy.h"

#include "window_title_format.h"

namespace UVE::Window {

NullWindowManager::NullWindowManager(const WindowDesc& desc)
    : m_width(desc.width), m_height(desc.height),
      m_vsyncEnabled(desc.vsyncModeExplicit ? desc.vsyncMode != Platform::VSyncMode::Off : desc.vsyncEnabled),
      m_vsyncMode(desc.vsyncModeExplicit ? desc.vsyncMode
                                        : (desc.vsyncEnabled ? Platform::VSyncMode::On
                                                             : Platform::VSyncMode::Off)),
      m_windowMode(desc.mode), m_contentScaleOverride(desc.contentScaleOverride),
      m_fullscreen(desc.mode == Platform::WindowMode::Fullscreen ||
                   desc.mode == Platform::WindowMode::ExclusiveFullscreen),
      m_displaySleepAllowed(desc.allowDisplaySleep), m_windowTitle(desc.title) {}

bool NullWindowManager::IsValid() const noexcept {
    return true;
}

void NullWindowManager::AttachInputSystem(Input::IInputSystem* /*inputSystem*/) noexcept {}

void NullWindowManager::PollEvents() {
    // No real OS event queue to pump.
}

void NullWindowManager::SwapBuffers() {
    // No real back buffer to present.
}

bool NullWindowManager::IsCloseRequested() const noexcept {
    return false;
}

void NullWindowManager::SetVSyncEnabled(const bool enabled) {
    m_vsyncEnabled = enabled;
    m_vsyncMode = enabled ? Platform::VSyncMode::On : Platform::VSyncMode::Off;
}

bool NullWindowManager::IsVSyncEnabled() const noexcept {
    return m_vsyncEnabled;
}

void NullWindowManager::SetVSyncMode(const Platform::VSyncMode mode) {
    if (Platform::GetVSyncModeName(mode).empty()) {
        return;
    }
    m_vsyncMode = mode;
    m_vsyncEnabled = mode != Platform::VSyncMode::Off;
}

Platform::VSyncMode NullWindowManager::GetVSyncMode() const noexcept {
    return m_vsyncMode;
}

void NullWindowManager::SetFullscreen(const bool fullscreen) {
    m_fullscreen = fullscreen;
    m_windowMode = fullscreen ? Platform::WindowMode::Fullscreen : Platform::WindowMode::Windowed;
}

bool NullWindowManager::IsFullscreen() const noexcept {
    return m_fullscreen;
}

void NullWindowManager::SetWindowMode(const Platform::WindowMode mode) {
    if (Platform::GetWindowModeName(mode).empty()) {
        return;
    }
    m_windowMode = mode;
    m_fullscreen = mode == Platform::WindowMode::Fullscreen ||
                   mode == Platform::WindowMode::ExclusiveFullscreen;
}

Platform::WindowMode NullWindowManager::GetWindowMode() const noexcept {
    return m_windowMode;
}

void NullWindowManager::SetWindowTitle(const std::string_view title) {
    if (title.empty() || title.size() > kMaximumWindowTitleBytes ||
        title.find('\0') != std::string_view::npos) {
        return;
    }
    m_windowTitle.assign(title);
}

std::string_view NullWindowManager::GetWindowTitle() const noexcept {
    return m_windowTitle;
}

void NullWindowManager::SetDisplaySleepAllowed(const bool allowed) noexcept {
    m_displaySleepAllowed = allowed;
}

bool NullWindowManager::IsDisplaySleepAllowed() const noexcept {
    return m_displaySleepAllowed;
}

void NullWindowManager::GetContentScale(float& outX, float& outY) const noexcept {
    const float scale = ResolveContentScaleOverride(m_contentScaleOverride).value_or(1.0F);
    outX = scale;
    outY = scale;
}

std::uint32_t NullWindowManager::GetWidth() const noexcept {
    return m_width;
}

std::uint32_t NullWindowManager::GetHeight() const noexcept {
    return m_height;
}

std::vector<MonitorInfo> NullWindowManager::EnumerateMonitors() const {
    return {};
}

void* NullWindowManager::GetNativeWindowHandle() const noexcept {
    return nullptr;
}

std::string_view NullWindowManager::GetBackendName() const noexcept {
    return "Null";
}

} // namespace UVE::Window
