// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"
#include "vector3.h"

namespace UVE::UI {

/// Color vision deficiency compensation modes (LMS Daltonization), matching `fullscreen_quad.glsl`.
enum class ColorblindModeUVE : std::uint8_t {
    None = 0,
    Protanopia = 1,
    Deuteranopia = 2,
    Tritanopia = 3,
};

/// Applies LMS Daltonization colorblind compensation in display RGB [0, 1] space.
[[nodiscard]] Math::Vector3UVE ApplyColorblindDaltonizationCpuUVE(
    const Math::Vector3UVE& rgb, ColorblindModeUVE mode, float severity = 1.0F) noexcept;

/// Structured accessible UI widget snapshot exposed to OS screen-reader bridges (UIA / ATK).
struct AccessibilityNodeInfoUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    std::string role;
    std::string label;
    std::string value;
    bool focused = false;
    bool enabled = true;
};

/// Platform-agnostic screen-reader & accessibility announcement bridge hooked into `UIRuntimeUVE`.
class IAccessibilityBridgeUVE {
public:
    virtual ~IAccessibilityBridgeUVE() = default;
    virtual void AnnounceFocusUVE(const AccessibilityNodeInfoUVE& node) = 0;
    virtual void AnnounceTextUVE(std::string_view text, bool polite = true) = 0;
};

/// Recording bridge used for tests and headless verification of screen-reader announcements.
class RecordingAccessibilityBridgeUVE final : public IAccessibilityBridgeUVE {
public:
    void AnnounceFocusUVE(const AccessibilityNodeInfoUVE& node) override {
        m_focusEvents.push_back(node);
    }
    void AnnounceTextUVE(const std::string_view text, const bool polite = true) override {
        static_cast<void>(polite);
        m_announcements.emplace_back(text);
    }

    [[nodiscard]] const std::vector<AccessibilityNodeInfoUVE>& FocusEventsUVE() const noexcept {
        return m_focusEvents;
    }
    [[nodiscard]] const std::vector<std::string>& AnnouncementsUVE() const noexcept {
        return m_announcements;
    }
    void ClearUVE() noexcept {
        m_focusEvents.clear();
        m_announcements.clear();
    }

private:
    std::vector<AccessibilityNodeInfoUVE> m_focusEvents;
    std::vector<std::string> m_announcements;
};

} // namespace UVE::UI
