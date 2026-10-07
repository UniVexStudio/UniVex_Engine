// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/window/window_desc_validation_uve.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "uve/window/display_mode_validation_uve.h"

namespace UVE::Window {

bool ValidateWindowDescUVE(const WindowDescUVE& desc) noexcept {
    // Keep dimensions within the shared display-mode axis cap before the GLFW int narrowing cast.
    // The enum name lookups reject forged/out-of-range enum values from serialized or caller data.
    if (desc.width == 0U || desc.width > kMaximumDisplayModeAxisUVE || desc.height == 0U ||
        desc.height > kMaximumDisplayModeAxisUVE || desc.title.empty() ||
        desc.title.size() > kMaximumWindowTitleBytesUVE || desc.title.find('\0') != std::string::npos ||
        desc.glVersionMajor < 1U || desc.icons.size() > 16U ||
        !Platform::GetWindowModeNameUVE(desc.mode).size() ||
        !Platform::GetVSyncModeNameUVE(desc.vsyncMode).size() ||
        !Platform::GetStretchModeNameUVE(desc.stretchMode).size() ||
        !Platform::GetAspectPolicyNameUVE(desc.aspectPolicy).size() ||
        !Platform::GetDisplayOrientationNameUVE(desc.orientation).size() ||
        desc.minimumWidth > kMaximumDisplayModeAxisUVE || desc.minimumHeight > kMaximumDisplayModeAxisUVE ||
        desc.maximumWidth > kMaximumDisplayModeAxisUVE || desc.maximumHeight > kMaximumDisplayModeAxisUVE ||
        (desc.minimumWidth != 0U && desc.maximumWidth != 0U && desc.minimumWidth > desc.maximumWidth) ||
        (desc.minimumHeight != 0U && desc.maximumHeight != 0U && desc.minimumHeight > desc.maximumHeight) ||
        desc.initialPositionX < -131072 || desc.initialPositionX > 131072 ||
        desc.initialPositionY < -131072 || desc.initialPositionY > 131072 ||
        desc.monitorName.size() > 256U || desc.monitorName.find('\0') != std::string::npos ||
        !std::isfinite(desc.contentScaleOverride) || desc.contentScaleOverride < 0.0 ||
        desc.contentScaleOverride > Platform::kMaximumContentScaleOverrideUVE || desc.focusedFrameRateCap > 1000U ||
        desc.unfocusedFrameRateCap > 1000U || desc.allowedOrientations.empty() ||
        desc.allowedOrientations.size() > 6U) {
        return false;
    }

    std::array<bool, 6U> orientationsSeen{};
    bool requestedOrientationAllowed = desc.orientation == Platform::DisplayOrientationUVE::Auto;
    for (const Platform::DisplayOrientationUVE orientation : desc.allowedOrientations) {
        if (orientation == Platform::DisplayOrientationUVE::Auto ||
            Platform::GetDisplayOrientationNameUVE(orientation).empty()) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(orientation);
        if (index >= orientationsSeen.size() || orientationsSeen[index]) {
            return false;
        }
        orientationsSeen[index] = true;
        requestedOrientationAllowed = requestedOrientationAllowed || orientation == desc.orientation;
    }
    if (!requestedOrientationAllowed) {
        return false;
    }

    constexpr std::uint64_t kMaximumImageBytesUVE = 64ULL * 1024ULL * 1024ULL;
    if ((desc.cursorRgba8.empty() && (desc.cursorImageWidth != 0U || desc.cursorImageHeight != 0U)) ||
        (!desc.cursorRgba8.empty() &&
         (desc.cursorImageWidth == 0U || desc.cursorImageWidth > kMaximumWindowIconAxisUVE ||
          desc.cursorImageHeight == 0U || desc.cursorImageHeight > kMaximumWindowIconAxisUVE ||
          desc.cursorHotspotX >= desc.cursorImageWidth || desc.cursorHotspotY >= desc.cursorImageHeight ||
          static_cast<std::uint64_t>(desc.cursorImageWidth) * desc.cursorImageHeight * 4ULL > kMaximumImageBytesUVE ||
          desc.cursorRgba8.size() != static_cast<std::uint64_t>(desc.cursorImageWidth) *
                                         desc.cursorImageHeight * 4ULL))) {
        return false;
    }

    for (const WindowIconUVE& icon : desc.icons) {
        if (icon.width == 0U || icon.width > kMaximumWindowIconAxisUVE || icon.height == 0U ||
            icon.height > kMaximumWindowIconAxisUVE) {
            return false;
        }
        const std::uint64_t pixelBytes = static_cast<std::uint64_t>(icon.width) * icon.height * 4ULL;
        if (pixelBytes > kMaximumImageBytesUVE || icon.rgba8.size() != pixelBytes) {
            return false;
        }
    }
    return true;
}

} // namespace UVE::Window
