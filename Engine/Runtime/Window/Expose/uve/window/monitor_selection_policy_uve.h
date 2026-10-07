// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace UVE::Window {

/// Finds an explicitly requested monitor by its exact backend-provided name. A missing index means
/// the caller should use the primary monitor (for an empty request or an unmatched name).
[[nodiscard]] constexpr std::optional<std::size_t> FindMonitorIndexUVE(
    const std::string_view requestedName, const std::span<const std::string_view> monitorNames) noexcept {
    if (requestedName.empty()) {
        return std::nullopt;
    }
    for (std::size_t index = 0U; index < monitorNames.size(); ++index) {
        if (monitorNames[index] == requestedName) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace UVE::Window
