// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "window_desc_validation.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "display_mode_validation.h"

namespace UVE::Window {

bool ValidateWindowDesc(const WindowDesc& desc) noexcept {
    // Keep dimensions within the shared display-mode axis cap before the GLFW int narrowing cast.
    // The enum name lookups reject forged/out-of-range enum values from serialized or caller data.
    if (desc.width == 0U || desc.width > kMaximumDisplayModeAxis || desc.height == 0U ||
        desc.height > kMaximumDisplayModeAxis || desc.title.empty() ||
        desc.title.size() > kMaximumWindowTitleBytes || desc.title.find('\0') != std::string::npos ||
        desc.glVersionMajor < 1U || desc.icons.size() > 16U ||
        !Platform::GetWindowModeName(desc.mode).size() ||
        !Platform::GetVSyncModeName(desc.vsyncMode).size() ||
        !Platform::GetStretchModeName(desc.stretchMode).size() ||
        !Platform::GetAspectPolicyName(desc.aspectPolicy).size() ||
        !Platform::GetDisplayOrientationName(desc.orientation).size() ||
        desc.minimumWidth > kMaximumDisplayModeAxis || desc.minimumHeight > kMaximumDisplayModeAxis ||
        desc.maximumWidth > kMaximumDisplayModeAxis || desc.maximumHeight > kMaximumDisplayModeAxis ||
        (desc.minimumWidth != 0U && desc.maximumWidth != 0U && desc.minimumWidth > desc.maximumWidth) ||
        (desc.minimumHeight != 0U && desc.maximumHeight != 0U && desc.minimumHeight > desc.maximumHeight) ||
        desc.initialPositionX < -131072 || desc.initialPositionX > 131072 ||
        desc.initialPositionY < -131072 || desc.initialPositionY > 131072 ||
        desc.monitorName.size() > 256U || desc.monitorName.find('\0') != std::string::npos ||
        !std::isfinite(desc.contentScaleOverride) || desc.contentScaleOverride < 0.0 ||
        desc.contentScaleOverride > Platform::kMaximumContentScaleOverride || desc.focusedFrameRateCap > 1000U ||
        desc.unfocusedFrameRateCap > 1000U || desc.allowedOrientations.empty() ||
        desc.allowedOrientations.size() > 6U) {
        return false;
    }

    std::array<bool, 6U> orientationsSeen{};
    bool requestedOrientationAllowed = desc.orientation == Platform::DisplayOrientation::Auto;
    for (const Platform::DisplayOrientation orientation : desc.allowedOrientations) {
        if (orientation == Platform::DisplayOrientation::Auto ||
            Platform::GetDisplayOrientationName(orientation).empty()) {
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

    constexpr std::uint64_t kMaximumImageBytes = 64ULL * 1024ULL * 1024ULL;
    if ((desc.cursorRgba8.empty() && (desc.cursorImageWidth != 0U || desc.cursorImageHeight != 0U)) ||
        (!desc.cursorRgba8.empty() &&
         (desc.cursorImageWidth == 0U || desc.cursorImageWidth > kMaximumWindowIconAxis ||
          desc.cursorImageHeight == 0U || desc.cursorImageHeight > kMaximumWindowIconAxis ||
          desc.cursorHotspotX >= desc.cursorImageWidth || desc.cursorHotspotY >= desc.cursorImageHeight ||
          static_cast<std::uint64_t>(desc.cursorImageWidth) * desc.cursorImageHeight * 4ULL > kMaximumImageBytes ||
          desc.cursorRgba8.size() != static_cast<std::uint64_t>(desc.cursorImageWidth) *
                                         desc.cursorImageHeight * 4ULL))) {
        return false;
    }

    for (const WindowIcon& icon : desc.icons) {
        if (icon.width == 0U || icon.width > kMaximumWindowIconAxis || icon.height == 0U ||
            icon.height > kMaximumWindowIconAxis) {
            return false;
        }
        const std::uint64_t pixelBytes = static_cast<std::uint64_t>(icon.width) * icon.height * 4ULL;
        if (pixelBytes > kMaximumImageBytes || icon.rgba8.size() != pixelBytes) {
            return false;
        }
    }
    return true;
}

} // namespace UVE::Window
