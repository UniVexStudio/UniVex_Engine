// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_asset.h"
#include "blueprint_asset_types.h"

namespace UVE::Blueprint::Assets {

/// Registry metadata for one Blueprint asset tracked in a project.
struct BlueprintAssetDescriptor final {
    std::string assetPath;
    BlueprintAssetKind kind = BlueprintAssetKind::BlueprintClass;
    std::string parentKind = "Object3D";
    std::uint32_t schemaVersion = 1U;
    std::vector<std::string> dependencies;
    std::vector<std::string> implementedInterfaces;
};

/// Central registry tracking Blueprint assets and their dependency graph.
class BlueprintAssetRegistry final {
public:
    bool RegisterAsset(BlueprintAssetDescriptor descriptor);
    bool UnregisterAsset(std::string_view assetPath);
    [[nodiscard]] const BlueprintAssetDescriptor* FindAsset(std::string_view assetPath) const noexcept;
    [[nodiscard]] std::vector<BlueprintAssetDescriptor> ListByKind(BlueprintAssetKind kind) const;
    [[nodiscard]] std::size_t Count() const noexcept { return m_assets.size(); }
    void Clear() noexcept { m_assets.clear(); }

    /// Direct asset dependencies of `assetPath`.
    [[nodiscard]] std::vector<std::string> GetDependencies(std::string_view assetPath) const;

    /// Reverse dependencies: registered assets that directly depend on `assetPath`.
    [[nodiscard]] std::vector<std::string> GetDependents(std::string_view assetPath) const;

    /// Transitive closure of dependencies of `assetPath` (excluding `assetPath` itself).
    [[nodiscard]] std::vector<std::string> GetTransitiveDependencies(std::string_view assetPath) const;

    /// Returns registered assets in topological dependency order (dependencies first), or empty if cyclic.
    [[nodiscard]] std::vector<std::string> ComputeBuildOrder() const;

    /// True if any dependency cycle exists among registered Blueprint assets.
    [[nodiscard]] bool HasDependencyCycle() const;

private:
    std::map<std::string, BlueprintAssetDescriptor, std::less<>> m_assets;
};

// ---------------------------------------------------------------------------
// Detailed Validation
// ---------------------------------------------------------------------------

enum class BlueprintAssetValidationSeverity : std::uint8_t {
    Warning = 0,
    Error,
};

struct BlueprintAssetValidationIssue final {
    BlueprintAssetValidationSeverity severity = BlueprintAssetValidationSeverity::Error;
    std::uint32_t nodeId = 0U;
    std::string code;
    std::string message;
};

struct BlueprintAssetValidationReport final {
    std::vector<BlueprintAssetValidationIssue> issues;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::size_t ErrorCount() const noexcept;
    [[nodiscard]] std::size_t WarningCount() const noexcept;
};

/// Performs thorough structural, node-catalog, variable, and wire-type validation of a Blueprint graph.
[[nodiscard]] BlueprintAssetValidationReport ValidateBlueprintAssetDetailed(
    const Asset::BlueprintAsset& blueprint);

[[nodiscard]] BlueprintAssetValidationReport ValidateFunctionLibraryAsset(
    const BlueprintFunctionLibraryAsset& library);

[[nodiscard]] BlueprintAssetValidationReport ValidateMacroLibraryAsset(
    const BlueprintMacroLibraryAsset& library);

[[nodiscard]] BlueprintAssetValidationReport ValidateInterfaceAsset(
    const BlueprintInterfaceAsset& iface);

} // namespace UVE::Blueprint::Assets
