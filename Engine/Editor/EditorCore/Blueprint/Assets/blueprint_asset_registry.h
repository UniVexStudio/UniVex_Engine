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
struct BlueprintAssetDescriptorUVE final {
    std::string assetPath;
    BlueprintAssetKindUVE kind = BlueprintAssetKindUVE::BlueprintClass;
    std::string parentKind = "Object3D";
    std::uint32_t schemaVersion = 1U;
    std::vector<std::string> dependencies;
    std::vector<std::string> implementedInterfaces;
};

/// Central registry tracking Blueprint assets and their dependency graph.
class BlueprintAssetRegistryUVE final {
public:
    bool RegisterAssetUVE(BlueprintAssetDescriptorUVE descriptor);
    bool UnregisterAssetUVE(std::string_view assetPath);
    [[nodiscard]] const BlueprintAssetDescriptorUVE* FindAssetUVE(std::string_view assetPath) const noexcept;
    [[nodiscard]] std::vector<BlueprintAssetDescriptorUVE> ListByKindUVE(BlueprintAssetKindUVE kind) const;
    [[nodiscard]] std::size_t CountUVE() const noexcept { return m_assets.size(); }
    void ClearUVE() noexcept { m_assets.clear(); }

    /// Direct asset dependencies of `assetPath`.
    [[nodiscard]] std::vector<std::string> GetDependenciesUVE(std::string_view assetPath) const;

    /// Reverse dependencies: registered assets that directly depend on `assetPath`.
    [[nodiscard]] std::vector<std::string> GetDependentsUVE(std::string_view assetPath) const;

    /// Transitive closure of dependencies of `assetPath` (excluding `assetPath` itself).
    [[nodiscard]] std::vector<std::string> GetTransitiveDependenciesUVE(std::string_view assetPath) const;

    /// Returns registered assets in topological dependency order (dependencies first), or empty if cyclic.
    [[nodiscard]] std::vector<std::string> ComputeBuildOrderUVE() const;

    /// True if any dependency cycle exists among registered Blueprint assets.
    [[nodiscard]] bool HasDependencyCycleUVE() const;

private:
    std::map<std::string, BlueprintAssetDescriptorUVE, std::less<>> m_assets;
};

// ---------------------------------------------------------------------------
// Detailed Validation
// ---------------------------------------------------------------------------

enum class BlueprintAssetValidationSeverityUVE : std::uint8_t {
    Warning = 0,
    Error,
};

struct BlueprintAssetValidationIssueUVE final {
    BlueprintAssetValidationSeverityUVE severity = BlueprintAssetValidationSeverityUVE::Error;
    std::uint32_t nodeId = 0U;
    std::string code;
    std::string message;
};

struct BlueprintAssetValidationReportUVE final {
    std::vector<BlueprintAssetValidationIssueUVE> issues;

    [[nodiscard]] bool IsValidUVE() const noexcept;
    [[nodiscard]] std::size_t ErrorCountUVE() const noexcept;
    [[nodiscard]] std::size_t WarningCountUVE() const noexcept;
};

/// Performs thorough structural, node-catalog, variable, and wire-type validation of a Blueprint graph.
[[nodiscard]] BlueprintAssetValidationReportUVE ValidateBlueprintAssetDetailedUVE(
    const Asset::BlueprintAssetUVE& blueprint);

[[nodiscard]] BlueprintAssetValidationReportUVE ValidateFunctionLibraryAssetUVE(
    const BlueprintFunctionLibraryAssetUVE& library);

[[nodiscard]] BlueprintAssetValidationReportUVE ValidateMacroLibraryAssetUVE(
    const BlueprintMacroLibraryAssetUVE& library);

[[nodiscard]] BlueprintAssetValidationReportUVE ValidateInterfaceAssetUVE(
    const BlueprintInterfaceAssetUVE& iface);

} // namespace UVE::Blueprint::Assets
