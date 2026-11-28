// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "plugin_manifest_validation.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace UVE::Plugins {
namespace {

[[nodiscard]] bool IsValidIdentifier(const std::string& value) noexcept {
    if (value.empty() || value.size() > kNativePluginManifestMaximumIdentifierBytes) {
        return false;
    }
    return std::all_of(value.cbegin(), value.cend(), [](const char character) {
        const unsigned char byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '_' || character == '-' || character == '.';
    });
}

} // namespace

NativePluginAbiNegotiationResult NegotiateNativePluginAbi(const NativePluginManifest& manifest,
                                                               const std::uint32_t engineProtocol) {
    if (engineProtocol == 0U) {
        return {NativePluginAbiNegotiationCode::InvalidEngineProtocol,
                engineProtocol,
                manifest.requiredEngineProtocol,
                "Engine plugin protocol must be non-zero."};
    }
    if (manifest.requiredEngineProtocol != engineProtocol) {
        return {NativePluginAbiNegotiationCode::UnsupportedPluginProtocol,
                engineProtocol,
                manifest.requiredEngineProtocol,
                "Plugin manifest requires an unsupported engine protocol."};
    }
    return {NativePluginAbiNegotiationCode::Compatible,
            engineProtocol,
            manifest.requiredEngineProtocol,
            "Plugin manifest protocol is compatible."};
}

NativePluginManifestValidationResult ValidateNativePluginManifest(
    const NativePluginManifest& manifest) {
    return ValidateNativePluginManifest(manifest, NativePluginCapabilityPolicy{});
}

NativePluginManifestValidationResult ValidateNativePluginManifest(
    const NativePluginManifest& manifest,
    const NativePluginCapabilityPolicy& policy) {
    NativePluginManifestValidationResult result;
    if (!IsValidIdentifier(manifest.pluginId)) {
        result.diagnostics.push_back({NativePluginManifestValidationCode::InvalidPluginId, 0U,
                                      "Plugin ID must be a bounded identifier using letters, digits, '_', '-', or '.'."});
        return result;
    }
    if (manifest.displayName.empty()) {
        result.diagnostics.push_back({NativePluginManifestValidationCode::EmptyDisplayName, 0U,
                                      "Plugin display name must not be empty."});
        return result;
    }
    if (manifest.displayName.size() > kNativePluginManifestMaximumDisplayNameBytes) {
        result.diagnostics.push_back({NativePluginManifestValidationCode::DisplayNameTooLong, 0U,
                                      "Plugin display name exceeds the bounded manifest limit."});
        return result;
    }
    const NativePluginAbiNegotiationResult abi = NegotiateNativePluginAbi(manifest);
    if (!abi.IsCompatible()) {
        result.diagnostics.push_back({NativePluginManifestValidationCode::UnsupportedProtocol, 0U, abi.message});
        return result;
    }
    if (manifest.capabilityIds.size() > NativePluginRegistry::kMaximumCapabilitiesPerPlugin) {
        result.diagnostics.push_back({NativePluginManifestValidationCode::TooManyCapabilities, 0U,
                                      "Plugin manifest exceeds the bounded capability limit."});
        return result;
    }
    if (policy.allowedCapabilityIds.size() > kNativePluginCapabilityPolicyMaximumEntries) {
        result.diagnostics.push_back({NativePluginManifestValidationCode::CapabilityPolicyTooLarge, 0U,
                                      "Plugin capability policy exceeds the bounded allowlist limit."});
        return result;
    }

    std::unordered_set<std::string> capabilities;
    for (std::size_t index = 0U; index < manifest.capabilityIds.size(); ++index) {
        const std::string& capability = manifest.capabilityIds[index];
        if (!IsValidIdentifier(capability)) {
            result.diagnostics.push_back({NativePluginManifestValidationCode::InvalidCapabilityId, index,
                                          "Plugin capability must be a bounded identifier."});
            return result;
        }
        if (!capabilities.insert(capability).second) {
            result.diagnostics.push_back({NativePluginManifestValidationCode::DuplicateCapabilityId, index,
                                          "Plugin capabilities must be unique."});
            return result;
        }
    }

    if (policy.allowAllCapabilities) {
        return result;
    }
    std::unordered_set<std::string> allowedCapabilities;
    for (std::size_t index = 0U; index < policy.allowedCapabilityIds.size(); ++index) {
        const std::string& allowedCapability = policy.allowedCapabilityIds[index];
        if (!IsValidIdentifier(allowedCapability)) {
            result.diagnostics.push_back({NativePluginManifestValidationCode::InvalidCapabilityId, index,
                                          "Plugin capability policy contains an invalid identifier."});
            return result;
        }
        if (!allowedCapabilities.insert(allowedCapability).second) {
            result.diagnostics.push_back({NativePluginManifestValidationCode::DuplicatePolicyCapabilityId, index,
                                          "Plugin capability policy entries must be unique."});
            return result;
        }
    }
    for (std::size_t index = 0U; index < manifest.capabilityIds.size(); ++index) {
        if (!allowedCapabilities.contains(manifest.capabilityIds[index])) {
            result.diagnostics.push_back({NativePluginManifestValidationCode::CapabilityNotAllowed, index,
                                          "Plugin capability is not allowed by the active policy."});
            return result;
        }
    }
    return result;
}

} // namespace UVE::Plugins
