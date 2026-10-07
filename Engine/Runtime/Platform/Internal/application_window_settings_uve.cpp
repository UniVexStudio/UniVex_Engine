// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/platform/application_window_settings_uve.h"

#include <array>

namespace UVE::Platform {
namespace {

template <typename TEnum, std::size_t TCount>
[[nodiscard]] std::string_view LookupNameUVE(
    const TEnum value, const std::array<std::pair<TEnum, std::string_view>, TCount>& entries) noexcept {
    for (const auto& [candidate, name] : entries) {
        if (candidate == value) {
            return name;
        }
    }
    return {};
}

template <typename TEnum, std::size_t TCount>
[[nodiscard]] std::optional<TEnum> LookupValueUVE(
    const std::string_view name, const std::array<std::pair<TEnum, std::string_view>, TCount>& entries) noexcept {
    for (const auto& [value, candidate] : entries) {
        if (candidate == name) {
            return value;
        }
    }
    return std::nullopt;
}

constexpr std::array kWindowModesUVE{
    std::pair{WindowModeUVE::Windowed, std::string_view{"windowed"}},
    std::pair{WindowModeUVE::Maximized, std::string_view{"maximized"}},
    std::pair{WindowModeUVE::Fullscreen, std::string_view{"fullscreen"}},
    std::pair{WindowModeUVE::ExclusiveFullscreen, std::string_view{"exclusiveFullscreen"}},
    std::pair{WindowModeUVE::Borderless, std::string_view{"borderless"}},
};
constexpr std::array kVSyncModesUVE{
    std::pair{VSyncModeUVE::Off, std::string_view{"off"}},
    std::pair{VSyncModeUVE::On, std::string_view{"on"}},
    std::pair{VSyncModeUVE::Adaptive, std::string_view{"adaptive"}},
    std::pair{VSyncModeUVE::Mailbox, std::string_view{"mailbox"}},
};
constexpr std::array kStretchModesUVE{
    std::pair{StretchModeUVE::Disabled, std::string_view{"disabled"}},
    std::pair{StretchModeUVE::CanvasItems, std::string_view{"canvasItems"}},
    std::pair{StretchModeUVE::Viewport, std::string_view{"viewport"}},
};
constexpr std::array kAspectPoliciesUVE{
    std::pair{AspectPolicyUVE::Ignore, std::string_view{"ignore"}},
    std::pair{AspectPolicyUVE::Keep, std::string_view{"keep"}},
    std::pair{AspectPolicyUVE::KeepWidth, std::string_view{"keepWidth"}},
    std::pair{AspectPolicyUVE::KeepHeight, std::string_view{"keepHeight"}},
    std::pair{AspectPolicyUVE::Expand, std::string_view{"expand"}},
};
constexpr std::array kOrientationsUVE{
    std::pair{DisplayOrientationUVE::Auto, std::string_view{"auto"}},
    std::pair{DisplayOrientationUVE::Landscape, std::string_view{"landscape"}},
    std::pair{DisplayOrientationUVE::Portrait, std::string_view{"portrait"}},
    std::pair{DisplayOrientationUVE::LandscapeLeft, std::string_view{"landscapeLeft"}},
    std::pair{DisplayOrientationUVE::LandscapeRight, std::string_view{"landscapeRight"}},
    std::pair{DisplayOrientationUVE::PortraitUpsideDown, std::string_view{"portraitUpsideDown"}},
};

} // namespace

std::string_view GetWindowModeNameUVE(const WindowModeUVE mode) noexcept {
    return LookupNameUVE(mode, kWindowModesUVE);
}
std::optional<WindowModeUVE> ParseWindowModeUVE(const std::string_view name) noexcept {
    return LookupValueUVE(name, kWindowModesUVE);
}
std::string_view GetVSyncModeNameUVE(const VSyncModeUVE mode) noexcept {
    return LookupNameUVE(mode, kVSyncModesUVE);
}
std::optional<VSyncModeUVE> ParseVSyncModeUVE(const std::string_view name) noexcept {
    return LookupValueUVE(name, kVSyncModesUVE);
}
std::string_view GetStretchModeNameUVE(const StretchModeUVE mode) noexcept {
    return LookupNameUVE(mode, kStretchModesUVE);
}
std::optional<StretchModeUVE> ParseStretchModeUVE(const std::string_view name) noexcept {
    return LookupValueUVE(name, kStretchModesUVE);
}
std::string_view GetAspectPolicyNameUVE(const AspectPolicyUVE policy) noexcept {
    return LookupNameUVE(policy, kAspectPoliciesUVE);
}
std::optional<AspectPolicyUVE> ParseAspectPolicyUVE(const std::string_view name) noexcept {
    return LookupValueUVE(name, kAspectPoliciesUVE);
}
std::string_view GetDisplayOrientationNameUVE(const DisplayOrientationUVE orientation) noexcept {
    return LookupNameUVE(orientation, kOrientationsUVE);
}
std::optional<DisplayOrientationUVE> ParseDisplayOrientationUVE(const std::string_view name) noexcept {
    return LookupValueUVE(name, kOrientationsUVE);
}

} // namespace UVE::Platform
