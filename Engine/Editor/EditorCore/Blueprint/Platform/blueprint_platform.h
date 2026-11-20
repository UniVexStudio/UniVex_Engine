// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace UVE::Blueprint::Platform {

/// Supported UniVex Blueprint execution and authoring platforms.
enum class BlueprintPlatformKindUVE : std::uint8_t {
    Linux = 0,
    Windows,
    Android,
    Unknown,
};

/// Platform-specific runtime and tooling capabilities for Blueprint execution and plugins.
struct BlueprintPlatformCapabilitiesUVE final {
    BlueprintPlatformKindUVE kind = BlueprintPlatformKindUVE::Unknown;
    std::string_view name;
    std::string_view sharedLibraryExtension;
    char preferredPathSeparator = '/';
    bool supportsNativeCodegen = false;
    bool supportsDynamicPluginLoading = false;
    bool supportsInteractiveDebugger = false;
    bool supportsFileWatchHotReload = false;
    std::size_t defaultMaxCallDepth = 256U;
    std::uint64_t defaultInstructionBudget = 1'000'000U;
};

[[nodiscard]] BlueprintPlatformKindUVE GetCurrentBlueprintPlatformKindUVE() noexcept;
[[nodiscard]] BlueprintPlatformCapabilitiesUVE GetBlueprintPlatformCapabilitiesForUVE(
    BlueprintPlatformKindUVE kind) noexcept;
[[nodiscard]] BlueprintPlatformCapabilitiesUVE GetCurrentBlueprintPlatformCapabilitiesUVE() noexcept;
[[nodiscard]] std::string_view BlueprintPlatformNameUVE(BlueprintPlatformKindUVE kind) noexcept;

/// Normalizes a project-relative Blueprint asset path to POSIX forward slashes, stripping leading "./".
/// Returns std::nullopt if the path is empty, absolute, or attempts parent-directory traversal ("..").
[[nodiscard]] std::optional<std::string> NormalizeBlueprintAssetPathUVE(std::string_view rawPath);

/// Computes the platform-specific Blueprint bytecode/codegen cache directory under `projectRoot`.
[[nodiscard]] std::filesystem::path GetBlueprintCacheDirectoryUVE(
    const std::filesystem::path& projectRoot,
    BlueprintPlatformKindUVE platform = GetCurrentBlueprintPlatformKindUVE());

} // namespace UVE::Blueprint::Platform
