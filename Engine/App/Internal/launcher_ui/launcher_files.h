// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace UVE::LauncherUI {

/// A byte count for people to read: whole units of the largest binary unit (B, KB, MB, GB, TB)
/// that keeps the number at least one, e.g. "512 KB" or "184 MB".
[[nodiscard]] std::string FormatByteSizeUVE(std::uint64_t bytes);

/// A path in the system's temporary folder that no earlier call returned: `prefix`, a value unique to
/// this moment, then `suffix` (an extension, or empty for a folder). Empty when the system has no
/// temporary folder.
[[nodiscard]] std::filesystem::path UniqueTemporaryPathUVE(std::string_view prefix, std::string_view suffix);

} // namespace UVE::LauncherUI
