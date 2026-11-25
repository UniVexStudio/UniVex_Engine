// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "display_mode_validation.h"
namespace UVE::Window {
bool ValidateDisplayModeDesc(const DisplayModeDesc& desc) noexcept {
    return desc.width > 0U && desc.width <= kMaximumDisplayModeAxis && desc.height > 0U &&
           desc.height <= kMaximumDisplayModeAxis && desc.refreshRateHz <= kMaximumDisplayModeRefreshRate;
}
} // namespace UVE::Window
