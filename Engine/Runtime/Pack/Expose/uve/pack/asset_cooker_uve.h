// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <filesystem>
#include <cstddef>
#include <string>
#include <cstdint>
namespace UVE::Pack {
enum class CookTargetUVE { Windows, Android };
struct AssetCookResultUVE {
    bool success = false;
    std::string message;
    std::uintmax_t sourceBytes = 0;
    std::uintmax_t cookedBytes = 0;
    std::size_t fileCount = 0;
};
/// Consumes imported native assets; never mutates the authoring project. Publishes to a fresh
/// directory only after every file/catalog validates. Native paths and GUIDs stay stable.
[[nodiscard]] AssetCookResultUVE CookProjectAssetsUVE(const std::filesystem::path& projectFile,
    const std::filesystem::path& output, CookTargetUVE target);
} // namespace UVE::Pack
