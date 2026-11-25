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

struct BlueprintPluginApiVersion final {
    std::uint32_t major = 1U;
    std::uint32_t minor = 0U;
    std::uint32_t patch = 0U;

    [[nodiscard]] bool operator==(const BlueprintPluginApiVersion&) const = default;
    [[nodiscard]] auto operator<=>(const BlueprintPluginApiVersion&) const = default;
};

inline constexpr BlueprintPluginApiVersion kCurrentBlueprintPluginApiVersion{1U, 0U, 0U};

/// Compatible when major versions match and the plugin does not require a newer minor API than the host.
[[nodiscard]] constexpr bool IsBlueprintPluginVersionCompatible(
    const BlueprintPluginApiVersion hostVersion, const BlueprintPluginApiVersion requiredVersion) noexcept {
    return hostVersion.major == requiredVersion.major && requiredVersion.minor <= hostVersion.minor;
}

[[nodiscard]] std::string FormatBlueprintPluginVersion(BlueprintPluginApiVersion version);
[[nodiscard]] std::optional<BlueprintPluginApiVersion> ParseBlueprintPluginVersion(std::string_view text);

// ---------------------------------------------------------------------------
// Manifest & Extension Interface
// ---------------------------------------------------------------------------

struct BlueprintPluginManifest final {
    std::string id;
    std::string displayName;
    std::string author;
    std::string description;
    BlueprintPluginApiVersion pluginVersion{1U, 0U, 0U};
    BlueprintPluginApiVersion requiredApiVersion = kCurrentBlueprintPluginApiVersion;
    std::vector<std::string> dependencies;
    std::vector<std::string> providedNodeKeys;
};

/// Base interface implemented by Blueprint node/tool plugin extensions.
class IBlueprintPluginExtension {
public:
    virtual ~IBlueprintPluginExtension() = default;

    [[nodiscard]] virtual BlueprintPluginManifest GetManifest() const = 0;
    virtual bool OnLoad() { return true; }
    virtual void OnUnload() {}
    virtual void RegisterNodes(std::vector<Asset::BlueprintNodeType>& outNodes) const = 0;
};

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

struct BlueprintPluginValidationResult final {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

[[nodiscard]] BlueprintPluginValidationResult ValidateBlueprintPluginManifest(
    const BlueprintPluginManifest& manifest,
    BlueprintPluginApiVersion hostApiVersion = kCurrentBlueprintPluginApiVersion);

/// Parses a `.uvbpplugin` JSON manifest text.
[[nodiscard]] std::optional<BlueprintPluginManifest> ParseBlueprintPluginManifestJson(std::string_view jsonText);

/// Serializes a plugin manifest to JSON text.
[[nodiscard]] std::string SerializeBlueprintPluginManifestJson(const BlueprintPluginManifest& manifest);

// ---------------------------------------------------------------------------
// Registration, Discovery & Loading
// ---------------------------------------------------------------------------

struct BlueprintPluginStatus final {
    BlueprintPluginManifest manifest;
    bool loaded = false;
    std::size_t contributedNodeCount = 0U;
    std::string lastError;
};

class BlueprintPluginManager final {
public:
    explicit BlueprintPluginManager(
        BlueprintPluginApiVersion hostApiVersion = kCurrentBlueprintPluginApiVersion) noexcept;

    /// Discovers `.uvbpplugin` manifest files in `directory` (non-recursive).
    [[nodiscard]] std::vector<BlueprintPluginManifest> DiscoverManifests(
        const std::filesystem::path& directory) const;

    /// Registers an in-process plugin extension after manifest & compatibility validation.
    bool RegisterExtension(std::shared_ptr<IBlueprintPluginExtension> extension, std::string* outError = nullptr);

    /// Unregisters and unloads the plugin with `pluginId`.
    bool UnregisterExtension(std::string_view pluginId);

    /// Loads `pluginId` (ensuring declared dependencies are loaded first) and registers its nodes.
    bool LoadPlugin(std::string_view pluginId, std::string* outError = nullptr);

    /// Loads all registered extensions in dependency order. Returns number of plugins loaded.
    std::size_t LoadAllPlugins();

    /// Unloads `pluginId` and removes its contributed nodes.
    bool UnloadPlugin(std::string_view pluginId);
    void UnloadAllPlugins();

    [[nodiscard]] std::optional<BlueprintPluginStatus> GetStatus(std::string_view pluginId) const;
    [[nodiscard]] std::vector<BlueprintPluginStatus> ListPlugins() const;
    [[nodiscard]] const std::vector<Asset::BlueprintNodeType>& GetRegisteredNodes() const noexcept {
        return m_registeredNodes;
    }
    [[nodiscard]] const Asset::BlueprintNodeType* FindRegisteredNode(std::string_view nodeKey) const noexcept;

private:
    struct Entry final {
        std::shared_ptr<IBlueprintPluginExtension> extension;
        BlueprintPluginManifest manifest;
        bool loaded = false;
        std::vector<std::string> nodeKeys;
        std::string lastError;
    };

    bool LoadRecursive(std::string_view pluginId, std::vector<std::string>& stack, std::string* outError);
    void RebuildNodeTable();

    BlueprintPluginApiVersion m_hostApiVersion;
    std::map<std::string, Entry, std::less<>> m_entries;
    std::vector<Asset::BlueprintNodeType> m_registeredNodes;
};

} // namespace UVE::Blueprint::Plugins
