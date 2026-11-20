// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_asset.h"
#include "blueprint_asset_types.h"
#include "blueprint_nodes.h"

namespace UVE::Blueprint::Serialization {

// ---------------------------------------------------------------------------
// Versioning
// ---------------------------------------------------------------------------

struct BlueprintSchemaVersionUVE final {
    std::uint32_t major = 1U;
    std::uint32_t minor = 0U;

    [[nodiscard]] bool operator==(const BlueprintSchemaVersionUVE&) const = default;
    [[nodiscard]] auto operator<=>(const BlueprintSchemaVersionUVE&) const = default;
};

inline constexpr BlueprintSchemaVersionUVE kCurrentBlueprintSchemaVersionUVE{1U, 0U};

[[nodiscard]] std::string FormatBlueprintSchemaVersionUVE(BlueprintSchemaVersionUVE version);
[[nodiscard]] std::optional<BlueprintSchemaVersionUVE> ParseBlueprintSchemaVersionUVE(std::string_view schemaText);

// ---------------------------------------------------------------------------
// Pin / Node / Wire / Asset Serialization Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::string_view BlueprintPinTypeToNameUVE(Asset::BlueprintPinTypeUVE type) noexcept;
[[nodiscard]] std::optional<Asset::BlueprintPinTypeUVE> ParseBlueprintPinTypeNameUVE(std::string_view name) noexcept;

[[nodiscard]] std::string SerializeBlueprintPinSpecJsonUVE(const Asset::BlueprintPinSpecUVE& pin);
[[nodiscard]] std::optional<Asset::BlueprintPinSpecUVE> ParseBlueprintPinSpecJsonUVE(std::string_view jsonText);

[[nodiscard]] std::string SerializeBlueprintNodeJsonUVE(const Asset::BlueprintNodeUVE& node);
[[nodiscard]] std::optional<Asset::BlueprintNodeUVE> ParseBlueprintNodeJsonUVE(std::string_view jsonText);

[[nodiscard]] std::string SerializeBlueprintWireJsonUVE(const Asset::BlueprintWireUVE& wire);
[[nodiscard]] std::optional<Asset::BlueprintWireUVE> ParseBlueprintWireJsonUVE(std::string_view jsonText);

[[nodiscard]] std::string SerializeFunctionLibraryAssetUVE(const Assets::BlueprintFunctionLibraryAssetUVE& library);
[[nodiscard]] std::optional<Assets::BlueprintFunctionLibraryAssetUVE> ParseFunctionLibraryAssetUVE(
    std::string_view jsonText);

[[nodiscard]] std::string SerializeMacroLibraryAssetUVE(const Assets::BlueprintMacroLibraryAssetUVE& library);
[[nodiscard]] std::optional<Assets::BlueprintMacroLibraryAssetUVE> ParseMacroLibraryAssetUVE(
    std::string_view jsonText);

[[nodiscard]] std::string SerializeInterfaceAssetUVE(const Assets::BlueprintInterfaceAssetUVE& iface);
[[nodiscard]] std::optional<Assets::BlueprintInterfaceAssetUVE> ParseInterfaceAssetUVE(std::string_view jsonText);

// ---------------------------------------------------------------------------
// Migration
// ---------------------------------------------------------------------------

struct BlueprintMigrationResultUVE final {
    bool success = false;
    BlueprintSchemaVersionUVE fromVersion{0U, 0U};
    BlueprintSchemaVersionUVE toVersion = kCurrentBlueprintSchemaVersionUVE;
    std::string migratedJson;
    std::vector<std::string> appliedSteps;
    std::string error;
};

struct BlueprintMigrationStepUVE final {
    BlueprintSchemaVersionUVE fromVersion;
    BlueprintSchemaVersionUVE toVersion;
    std::string description;
    /// Transforms a JSON document string from `fromVersion` to `toVersion`, returning empty on failure.
    std::function<std::optional<std::string>(std::string_view)> transform;
};

class BlueprintMigrationRegistryUVE final {
public:
    /// Constructs a registry pre-populated with built-in legacy schema migrations (e.g. v0 -> v1).
    static BlueprintMigrationRegistryUVE CreateDefaultUVE();

    void RegisterStepUVE(BlueprintMigrationStepUVE step);

    [[nodiscard]] BlueprintMigrationResultUVE MigrateJsonUVE(
        std::string_view jsonText,
        BlueprintSchemaVersionUVE targetVersion = kCurrentBlueprintSchemaVersionUVE) const;

private:
    std::vector<BlueprintMigrationStepUVE> m_steps;
};

// ---------------------------------------------------------------------------
// Import & Export
// ---------------------------------------------------------------------------

enum class BlueprintExportFormatUVE : std::uint8_t {
    JsonPretty = 0,
    JsonCompact,
    UVScriptSource,
};

struct BlueprintImportResultUVE final {
    std::optional<Asset::BlueprintAssetUVE> asset;
    BlueprintSchemaVersionUVE sourceVersion{0U, 0U};
    bool migrated = false;
    std::vector<std::string> notes;
    std::string error;

    [[nodiscard]] bool IsSuccessUVE() const noexcept { return asset.has_value() && error.empty(); }
};

[[nodiscard]] std::string ExportBlueprintAssetUVE(const Asset::BlueprintAssetUVE& blueprint,
                                                  BlueprintExportFormatUVE format,
                                                  std::string_view objectName = "BlueprintObject");

[[nodiscard]] BlueprintImportResultUVE ImportBlueprintFromJsonUVE(
    std::string_view jsonText, const BlueprintMigrationRegistryUVE* customRegistry = nullptr);

} // namespace UVE::Blueprint::Serialization
