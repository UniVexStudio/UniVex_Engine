// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "plugin_registry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Plugins {

inline constexpr std::size_t kNativePluginManifestMaximumIdentifierBytes = 128U;
inline constexpr std::size_t kNativePluginManifestMaximumDisplayNameBytes = 256U;
inline constexpr std::size_t kNativePluginCapabilityPolicyMaximumEntries = 64U;

struct NativePluginCapabilityPolicy final {
    bool allowAllCapabilities = true;
    std::vector<std::string> allowedCapabilityIds;
};

enum class NativePluginAbiNegotiationCode : std::uint8_t {
    Compatible = 0,
    InvalidEngineProtocol,
    UnsupportedPluginProtocol,
};

struct NativePluginAbiNegotiationResult final {
    NativePluginAbiNegotiationCode code = NativePluginAbiNegotiationCode::InvalidEngineProtocol;
    std::uint32_t engineProtocol = 0U;
    std::uint32_t pluginProtocol = 0U;
    std::string message;

    [[nodiscard]] bool IsCompatible() const noexcept {
        return code == NativePluginAbiNegotiationCode::Compatible;
    }
};

enum class NativePluginManifestValidationCode : std::uint8_t {
    InvalidPluginId = 0,
    EmptyDisplayName,
    DisplayNameTooLong,
    UnsupportedProtocol,
    TooManyCapabilities,
    InvalidCapabilityId,
    DuplicateCapabilityId,
    CapabilityPolicyTooLarge,
    DuplicatePolicyCapabilityId,
    CapabilityNotAllowed,
};

struct NativePluginManifestDiagnostic final {
    NativePluginManifestValidationCode code = NativePluginManifestValidationCode::InvalidPluginId;
    std::size_t capabilityIndex = 0U;
    std::string message;
};

struct NativePluginManifestValidationResult final {
    std::vector<NativePluginManifestDiagnostic> diagnostics;

    [[nodiscard]] bool IsValid() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] NativePluginAbiNegotiationResult NegotiateNativePluginAbi(
    const NativePluginManifest& manifest,
    std::uint32_t engineProtocol = kNativePluginProtocolVersion);

[[nodiscard]] NativePluginManifestValidationResult ValidateNativePluginManifest(
    const NativePluginManifest& manifest);

[[nodiscard]] NativePluginManifestValidationResult ValidateNativePluginManifest(
    const NativePluginManifest& manifest,
    const NativePluginCapabilityPolicy& policy);

} // namespace UVE::Plugins
