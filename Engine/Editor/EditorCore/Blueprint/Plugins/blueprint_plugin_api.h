// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_nodes.h"

namespace UVE::Blueprint::Plugins {

// ---------------------------------------------------------------------------
// API Version & Compatibility
// ---------------------------------------------------------------------------

struct BlueprintPluginApiVersionUVE final {
    std::uint32_t major = 1U;
    std::uint32_t minor = 0U;
    std::uint32_t patch = 0U;

    [[nodiscard]] bool operator==(const BlueprintPluginApiVersionUVE&) const = default;
    [[nodiscard]] auto operator<=>(const BlueprintPluginApiVersionUVE&) const = default;
};

inline constexpr BlueprintPluginApiVersionUVE kCurrentBlueprintPluginApiVersionUVE{1U, 0U, 0U};

/// Compatible when major versions match and the plugin does not require a newer minor API than the host.
[[nodiscard]] constexpr bool IsBlueprintPluginVersionCompatibleUVE(
    const BlueprintPluginApiVersionUVE hostVersion, const BlueprintPluginApiVersionUVE requiredVersion) noexcept {
    return hostVersion.major == requiredVersion.major && requiredVersion.minor <= hostVersion.minor;
}

[[nodiscard]] std::string FormatBlueprintPluginVersionUVE(BlueprintPluginApiVersionUVE version);
[[nodiscard]] std::optional<BlueprintPluginApiVersionUVE> ParseBlueprintPluginVersionUVE(std::string_view text);

// ---------------------------------------------------------------------------
// Manifest & Extension Interface
// ---------------------------------------------------------------------------

struct BlueprintPluginManifestUVE final {
    std::string id;
    std::string displayName;
    std::string author;
    std::string description;
    BlueprintPluginApiVersionUVE pluginVersion{1U, 0U, 0U};
    BlueprintPluginApiVersionUVE requiredApiVersion = kCurrentBlueprintPluginApiVersionUVE;
    std::vector<std::string> dependencies;
    std::vector<std::string> providedNodeKeys;
};

/// Base interface implemented by Blueprint node/tool plugin extensions.
class IBlueprintPluginExtensionUVE {
public:
    virtual ~IBlueprintPluginExtensionUVE() = default;

    [[nodiscard]] virtual BlueprintPluginManifestUVE GetManifestUVE() const = 0;
    virtual bool OnLoadUVE() { return true; }
    virtual void OnUnloadUVE() {}
    virtual void RegisterNodesUVE(std::vector<Asset::BlueprintNodeTypeUVE>& outNodes) const = 0;
};

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

struct BlueprintPluginValidationResultUVE final {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

[[nodiscard]] BlueprintPluginValidationResultUVE ValidateBlueprintPluginManifestUVE(
    const BlueprintPluginManifestUVE& manifest,
    BlueprintPluginApiVersionUVE hostApiVersion = kCurrentBlueprintPluginApiVersionUVE);

/// Parses a `.uvbpplugin` JSON manifest text.
[[nodiscard]] std::optional<BlueprintPluginManifestUVE> ParseBlueprintPluginManifestJsonUVE(std::string_view jsonText);

/// Serializes a plugin manifest to JSON text.
[[nodiscard]] std::string SerializeBlueprintPluginManifestJsonUVE(const BlueprintPluginManifestUVE& manifest);

// ---------------------------------------------------------------------------
// Registration, Discovery & Loading
// ---------------------------------------------------------------------------

struct BlueprintPluginStatusUVE final {
    BlueprintPluginManifestUVE manifest;
    bool loaded = false;
    std::size_t contributedNodeCount = 0U;
    std::string lastError;
};

class BlueprintPluginManagerUVE final {
public:
    explicit BlueprintPluginManagerUVE(
        BlueprintPluginApiVersionUVE hostApiVersion = kCurrentBlueprintPluginApiVersionUVE) noexcept;

    /// Discovers `.uvbpplugin` manifest files in `directory` (non-recursive).
    [[nodiscard]] std::vector<BlueprintPluginManifestUVE> DiscoverManifestsUVE(
        const std::filesystem::path& directory) const;

    /// Registers an in-process plugin extension after manifest & compatibility validation.
    bool RegisterExtensionUVE(std::shared_ptr<IBlueprintPluginExtensionUVE> extension, std::string* outError = nullptr);

    /// Unregisters and unloads the plugin with `pluginId`.
    bool UnregisterExtensionUVE(std::string_view pluginId);

    /// Loads `pluginId` (ensuring declared dependencies are loaded first) and registers its nodes.
    bool LoadPluginUVE(std::string_view pluginId, std::string* outError = nullptr);

    /// Loads all registered extensions in dependency order. Returns number of plugins loaded.
    std::size_t LoadAllPluginsUVE();

    /// Unloads `pluginId` and removes its contributed nodes.
    bool UnloadPluginUVE(std::string_view pluginId);
    void UnloadAllPluginsUVE();

    [[nodiscard]] std::optional<BlueprintPluginStatusUVE> GetStatusUVE(std::string_view pluginId) const;
    [[nodiscard]] std::vector<BlueprintPluginStatusUVE> ListPluginsUVE() const;
    [[nodiscard]] const std::vector<Asset::BlueprintNodeTypeUVE>& GetRegisteredNodesUVE() const noexcept {
        return m_registeredNodes;
    }
    [[nodiscard]] const Asset::BlueprintNodeTypeUVE* FindRegisteredNodeUVE(std::string_view nodeKey) const noexcept;

private:
    struct EntryUVE final {
        std::shared_ptr<IBlueprintPluginExtensionUVE> extension;
        BlueprintPluginManifestUVE manifest;
        bool loaded = false;
        std::vector<std::string> nodeKeys;
        std::string lastError;
    };

    bool LoadRecursiveUVE(std::string_view pluginId, std::vector<std::string>& stack, std::string* outError);
    void RebuildNodeTableUVE();

    BlueprintPluginApiVersionUVE m_hostApiVersion;
    std::map<std::string, EntryUVE, std::less<>> m_entries;
    std::vector<Asset::BlueprintNodeTypeUVE> m_registeredNodes;
};

} // namespace UVE::Blueprint::Plugins
