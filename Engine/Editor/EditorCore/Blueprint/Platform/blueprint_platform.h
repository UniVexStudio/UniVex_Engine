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
enum class BlueprintPlatformKind : std::uint8_t {
    Linux = 0,
    Windows,
    Android,
    Unknown,
};

/// Platform-specific runtime and tooling capabilities for Blueprint execution and plugins.
struct BlueprintPlatformCapabilities final {
    BlueprintPlatformKind kind = BlueprintPlatformKind::Unknown;
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

[[nodiscard]] BlueprintPlatformKind GetCurrentBlueprintPlatformKind() noexcept;
[[nodiscard]] BlueprintPlatformCapabilities GetBlueprintPlatformCapabilitiesFor(
    BlueprintPlatformKind kind) noexcept;
[[nodiscard]] BlueprintPlatformCapabilities GetCurrentBlueprintPlatformCapabilities() noexcept;
[[nodiscard]] std::string_view BlueprintPlatformName(BlueprintPlatformKind kind) noexcept;

/// Normalizes a project-relative Blueprint asset path to POSIX forward slashes, stripping leading "./".
/// Returns std::nullopt if the path is empty, absolute, or attempts parent-directory traversal ("..").
[[nodiscard]] std::optional<std::string> NormalizeBlueprintAssetPath(std::string_view rawPath);

/// Computes the platform-specific Blueprint bytecode/codegen cache directory under `projectRoot`.
[[nodiscard]] std::filesystem::path GetBlueprintCacheDirectory(
    const std::filesystem::path& projectRoot,
    BlueprintPlatformKind platform = GetCurrentBlueprintPlatformKind());

} // namespace UVE::Blueprint::Platform
