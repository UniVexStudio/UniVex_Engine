// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "plugin_registry.h"
#include "plugin_manifest_validation.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace UVE::Plugins {

NativePluginRegistryResult NativePluginRegistry::RegisterManifest(NativePluginManifest manifest) {
    return RegisterManifest(std::move(manifest), NativePluginCapabilityPolicy{});
}

NativePluginRegistryResult NativePluginRegistry::RegisterManifest(
    NativePluginManifest manifest, const NativePluginCapabilityPolicy& policy) {
    const NativePluginManifestValidationResult validation = ValidateNativePluginManifest(manifest, policy);
    if (!validation.IsValid() || m_entries.contains(manifest.pluginId) ||
        m_entries.size() >= kMaximumPlugins) {
        return {NativePluginRegistryCode::Rejected,
                validation.IsValid() ? "Plugin manifest conflicts with an existing bounded registry entry."
                                        : validation.diagnostics.front().message};
    }
    const std::string pluginId = manifest.pluginId;
    m_entries.emplace(pluginId, Entry{std::move(manifest), 0U, false});
    return {NativePluginRegistryCode::Accepted, "Plugin manifest registered."};
}

std::optional<NativePluginRegistrationScope> NativePluginRegistry::OpenScope(
    const std::string_view pluginId) {
    const auto iterator = m_entries.find(std::string(pluginId));
    if (iterator == m_entries.end() || iterator->second.scopeOpen) {
        return std::nullopt;
    }
    if (m_nextGeneration == 0U) {
        return std::nullopt;
    }
    iterator->second.generation = m_nextGeneration++;
    iterator->second.scopeOpen = true;
    return NativePluginRegistrationScope{iterator->second.manifest.pluginId, iterator->second.generation};
}

NativePluginRegistryResult NativePluginRegistry::CloseScope(
    const NativePluginRegistrationScope& scope) {
    if (!scope.IsValid()) {
        return {NativePluginRegistryCode::InvalidScope, "The plugin registration scope is invalid."};
    }
    const auto iterator = m_entries.find(scope.pluginId);
    if (iterator == m_entries.end()) {
        return {NativePluginRegistryCode::NotFound, "The plugin manifest is not registered."};
    }
    if (!iterator->second.scopeOpen || iterator->second.generation != scope.generation) {
        return {NativePluginRegistryCode::InvalidScope, "The plugin registration scope is stale or already closed."};
    }
    iterator->second.scopeOpen = false;
    iterator->second.generation = 0U;
    return {NativePluginRegistryCode::Accepted, "Plugin registration scope closed."};
}

NativePluginRegistryResult NativePluginRegistry::UnregisterManifest(const std::string_view pluginId) {
    if (pluginId.empty()) {
        return {NativePluginRegistryCode::NotFound, "The plugin identifier is empty."};
    }
    const auto iterator = m_entries.find(std::string(pluginId));
    if (iterator == m_entries.end()) {
        return {NativePluginRegistryCode::NotFound, "The plugin manifest is not registered."};
    }
    if (iterator->second.scopeOpen) {
        return {NativePluginRegistryCode::Busy, "The plugin registration scope must be closed before removal."};
    }
    m_entries.erase(iterator);
    return {NativePluginRegistryCode::Accepted, "Plugin manifest unregistered."};
}

const NativePluginManifest* NativePluginRegistry::FindManifest(
    const std::string_view pluginId) const noexcept {
    const auto iterator = m_entries.find(std::string(pluginId));
    return iterator == m_entries.end() ? nullptr : &iterator->second.manifest;
}

bool NativePluginRegistry::IsScopeOpen(const std::string_view pluginId) const noexcept {
    const auto iterator = m_entries.find(std::string(pluginId));
    return iterator != m_entries.end() && iterator->second.scopeOpen;
}

std::size_t NativePluginRegistry::GetManifestCount() const noexcept {
    return m_entries.size();
}

std::size_t NativePluginRegistry::GetOpenScopeCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(m_entries.cbegin(), m_entries.cend(), [](const auto& entry) {
        return entry.second.scopeOpen;
    }));
}

} // namespace UVE::Plugins
