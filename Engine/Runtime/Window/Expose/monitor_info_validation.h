#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "monitor_info.h"

namespace UVE::Window {

inline constexpr std::size_t kMaximumMonitorSnapshotEntries = 32U;

/// Validates signed backend monitor dimensions before conversion into the copied unsigned snapshot DTO.
[[nodiscard]] bool ValidateMonitorDimensions(int width, int height, std::uint32_t& outWidth,
                                               std::uint32_t& outHeight) noexcept;

/// Validates copied monitor snapshots without owning display handles, hot-plug state, or backend choice.
[[nodiscard]] bool ValidateMonitorInfo(const MonitorInfo& monitor) noexcept;
[[nodiscard]] bool ValidateMonitorSnapshot(const std::vector<MonitorInfo>& monitors) noexcept;

} // namespace UVE::Window
