// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/plugins/native_plugin_host_uve.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <limits>
#include <system_error>
#include <utility>

namespace UVE::Plugins {
namespace {

// Memory-safety scan bounds for foreign strings: generous on purpose. The manifest validator
// enforces the real policy limits afterwards; these only stop the host from reading past a
// missing NUL into unmapped memory.
inline constexpr std::size_t kForeignIdentifierScanBytesUVE = 1024U;
inline constexpr std::size_t kForeignDisplayNameScanBytesUVE = 4096U;
inline constexpr std::size_t kForeignCapabilityScanBytesUVE = 1024U;

[[nodiscard]] bool CopyForeignStringUVE(const char* text, const std::size_t scanBytes, std::string& out) {
    out.clear();
    if (text == nullptr) {
        return false;
    }
    std::size_t length = 0U;
    while (length < scanBytes && text[length] != '\0') {
        ++length;
    }
    if (length >= scanBytes) {
        return false;
    }
    out.assign(text, length);
    return true;
}

[[nodiscard]] void* OpenLibraryUVE(const std::filesystem::path& path, std::string& outError) {
#ifdef _WIN32
    const std::u8string utf8 = path.u8string();
    if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        outError = "The plugin path is too long to open.";
        return nullptr;
    }
    const int wideLength =
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(utf8.c_str()),
                              static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLength <= 0) {
        outError = "The plugin path is not valid UTF-8.";
        return nullptr;
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(utf8.c_str()),
                              static_cast<int>(utf8.size()), wide.data(), wideLength) != wideLength) {
        outError = "The plugin path could not be converted for loading.";
        return nullptr;
    }
    void* handle = ::LoadLibraryW(wide.c_str());
    if (handle == nullptr) {
        outError = "LoadLibraryW failed (error " + std::to_string(::GetLastError()) + ").";
    }
    return handle;
#else
    static_cast<void>(::dlerror());
    void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* error = ::dlerror();
        outError = error != nullptr ? error : "dlopen failed with no diagnostic.";
    }
    return handle;
#endif
}

[[nodiscard]] void* FindSymbolUVE(void* handle, const char* name) noexcept {
#ifdef _WIN32
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return ::dlsym(handle, name);
#endif
}

void CloseLibraryUVE(void* handle) noexcept {
#ifdef _WIN32
    static_cast<void>(::FreeLibrary(static_cast<HMODULE>(handle)));
#else
    static_cast<void>(::dlclose(handle));
#endif
}

} // namespace

NativePluginDescriptorResultUVE ValidateNativePluginDescriptorUVE(
    const NativePluginDescriptorUVE* descriptor, NativePluginManifestUVE& outManifest) {
    outManifest = NativePluginManifestUVE{};
    if (descriptor == nullptr) {
        return {NativePluginDescriptorCodeUVE::NullDescriptor, "The plugin entry point returned null."};
    }
    if (descriptor->abiVersion != kNativePluginProtocolVersionUVE) {
        return {NativePluginDescriptorCodeUVE::UnsupportedAbi,
                "The plugin speaks ABI v" + std::to_string(descriptor->abiVersion) + "; the host speaks v" +
                    std::to_string(kNativePluginProtocolVersionUVE) + "."};
    }
    NativePluginManifestUVE manifest;
    manifest.requiredEngineProtocol = descriptor->requiredEngineProtocol;
    manifest.version = NativePluginVersionUVE{descriptor->versionMajor, descriptor->versionMinor,
                                              descriptor->versionPatch};
    if (!CopyForeignStringUVE(descriptor->pluginId, kForeignIdentifierScanBytesUVE, manifest.pluginId)) {
        return {NativePluginDescriptorCodeUVE::InvalidIdentifier,
                "The plugin identifier is null or not NUL-terminated."};
    }
    if (!CopyForeignStringUVE(descriptor->displayName, kForeignDisplayNameScanBytesUVE, manifest.displayName)) {
        return {NativePluginDescriptorCodeUVE::InvalidDisplayName,
                "The plugin display name is null or not NUL-terminated."};
    }
    if (descriptor->capabilityCount > NativePluginRegistryUVE::kMaximumCapabilitiesPerPluginUVE) {
        return {NativePluginDescriptorCodeUVE::TooManyCapabilities,
                "The plugin claims " + std::to_string(descriptor->capabilityCount) + " capabilities; at most " +
                    std::to_string(NativePluginRegistryUVE::kMaximumCapabilitiesPerPluginUVE) + " are read."};
    }
    if (descriptor->capabilityCount > 0U && descriptor->capabilityIds == nullptr) {
        return {NativePluginDescriptorCodeUVE::InvalidCapabilityId,
                "The plugin claims capabilities but provides no capability array."};
    }
    for (std::uint32_t index = 0U; index < descriptor->capabilityCount; ++index) {
        std::string capability;
        if (!CopyForeignStringUVE(descriptor->capabilityIds[index], kForeignCapabilityScanBytesUVE, capability)) {
            return {NativePluginDescriptorCodeUVE::InvalidCapabilityId,
                    "Capability " + std::to_string(index) + " is null or not NUL-terminated."};
        }
        manifest.capabilityIds.push_back(std::move(capability));
    }
    outManifest = std::move(manifest);
    return {NativePluginDescriptorCodeUVE::Valid, {}};
}

NativePluginHostUVE::NativePluginHostUVE(NativePluginRegistryUVE& registry) noexcept : m_registry(registry) {}

NativePluginHostUVE::~NativePluginHostUVE() {
    try {
        UnloadAllUVE();
    } catch (...) {
        // Best effort by design: destruction must not throw.
    }
}

