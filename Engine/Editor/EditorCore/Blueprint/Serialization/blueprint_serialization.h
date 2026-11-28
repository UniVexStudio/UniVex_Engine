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

struct BlueprintSchemaVersion final {
    std::uint32_t major = 1U;
    std::uint32_t minor = 0U;

    [[nodiscard]] bool operator==(const BlueprintSchemaVersion&) const = default;
    [[nodiscard]] auto operator<=>(const BlueprintSchemaVersion&) const = default;
};

inline constexpr BlueprintSchemaVersion kCurrentBlueprintSchemaVersion{1U, 0U};

[[nodiscard]] std::string FormatBlueprintSchemaVersion(BlueprintSchemaVersion version);
[[nodiscard]] std::optional<BlueprintSchemaVersion> ParseBlueprintSchemaVersion(std::string_view schemaText);

// ---------------------------------------------------------------------------
// Pin / Node / Wire / Asset Serialization Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::string_view BlueprintPinTypeToName(Asset::BlueprintPinType type) noexcept;
[[nodiscard]] std::optional<Asset::BlueprintPinType> ParseBlueprintPinTypeName(std::string_view name) noexcept;

[[nodiscard]] std::string SerializeBlueprintPinSpecJson(const Asset::BlueprintPinSpec& pin);
[[nodiscard]] std::optional<Asset::BlueprintPinSpec> ParseBlueprintPinSpecJson(std::string_view jsonText);

[[nodiscard]] std::string SerializeBlueprintNodeJson(const Asset::BlueprintNode& node);
[[nodiscard]] std::optional<Asset::BlueprintNode> ParseBlueprintNodeJson(std::string_view jsonText);

[[nodiscard]] std::string SerializeBlueprintWireJson(const Asset::BlueprintWire& wire);
[[nodiscard]] std::optional<Asset::BlueprintWire> ParseBlueprintWireJson(std::string_view jsonText);

[[nodiscard]] std::string SerializeFunctionLibraryAsset(const Assets::BlueprintFunctionLibraryAsset& library);
[[nodiscard]] std::optional<Assets::BlueprintFunctionLibraryAsset> ParseFunctionLibraryAsset(
    std::string_view jsonText);

[[nodiscard]] std::string SerializeMacroLibraryAsset(const Assets::BlueprintMacroLibraryAsset& library);
[[nodiscard]] std::optional<Assets::BlueprintMacroLibraryAsset> ParseMacroLibraryAsset(
    std::string_view jsonText);

[[nodiscard]] std::string SerializeInterfaceAsset(const Assets::BlueprintInterfaceAsset& iface);
[[nodiscard]] std::optional<Assets::BlueprintInterfaceAsset> ParseInterfaceAsset(std::string_view jsonText);

// ---------------------------------------------------------------------------
// Migration
// ---------------------------------------------------------------------------

struct BlueprintMigrationResult final {
    bool success = false;
    BlueprintSchemaVersion fromVersion{0U, 0U};
    BlueprintSchemaVersion toVersion = kCurrentBlueprintSchemaVersion;
    std::string migratedJson;
    std::vector<std::string> appliedSteps;
    std::string error;
};

struct BlueprintMigrationStep final {
    BlueprintSchemaVersion fromVersion;
    BlueprintSchemaVersion toVersion;
    std::string description;
    /// Transforms a JSON document string from `fromVersion` to `toVersion`, returning empty on failure.
    std::function<std::optional<std::string>(std::string_view)> transform;
};

class BlueprintMigrationRegistry final {
public:
    /// Constructs a registry pre-populated with built-in legacy schema migrations (e.g. v0 -> v1).
    static BlueprintMigrationRegistry CreateDefault();

    void RegisterStep(BlueprintMigrationStep step);

    [[nodiscard]] BlueprintMigrationResult MigrateJson(
        std::string_view jsonText,
        BlueprintSchemaVersion targetVersion = kCurrentBlueprintSchemaVersion) const;

private:
    std::vector<BlueprintMigrationStep> m_steps;
};

// ---------------------------------------------------------------------------
// Import & Export
// ---------------------------------------------------------------------------

enum class BlueprintExportFormat : std::uint8_t {
    JsonPretty = 0,
    JsonCompact,
};

struct BlueprintImportResult final {
    std::optional<Asset::BlueprintAsset> asset;
    BlueprintSchemaVersion sourceVersion{0U, 0U};
    bool migrated = false;
    std::vector<std::string> notes;
    std::string error;

    [[nodiscard]] bool IsSuccess() const noexcept { return asset.has_value() && error.empty(); }
};

[[nodiscard]] std::string ExportBlueprintAsset(const Asset::BlueprintAsset& blueprint,
                                                  BlueprintExportFormat format);

[[nodiscard]] BlueprintImportResult ImportBlueprintFromJson(
    std::string_view jsonText, const BlueprintMigrationRegistry* customRegistry = nullptr);

} // namespace UVE::Blueprint::Serialization
