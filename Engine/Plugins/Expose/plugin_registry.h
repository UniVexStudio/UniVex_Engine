// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace UVE::Plugins {

struct NativePluginCapabilityPolicy;

inline constexpr std::uint32_t kNativePluginProtocolVersion = 1U;

enum class NativePluginRegistryCode : std::uint8_t {
    Accepted = 0,
    Rejected,
    NotFound,
    Busy,
    InvalidScope,
};

struct NativePluginVersion final {
    std::uint16_t major = 0U;
    std::uint16_t minor = 0U;
    std::uint16_t patch = 0U;

    [[nodiscard]] constexpr bool operator==(const NativePluginVersion&) const noexcept = default;
};

struct NativePluginManifest final {
    std::string pluginId;
    std::string displayName;
    NativePluginVersion version;
    std::uint32_t requiredEngineProtocol = kNativePluginProtocolVersion;
    std::vector<std::string> capabilityIds;

    [[nodiscard]] bool operator==(const NativePluginManifest&) const = default;
};

struct NativePluginRegistrationScope final {
    std::string pluginId;
    std::uint64_t generation = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return !pluginId.empty() && generation != 0U;
    }
};

struct NativePluginRegistryResult final {
    NativePluginRegistryCode code = NativePluginRegistryCode::Rejected;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == NativePluginRegistryCode::Accepted;
    }
};

class NativePluginRegistry final {
public:
    static constexpr std::size_t kMaximumPlugins = 64U;
    static constexpr std::size_t kMaximumCapabilitiesPerPlugin = 32U;

    NativePluginRegistry() = default;
    NativePluginRegistry(const NativePluginRegistry&) = delete;
    NativePluginRegistry& operator=(const NativePluginRegistry&) = delete;

    [[nodiscard]] NativePluginRegistryResult RegisterManifest(NativePluginManifest manifest);
    [[nodiscard]] NativePluginRegistryResult RegisterManifest(
        NativePluginManifest manifest, const NativePluginCapabilityPolicy& policy);
    [[nodiscard]] std::optional<NativePluginRegistrationScope> OpenScope(std::string_view pluginId);
    [[nodiscard]] NativePluginRegistryResult CloseScope(const NativePluginRegistrationScope& scope);
    [[nodiscard]] NativePluginRegistryResult UnregisterManifest(std::string_view pluginId);
    [[nodiscard]] const NativePluginManifest* FindManifest(std::string_view pluginId) const noexcept;
    [[nodiscard]] bool IsScopeOpen(std::string_view pluginId) const noexcept;
    [[nodiscard]] std::size_t GetManifestCount() const noexcept;
    [[nodiscard]] std::size_t GetOpenScopeCount() const noexcept;

private:
    struct Entry final {
        NativePluginManifest manifest;
        std::uint64_t generation = 0U;
        bool scopeOpen = false;
    };

    std::unordered_map<std::string, Entry> m_entries;
    std::uint64_t m_nextGeneration = 1U;
};

} // namespace UVE::Plugins
