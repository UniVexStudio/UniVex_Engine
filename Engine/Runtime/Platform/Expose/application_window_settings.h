// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace UVE::Platform {

inline constexpr double kMaximumContentScaleOverride = 4.0;

/// Portable, serializable display policies shared by project settings and the runtime window
/// backends. These are policy values, not claims that every host API can implement every mode.
enum class WindowMode : std::uint8_t {
    Windowed = 0U,
    Maximized,
    Fullscreen,          ///< Borderless desktop-sized presentation on the selected monitor.
    ExclusiveFullscreen, ///< The native backend owns the monitor's active video mode.
    Borderless,          ///< Undecorated windowed mode, retaining the configured window size.
};

enum class VSyncMode : std::uint8_t {
    Off = 0U,
    On,
    Adaptive,
    Mailbox,
};

enum class StretchMode : std::uint8_t {
    Disabled = 0U,
    CanvasItems,
    Viewport,
};

enum class AspectPolicy : std::uint8_t {
    Ignore = 0U,
    Keep,
    KeepWidth,
    KeepHeight,
    Expand,
};

enum class DisplayOrientation : std::uint8_t {
    Auto = 0U,
    Landscape,
    Portrait,
    LandscapeLeft,
    LandscapeRight,
    PortraitUpsideDown,
};

/// Project rendering profile persisted per project in `.uvproject`.
/// Runtime GPU capability checks may automatically fall back to a lower profile when required
/// features (such as SSBOs or HDR16F render targets) are unavailable on the active device.
enum class RendererProfile : std::uint8_t {
    Compatibility = 0U,
    Mobile = 1U,
    ForwardPlus = 2U,
};

[[nodiscard]] constexpr RendererProfile DefaultRendererProfileForHost() noexcept {
#if defined(__ANDROID__)
    return RendererProfile::Compatibility;
#else
    return RendererProfile::ForwardPlus;
#endif
}

[[nodiscard]] std::string_view GetWindowModeName(WindowMode mode) noexcept;
[[nodiscard]] std::optional<WindowMode> ParseWindowMode(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetVSyncModeName(VSyncMode mode) noexcept;
[[nodiscard]] std::optional<VSyncMode> ParseVSyncMode(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetStretchModeName(StretchMode mode) noexcept;
[[nodiscard]] std::optional<StretchMode> ParseStretchMode(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetAspectPolicyName(AspectPolicy policy) noexcept;
[[nodiscard]] std::optional<AspectPolicy> ParseAspectPolicy(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetDisplayOrientationName(DisplayOrientation orientation) noexcept;
[[nodiscard]] std::optional<DisplayOrientation> ParseDisplayOrientation(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetRendererProfileName(RendererProfile profile) noexcept;
[[nodiscard]] std::string_view GetRendererProfileDisplayName(RendererProfile profile) noexcept;
[[nodiscard]] std::string_view GetRendererProfileDescription(RendererProfile profile) noexcept;
[[nodiscard]] std::optional<RendererProfile> ParseRendererProfile(std::string_view name) noexcept;

} // namespace UVE::Platform
