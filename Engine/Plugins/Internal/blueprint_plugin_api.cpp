// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_plugin_api.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <exception>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace UVE::Blueprint::Plugins {
namespace {

using Json = nlohmann::json;

[[nodiscard]] std::optional<std::uint32_t> ParsePart(const std::string_view part) noexcept {
    if (part.empty()) {
        return std::nullopt;
    }
    std::uint32_t val = 0U;
    const auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), val);
    if (ec != std::errc{} || ptr != part.data() + part.size()) {
        return std::nullopt;
    }
    return val;
}

[[nodiscard]] bool IsValidPluginId(const std::string_view id) noexcept {
    if (id.empty() || id.size() > 128U) {
        return false;
    }
    for (const char c : id) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Version Formatting / Parsing
// ---------------------------------------------------------------------------

std::string FormatBlueprintPluginVersion(const BlueprintPluginApiVersion version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
}

std::optional<BlueprintPluginApiVersion> ParseBlueprintPluginVersion(const std::string_view text) {
    const std::size_t dot1 = text.find('.');
    if (dot1 == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t dot2 = text.find('.', dot1 + 1U);
    if (dot2 == std::string_view::npos) {
        return std::nullopt;
    }
    const auto maj = ParsePart(text.substr(0U, dot1));
    const auto min = ParsePart(text.substr(dot1 + 1U, dot2 - dot1 - 1U));
    const auto pat = ParsePart(text.substr(dot2 + 1U));
    if (!maj.has_value() || !min.has_value() || !pat.has_value()) {
        return std::nullopt;
    }
    return BlueprintPluginApiVersion{*maj, *min, *pat};
}

// ---------------------------------------------------------------------------
// Validation & JSON Manifest Serialization
// ---------------------------------------------------------------------------

BlueprintPluginValidationResult ValidateBlueprintPluginManifest(
    const BlueprintPluginManifest& manifest, const BlueprintPluginApiVersion hostApiVersion) {
    BlueprintPluginValidationResult result;
    if (!IsValidPluginId(manifest.id)) {
        result.valid = false;
        result.errors.push_back("Plugin ID must be 1..128 alphanumeric/dot/underscore/hyphen characters.");
    }
    if (manifest.displayName.empty()) {
        result.valid = false;
        result.errors.push_back("Plugin displayName cannot be empty.");
    }
    if (!IsBlueprintPluginVersionCompatible(hostApiVersion, manifest.requiredApiVersion)) {
        result.valid = false;
        result.errors.push_back("Plugin requires incompatible API version " +
                                FormatBlueprintPluginVersion(manifest.requiredApiVersion) + " (host is " +
                                FormatBlueprintPluginVersion(hostApiVersion) + ").");
    }
    for (const std::string& dep : manifest.dependencies) {
        if (dep == manifest.id) {
            result.valid = false;
            result.errors.push_back("Plugin cannot depend on itself.");
        } else if (!IsValidPluginId(dep)) {
            result.valid = false;
            result.errors.push_back("Invalid dependency plugin ID: " + dep);
        }
    }
    std::set<std::string> seenKeys;
    for (const std::string& key : manifest.providedNodeKeys) {
        if (key.empty()) {
            result.valid = false;
            result.errors.push_back("Provided node key cannot be empty.");
        } else if (!seenKeys.insert(key).second) {
            result.warnings.push_back("Duplicate provided node key in manifest: " + key);
        }
    }
    return result;
}

std::string SerializeBlueprintPluginManifestJson(const BlueprintPluginManifest& manifest) {
    const Json doc{{"id", manifest.id},
                      {"displayName", manifest.displayName},
                      {"author", manifest.author},
                      {"description", manifest.description},
                      {"pluginVersion", FormatBlueprintPluginVersion(manifest.pluginVersion)},
                      {"requiredApiVersion", FormatBlueprintPluginVersion(manifest.requiredApiVersion)},
                      {"dependencies", manifest.dependencies},
                      {"providedNodeKeys", manifest.providedNodeKeys}};
    return doc.dump(2);
}

std::optional<BlueprintPluginManifest> ParseBlueprintPluginManifestJson(const std::string_view jsonText) {
    try {
        const Json doc = Json::parse(jsonText);
        if (!doc.is_object()) {
            return std::nullopt;
        }
        BlueprintPluginManifest manifest;
        manifest.id = doc.value("id", std::string{});
        manifest.displayName = doc.value("displayName", std::string{});
        manifest.author = doc.value("author", std::string{});
        manifest.description = doc.value("description", std::string{});
        if (const auto v = ParseBlueprintPluginVersion(doc.value("pluginVersion", std::string{"1.0.0"}));
            v.has_value()) {
            manifest.pluginVersion = *v;
        } else {
            return std::nullopt;
        }
        if (const auto api = ParseBlueprintPluginVersion(doc.value("requiredApiVersion", std::string{"1.0.0"}));
            api.has_value()) {
            manifest.requiredApiVersion = *api;
        } else {
            return std::nullopt;
        }
        manifest.dependencies = doc.value("dependencies", std::vector<std::string>{});
        manifest.providedNodeKeys = doc.value("providedNodeKeys", std::vector<std::string>{});
        return manifest;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// BlueprintPluginManager
// ---------------------------------------------------------------------------

BlueprintPluginManager::BlueprintPluginManager(const BlueprintPluginApiVersion hostApiVersion) noexcept
    : m_hostApiVersion(hostApiVersion) {}

std::vector<BlueprintPluginManifest> BlueprintPluginManager::DiscoverManifests(
    const std::filesystem::path& directory) const {
    std::vector<BlueprintPluginManifest> found;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec) || ec) {
        return found;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".uvbpplugin") {
            continue;
        }
        std::ifstream in(entry.path());
        if (!in) {
            continue;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (auto parsed = ParseBlueprintPluginManifestJson(text); parsed.has_value()) {
            found.push_back(std::move(*parsed));
        }
    }
    std::sort(found.begin(), found.end(),
              [](const BlueprintPluginManifest& a, const BlueprintPluginManifest& b) { return a.id < b.id; });
    return found;
}

bool BlueprintPluginManager::RegisterExtension(std::shared_ptr<IBlueprintPluginExtension> extension,
                                                     std::string* const outError) {
    if (!extension) {
        if (outError != nullptr) *outError = "Extension pointer is null.";
        return false;
    }
    BlueprintPluginManifest manifest = extension->GetManifest();
    const BlueprintPluginValidationResult validation =
        ValidateBlueprintPluginManifest(manifest, m_hostApiVersion);
    if (!validation.valid) {
        if (outError != nullptr) {
            *outError = validation.errors.empty() ? "Invalid plugin manifest." : validation.errors.front();
        }
        return false;
    }
    if (m_entries.contains(manifest.id)) {
        if (outError != nullptr) *outError = "Plugin already registered: " + manifest.id;
        return false;
    }
    const std::string id = manifest.id;
    m_entries.emplace(id, Entry{std::move(extension), std::move(manifest), false, {}, {}});
    return true;
}

bool BlueprintPluginManager::UnregisterExtension(const std::string_view pluginId) {
    const auto it = m_entries.find(pluginId);
    if (it == m_entries.end()) {
        return false;
    }
    if (it->second.loaded && it->second.extension) {
        it->second.extension->OnUnload();
    }
    m_entries.erase(it);
    RebuildNodeTable();
    return true;
}

bool BlueprintPluginManager::LoadRecursive(const std::string_view pluginId, std::vector<std::string>& stack,
                                                 std::string* const outError) {
    const auto it = m_entries.find(pluginId);
    if (it == m_entries.end()) {
        if (outError != nullptr) *outError = "Plugin not registered: " + std::string(pluginId);
        return false;
    }
    Entry& entry = it->second;
    if (entry.loaded) {
        return true;
    }
    if (std::find(stack.begin(), stack.end(), entry.manifest.id) != stack.end()) {
        entry.lastError = "Cyclic plugin dependency detected involving: " + entry.manifest.id;
        if (outError != nullptr) *outError = entry.lastError;
        return false;
    }
    stack.push_back(entry.manifest.id);

    for (const std::string& depId : entry.manifest.dependencies) {
        std::string depErr;
        if (!LoadRecursive(depId, stack, &depErr)) {
            entry.lastError = "Missing or failed dependency '" + depId + "': " + depErr;
            if (outError != nullptr) *outError = entry.lastError;
            stack.pop_back();
            return false;
        }
    }

    if (!entry.extension || !entry.extension->OnLoad()) {
        entry.lastError = "Plugin OnLoad returned false.";
        if (outError != nullptr) *outError = entry.lastError;
        stack.pop_back();
        return false;
    }

    entry.loaded = true;
    entry.lastError.clear();
    stack.pop_back();
    return true;
}

bool BlueprintPluginManager::LoadPlugin(const std::string_view pluginId, std::string* const outError) {
    std::vector<std::string> stack;
    const bool ok = LoadRecursive(pluginId, stack, outError);
    if (ok) {
        RebuildNodeTable();
    }
    return ok;
}

std::size_t BlueprintPluginManager::LoadAllPlugins() {
    std::size_t loadedCount = 0U;
    for (const auto& pair : m_entries) {
        if (LoadPlugin(pair.first)) {
            ++loadedCount;
        }
    }
    return loadedCount;
}

bool BlueprintPluginManager::UnloadPlugin(const std::string_view pluginId) {
    const auto it = m_entries.find(pluginId);
    if (it == m_entries.end() || !it->second.loaded) {
        return false;
    }
    if (it->second.extension) {
        it->second.extension->OnUnload();
    }
    it->second.loaded = false;
    it->second.nodeKeys.clear();
    RebuildNodeTable();
    return true;
}

void BlueprintPluginManager::UnloadAllPlugins() {
    for (auto& pair : m_entries) {
        if (pair.second.loaded && pair.second.extension) {
            pair.second.extension->OnUnload();
        }
        pair.second.loaded = false;
        pair.second.nodeKeys.clear();
    }
    m_registeredNodes.clear();
}

void BlueprintPluginManager::RebuildNodeTable() {
    m_registeredNodes.clear();
    std::set<std::string> usedKeys;
    for (auto& pair : m_entries) {
        Entry& entry = pair.second;
        entry.nodeKeys.clear();
        if (!entry.loaded || !entry.extension) {
            continue;
        }
        std::vector<Asset::BlueprintNodeType> contributed;
        entry.extension->RegisterNodes(contributed);
        for (Asset::BlueprintNodeType& node : contributed) {
            if (node.key.empty() || Asset::FindBlueprintNodeType(node.key) != nullptr ||
                !usedKeys.insert(node.key).second) {
                continue;
            }
            entry.nodeKeys.push_back(node.key);
            m_registeredNodes.push_back(std::move(node));
        }
    }
}

std::optional<BlueprintPluginStatus> BlueprintPluginManager::GetStatus(
    const std::string_view pluginId) const {
    const auto it = m_entries.find(pluginId);
    if (it == m_entries.end()) {
        return std::nullopt;
    }
    return BlueprintPluginStatus{it->second.manifest, it->second.loaded, it->second.nodeKeys.size(),
                                    it->second.lastError};
}

std::vector<BlueprintPluginStatus> BlueprintPluginManager::ListPlugins() const {
    std::vector<BlueprintPluginStatus> out;
    out.reserve(m_entries.size());
    for (const auto& pair : m_entries) {
        out.push_back(BlueprintPluginStatus{pair.second.manifest, pair.second.loaded, pair.second.nodeKeys.size(),
                                               pair.second.lastError});
    }
    return out;
}

const Asset::BlueprintNodeType* BlueprintPluginManager::FindRegisteredNode(
    const std::string_view nodeKey) const noexcept {
    for (const Asset::BlueprintNodeType& node : m_registeredNodes) {
        if (node.key == nodeKey) {
            return &node;
        }
    }
    return nullptr;
}

} // namespace UVE::Blueprint::Plugins