NativePluginLoadResultUVE NativePluginHostUVE::LoadUVE(const std::filesystem::path& libraryPath,
                                                       const NativePluginCapabilityPolicyUVE& policy) {
    std::error_code error;
    const bool exists = std::filesystem::exists(libraryPath, error);
    if (error || !exists) {
        return {NativePluginLoadCodeUVE::FileNotFound,
                "No plugin file at '" + libraryPath.generic_string() + "'.", {}};
    }
    std::string openError;
    void* handle = OpenLibraryUVE(libraryPath, openError);
    if (handle == nullptr) {
        return {NativePluginLoadCodeUVE::OpenFailed, openError, {}};
    }
    const auto entry = reinterpret_cast<NativePluginEntryFnUVE>(
        FindSymbolUVE(handle, kNativePluginEntrySymbolUVE.data()));
    if (entry == nullptr) {
        CloseLibraryUVE(handle);
        return {NativePluginLoadCodeUVE::EntryMissing,
                "The library exports no '" + std::string(kNativePluginEntrySymbolUVE) + "'.", {}};
    }
    const NativePluginDescriptorUVE* descriptor = nullptr;
    try {
        descriptor = entry();
    } catch (...) {
        CloseLibraryUVE(handle);
        return {NativePluginLoadCodeUVE::InvalidDescriptor, "The plugin entry point threw an exception.", {}};
    }
    NativePluginManifestUVE manifest;
    const NativePluginDescriptorResultUVE shape = ValidateNativePluginDescriptorUVE(descriptor, manifest);
    if (!shape.IsValidUVE()) {
        CloseLibraryUVE(handle);
        const NativePluginLoadCodeUVE code =
            shape.code == NativePluginDescriptorCodeUVE::UnsupportedAbi
                ? NativePluginLoadCodeUVE::UnsupportedProtocol
                : NativePluginLoadCodeUVE::InvalidDescriptor;
        return {code, shape.message, {}};
    }
    if (m_loaded.find(manifest.pluginId) != m_loaded.end()) {
        CloseLibraryUVE(handle);
        return {NativePluginLoadCodeUVE::ManifestRejected,
                "The plugin '" + manifest.pluginId + "' is already loaded.", {}};
    }
    // Negotiation before validation: protocol compatibility is the cheapest disqualifier, and it
    // keeps UnsupportedProtocol reachable (validation also rejects unknown protocols, so running it
    // first would swallow every such failure into ManifestRejected).
    const NativePluginAbiNegotiationResultUVE negotiated = NegotiateNativePluginAbiUVE(manifest);
    if (!negotiated.IsCompatibleUVE()) {
        CloseLibraryUVE(handle);
        return {NativePluginLoadCodeUVE::UnsupportedProtocol, negotiated.message, {}};
    }
    const NativePluginManifestValidationResultUVE valid = ValidateNativePluginManifestUVE(manifest, policy);
    if (!valid.IsValidUVE()) {
        CloseLibraryUVE(handle);
        return {NativePluginLoadCodeUVE::ManifestRejected,
                "The manifest is invalid: " + valid.diagnostics.front().message, {}};
    }
    const std::string pluginId = manifest.pluginId;
    const NativePluginRegistryResultUVE registered = m_registry.RegisterManifestUVE(std::move(manifest), policy);
    if (!registered.IsAcceptedUVE()) {
        CloseLibraryUVE(handle);
        return {NativePluginLoadCodeUVE::ManifestRejected, registered.message, {}};
    }
    if (descriptor->initialize != nullptr) {
        int status = 0;
        bool threw = false;
        try {
            status = descriptor->initialize();
        } catch (...) {
            threw = true;
        }
        if (threw || status != 0) {
            static_cast<void>(m_registry.UnregisterManifestUVE(pluginId));
            CloseLibraryUVE(handle);
            return {NativePluginLoadCodeUVE::InitializeFailed,
                    threw ? "The plugin initialize function threw an exception."
                          : "The plugin initialize function returned " + std::to_string(status) + ".",
                    {}};
        }
    }
    m_loaded.emplace(pluginId, LoadedPluginUVE{handle, descriptor->shutdown});
    m_loadOrder.push_back(pluginId);
    return {NativePluginLoadCodeUVE::Loaded, "The plugin '" + pluginId + "' loaded.", pluginId};
}

bool NativePluginHostUVE::UnloadUVE(const std::string_view pluginId) {
    const auto loaded = m_loaded.find(std::string(pluginId));
    if (loaded == m_loaded.end()) {
        return false;
    }
    const NativePluginRegistryResultUVE removed = m_registry.UnregisterManifestUVE(loaded->first);
    if (removed.code == NativePluginRegistryCodeUVE::Busy) {
        return false;
    }
    if (loaded->second.shutdown != nullptr) {
        try {
            loaded->second.shutdown();
        } catch (...) {
            // A throwing shutdown cannot stop an unload already past unregistering.
        }
    }
    CloseLibraryUVE(loaded->second.handle);
    std::erase(m_loadOrder, loaded->first);
    m_loaded.erase(loaded);
    return true;
}

void NativePluginHostUVE::UnloadAllUVE() {
    const std::vector<std::string> order = m_loadOrder;
    for (auto id = order.rbegin(); id != order.rend(); ++id) {
        static_cast<void>(UnloadUVE(*id));
    }
}

bool NativePluginHostUVE::IsLoadedUVE(const std::string_view pluginId) const noexcept {
    return m_loaded.find(std::string(pluginId)) != m_loaded.end();
}

std::size_t NativePluginHostUVE::LoadedCountUVE() const noexcept {
    return m_loaded.size();
}

} // namespace UVE::Plugins
