// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace UVE::Platform {

inline constexpr double kMaximumContentScaleOverrideUVE = 4.0;

/// Portable, serializable display policies shared by project settings and the runtime window
/// backends. These are policy values, not claims that every host API can implement every mode.
enum class WindowModeUVE : std::uint8_t {
    Windowed = 0U,
    Maximized,
    Fullscreen,          ///< Borderless desktop-sized presentation on the selected monitor.
    ExclusiveFullscreen, ///< The native backend owns the monitor's active video mode.
    Borderless,          ///< Undecorated windowed mode, retaining the configured window size.
};

enum class VSyncModeUVE : std::uint8_t {
    Off = 0U,
    On,
    Adaptive,
    Mailbox,
};

enum class StretchModeUVE : std::uint8_t {
    Disabled = 0U,
    CanvasItems,
    Viewport,
};

enum class AspectPolicyUVE : std::uint8_t {
    Ignore = 0U,
    Keep,
    KeepWidth,
    KeepHeight,
    Expand,
};

enum class DisplayOrientationUVE : std::uint8_t {
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
enum class RendererProfileUVE : std::uint8_t {
    Compatibility = 0U,
    Mobile = 1U,
    ForwardPlus = 2U,
};

[[nodiscard]] constexpr RendererProfileUVE DefaultRendererProfileForHostUVE() noexcept {
#if defined(__ANDROID__)
    return RendererProfileUVE::Compatibility;
#else
    return RendererProfileUVE::ForwardPlus;
#endif
}

[[nodiscard]] std::string_view GetWindowModeNameUVE(WindowModeUVE mode) noexcept;
[[nodiscard]] std::optional<WindowModeUVE> ParseWindowModeUVE(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetVSyncModeNameUVE(VSyncModeUVE mode) noexcept;
[[nodiscard]] std::optional<VSyncModeUVE> ParseVSyncModeUVE(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetStretchModeNameUVE(StretchModeUVE mode) noexcept;
[[nodiscard]] std::optional<StretchModeUVE> ParseStretchModeUVE(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetAspectPolicyNameUVE(AspectPolicyUVE policy) noexcept;
[[nodiscard]] std::optional<AspectPolicyUVE> ParseAspectPolicyUVE(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetDisplayOrientationNameUVE(DisplayOrientationUVE orientation) noexcept;
[[nodiscard]] std::optional<DisplayOrientationUVE> ParseDisplayOrientationUVE(std::string_view name) noexcept;
[[nodiscard]] std::string_view GetRendererProfileNameUVE(RendererProfileUVE profile) noexcept;
[[nodiscard]] std::string_view GetRendererProfileDisplayNameUVE(RendererProfileUVE profile) noexcept;
[[nodiscard]] std::string_view GetRendererProfileDescriptionUVE(RendererProfileUVE profile) noexcept;
[[nodiscard]] std::optional<RendererProfileUVE> ParseRendererProfileUVE(std::string_view name) noexcept;

} // namespace UVE::Platform
