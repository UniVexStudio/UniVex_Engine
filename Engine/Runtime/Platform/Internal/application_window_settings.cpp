// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "application_window_settings.h"

#include <array>

namespace UVE::Platform {
namespace {

template <typename TEnum, std::size_t TCount>
[[nodiscard]] std::string_view LookupName(
    const TEnum value, const std::array<std::pair<TEnum, std::string_view>, TCount>& entries) noexcept {
    for (const auto& [candidate, name] : entries) {
        if (candidate == value) {
            return name;
        }
    }
    return {};
}

template <typename TEnum, std::size_t TCount>
[[nodiscard]] std::optional<TEnum> LookupValue(
    const std::string_view name, const std::array<std::pair<TEnum, std::string_view>, TCount>& entries) noexcept {
    for (const auto& [value, candidate] : entries) {
        if (candidate == name) {
            return value;
        }
    }
    return std::nullopt;
}

constexpr std::array kWindowModes{
    std::pair{WindowMode::Windowed, std::string_view{"windowed"}},
    std::pair{WindowMode::Maximized, std::string_view{"maximized"}},
    std::pair{WindowMode::Fullscreen, std::string_view{"fullscreen"}},
    std::pair{WindowMode::ExclusiveFullscreen, std::string_view{"exclusiveFullscreen"}},
    std::pair{WindowMode::Borderless, std::string_view{"borderless"}},
};
constexpr std::array kVSyncModes{
    std::pair{VSyncMode::Off, std::string_view{"off"}},
    std::pair{VSyncMode::On, std::string_view{"on"}},
    std::pair{VSyncMode::Adaptive, std::string_view{"adaptive"}},
    std::pair{VSyncMode::Mailbox, std::string_view{"mailbox"}},
};
constexpr std::array kStretchModes{
    std::pair{StretchMode::Disabled, std::string_view{"disabled"}},
    std::pair{StretchMode::CanvasItems, std::string_view{"canvasItems"}},
    std::pair{StretchMode::Viewport, std::string_view{"viewport"}},
};
constexpr std::array kAspectPolicies{
    std::pair{AspectPolicy::Ignore, std::string_view{"ignore"}},
    std::pair{AspectPolicy::Keep, std::string_view{"keep"}},
    std::pair{AspectPolicy::KeepWidth, std::string_view{"keepWidth"}},
    std::pair{AspectPolicy::KeepHeight, std::string_view{"keepHeight"}},
    std::pair{AspectPolicy::Expand, std::string_view{"expand"}},
};
constexpr std::array kOrientations{
    std::pair{DisplayOrientation::Auto, std::string_view{"auto"}},
    std::pair{DisplayOrientation::Landscape, std::string_view{"landscape"}},
    std::pair{DisplayOrientation::Portrait, std::string_view{"portrait"}},
    std::pair{DisplayOrientation::LandscapeLeft, std::string_view{"landscapeLeft"}},
    std::pair{DisplayOrientation::LandscapeRight, std::string_view{"landscapeRight"}},
    std::pair{DisplayOrientation::PortraitUpsideDown, std::string_view{"portraitUpsideDown"}},
};
constexpr std::array kRendererProfiles{
    std::pair{RendererProfile::Compatibility, std::string_view{"compatibility"}},
    std::pair{RendererProfile::Mobile, std::string_view{"mobile"}},
    std::pair{RendererProfile::ForwardPlus, std::string_view{"forward_plus"}},
};

} // namespace

std::string_view GetWindowModeName(const WindowMode mode) noexcept {
    return LookupName(mode, kWindowModes);
}
std::optional<WindowMode> ParseWindowMode(const std::string_view name) noexcept {
    return LookupValue(name, kWindowModes);
}
std::string_view GetVSyncModeName(const VSyncMode mode) noexcept {
    return LookupName(mode, kVSyncModes);
}
std::optional<VSyncMode> ParseVSyncMode(const std::string_view name) noexcept {
    return LookupValue(name, kVSyncModes);
}
std::string_view GetStretchModeName(const StretchMode mode) noexcept {
    return LookupName(mode, kStretchModes);
}
std::optional<StretchMode> ParseStretchMode(const std::string_view name) noexcept {
    return LookupValue(name, kStretchModes);
}
std::string_view GetAspectPolicyName(const AspectPolicy policy) noexcept {
    return LookupName(policy, kAspectPolicies);
}
std::optional<AspectPolicy> ParseAspectPolicy(const std::string_view name) noexcept {
    return LookupValue(name, kAspectPolicies);
}
std::string_view GetDisplayOrientationName(const DisplayOrientation orientation) noexcept {
    return LookupName(orientation, kOrientations);
}
std::optional<DisplayOrientation> ParseDisplayOrientation(const std::string_view name) noexcept {
    return LookupValue(name, kOrientations);
}
std::string_view GetRendererProfileName(const RendererProfile profile) noexcept {
    return LookupName(profile, kRendererProfiles);
}
std::string_view GetRendererProfileDisplayName(const RendererProfile profile) noexcept {
    switch (profile) {
        case RendererProfile::Compatibility:
            return "Compatibility";
        case RendererProfile::Mobile:
            return "Mobile";
        case RendererProfile::ForwardPlus:
            return "Forward+";
    }
    return "Compatibility";
}
std::string_view GetRendererProfileDescription(const RendererProfile profile) noexcept {
    switch (profile) {
        case RendererProfile::Compatibility:
            return "Fastest RGBA8 renderer for low-end mobile GPUs (e.g. Snapdragon 665) and older desktops. Low RAM, 1 shadow, up to 4 lights.";
        case RendererProfile::Mobile:
            return "Balanced mobile PBR renderer with HDR lighting, 2 shadow cascades, FXAA, and up to 8 lights.";
        case RendererProfile::ForwardPlus:
            return "Full desktop PBR with clustered SSBO lighting, 3 shadow cascades, SSAO, Bloom, and MSAA. Auto-falls back to Mobile if GPU lacks support.";
    }
    return {};
}
std::optional<RendererProfile> ParseRendererProfile(const std::string_view name) noexcept {
    return LookupValue(name, kRendererProfiles);
}

} // namespace UVE::Platform
