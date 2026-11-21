// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace UVE::Online {

/// File, beside the project registry, holding the access token for the engine release feed. It is
/// readable by its owner only and never logged.
inline constexpr std::string_view kOnlineCredentialsFileNameUVE = "credentials.json";

/// The stored token, or empty when none is saved.
[[nodiscard]] std::string LoadAccessTokenUVE(const std::filesystem::path& file);
/// Saves `token` with owner-only permissions; an empty token removes the file.
[[nodiscard]] bool SaveAccessTokenUVE(const std::filesystem::path& file, const std::string& token);

} // namespace UVE::Online
