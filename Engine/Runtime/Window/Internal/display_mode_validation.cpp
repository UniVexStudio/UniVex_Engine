// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "display_mode_validation.h"
namespace UVE::Window {
bool ValidateDisplayModeDescUVE(const DisplayModeDescUVE& desc) noexcept {
    return desc.width > 0U && desc.width <= kMaximumDisplayModeAxisUVE && desc.height > 0U &&
           desc.height <= kMaximumDisplayModeAxisUVE && desc.refreshRateHz <= kMaximumDisplayModeRefreshRateUVE;
}
} // namespace UVE::Window
