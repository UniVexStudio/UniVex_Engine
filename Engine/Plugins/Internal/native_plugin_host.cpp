// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "native_plugin_host.h"

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
inline constexpr std::size_t kForeignIdentifierScanBytes = 1024U;
inline constexpr std::size_t kForeignDisplayNameScanBytes = 4096U;
inline constexpr std::size_t kForeignCapabilityScanBytes = 1024U;

[[nodiscard]] bool CopyForeignString(const char* text, const std::size_t scanBytes, std::string& out) {
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

[[nodiscard]] void* OpenLibrary(const std::filesystem::path& path, std::string& outError) {
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

[[nodiscard]] void* FindSymbol(void* handle, const char* name) noexcept {
#ifdef _WIN32
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return ::dlsym(handle, name);
#endif
}

void CloseLibrary(void* handle) noexcept {
#ifdef _WIN32
    static_cast<void>(::FreeLibrary(static_cast<HMODULE>(handle)));
#else
    static_cast<void>(::dlclose(handle));
#endif
}

} // namespace

NativePluginDescriptorResult ValidateNativePluginDescriptor(
    const NativePluginDescriptor* descriptor, NativePluginManifest& outManifest) {
    outManifest = NativePluginManifest{};
    if (descriptor == nullptr) {
        return {NativePluginDescriptorCode::NullDescriptor, "The plugin entry point returned null."};
    }
    if (descriptor->abiVersion != kNativePluginProtocolVersion) {
        return {NativePluginDescriptorCode::UnsupportedAbi,
                "The plugin speaks ABI v" + std::to_string(descriptor->abiVersion) + "; the host speaks v" +
                    std::to_string(kNativePluginProtocolVersion) + "."};
    }
    NativePluginManifest manifest;
    manifest.requiredEngineProtocol = descriptor->requiredEngineProtocol;
    manifest.version = NativePluginVersion{descriptor->versionMajor, descriptor->versionMinor,
                                              descriptor->versionPatch};
    if (!CopyForeignString(descriptor->pluginId, kForeignIdentifierScanBytes, manifest.pluginId)) {
        return {NativePluginDescriptorCode::InvalidIdentifier,
                "The plugin identifier is null or not NUL-terminated."};
    }
    if (!CopyForeignString(descriptor->displayName, kForeignDisplayNameScanBytes, manifest.displayName)) {
        return {NativePluginDescriptorCode::InvalidDisplayName,
                "The plugin display name is null or not NUL-terminated."};
    }
    if (descriptor->capabilityCount > NativePluginRegistry::kMaximumCapabilitiesPerPlugin) {
        return {NativePluginDescriptorCode::TooManyCapabilities,
                "The plugin claims " + std::to_string(descriptor->capabilityCount) + " capabilities; at most " +
                    std::to_string(NativePluginRegistry::kMaximumCapabilitiesPerPlugin) + " are read."};
    }
    if (descriptor->capabilityCount > 0U && descriptor->capabilityIds == nullptr) {
        return {NativePluginDescriptorCode::InvalidCapabilityId,
                "The plugin claims capabilities but provides no capability array."};
    }
    for (std::uint32_t index = 0U; index < descriptor->capabilityCount; ++index) {
        std::string capability;
        if (!CopyForeignString(descriptor->capabilityIds[index], kForeignCapabilityScanBytes, capability)) {
            return {NativePluginDescriptorCode::InvalidCapabilityId,
                    "Capability " + std::to_string(index) + " is null or not NUL-terminated."};
        }
        manifest.capabilityIds.push_back(std::move(capability));
    }
    outManifest = std::move(manifest);
    return {NativePluginDescriptorCode::Valid, {}};
}

NativePluginHost::NativePluginHost(NativePluginRegistry& registry) noexcept : m_registry(registry) {}

NativePluginHost::~NativePluginHost() {
    try {
        UnloadAll();
    } catch (...) {
        // Best effort by design: destruction must not throw.
    }
}

NativePluginLoadResult NativePluginHost::Load(const std::filesystem::path& libraryPath,
                                                       const NativePluginCapabilityPolicy& policy) {
    std::error_code error;
    const bool exists = std::filesystem::exists(libraryPath, error);
    if (error || !exists) {
        return {NativePluginLoadCode::FileNotFound,
                "No plugin file at '" + libraryPath.generic_string() + "'.", {}};
    }
    std::string openError;
    void* handle = OpenLibrary(libraryPath, openError);
    if (handle == nullptr) {
        return {NativePluginLoadCode::OpenFailed, openError, {}};
    }
    const auto entry = reinterpret_cast<NativePluginEntryFn>(
        FindSymbol(handle, kNativePluginEntrySymbol.data()));
    if (entry == nullptr) {
        CloseLibrary(handle);
        return {NativePluginLoadCode::EntryMissing,
                "The library exports no '" + std::string(kNativePluginEntrySymbol) + "'.", {}};
    }
    const NativePluginDescriptor* descriptor = nullptr;
    try {
        descriptor = entry();
    } catch (...) {
        CloseLibrary(handle);
        return {NativePluginLoadCode::InvalidDescriptor, "The plugin entry point threw an exception.", {}};
    }
    NativePluginManifest manifest;
    const NativePluginDescriptorResult shape = ValidateNativePluginDescriptor(descriptor, manifest);
    if (!shape.IsValid()) {
        CloseLibrary(handle);
        const NativePluginLoadCode code =
            shape.code == NativePluginDescriptorCode::UnsupportedAbi
                ? NativePluginLoadCode::UnsupportedProtocol
                : NativePluginLoadCode::InvalidDescriptor;
        return {code, shape.message, {}};
    }
    if (m_loaded.find(manifest.pluginId) != m_loaded.end()) {
        CloseLibrary(handle);
        return {NativePluginLoadCode::ManifestRejected,
                "The plugin '" + manifest.pluginId + "' is already loaded.", {}};
    }
    // Negotiation before validation: protocol compatibility is the cheapest disqualifier, and it
    // keeps UnsupportedProtocol reachable (validation also rejects unknown protocols, so running it
    // first would swallow every such failure into ManifestRejected).
    const NativePluginAbiNegotiationResult negotiated = NegotiateNativePluginAbi(manifest);
    if (!negotiated.IsCompatible()) {
        CloseLibrary(handle);
        return {NativePluginLoadCode::UnsupportedProtocol, negotiated.message, {}};
    }
    const NativePluginManifestValidationResult valid = ValidateNativePluginManifest(manifest, policy);
    if (!valid.IsValid()) {
        CloseLibrary(handle);
        return {NativePluginLoadCode::ManifestRejected,
                "The manifest is invalid: " + valid.diagnostics.front().message, {}};
    }
    const std::string pluginId = manifest.pluginId;
    const NativePluginRegistryResult registered = m_registry.RegisterManifest(std::move(manifest), policy);
    if (!registered.IsAccepted()) {
        CloseLibrary(handle);
        return {NativePluginLoadCode::ManifestRejected, registered.message, {}};
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
            static_cast<void>(m_registry.UnregisterManifest(pluginId));
            CloseLibrary(handle);
            return {NativePluginLoadCode::InitializeFailed,
                    threw ? "The plugin initialize function threw an exception."
                          : "The plugin initialize function returned " + std::to_string(status) + ".",
                    {}};
        }
    }
    m_loaded.emplace(pluginId, LoadedPlugin{handle, descriptor->shutdown});
    m_loadOrder.push_back(pluginId);
    return {NativePluginLoadCode::Loaded, "The plugin '" + pluginId + "' loaded.", pluginId};
}

bool NativePluginHost::Unload(const std::string_view pluginId) {
    const auto loaded = m_loaded.find(std::string(pluginId));
    if (loaded == m_loaded.end()) {
        return false;
    }
    const NativePluginRegistryResult removed = m_registry.UnregisterManifest(loaded->first);
    if (removed.code == NativePluginRegistryCode::Busy) {
        return false;
    }
    if (loaded->second.shutdown != nullptr) {
        try {
            loaded->second.shutdown();
        } catch (...) {
            // A throwing shutdown cannot stop an unload already past unregistering.
        }
    }
    CloseLibrary(loaded->second.handle);
    std::erase(m_loadOrder, loaded->first);
    m_loaded.erase(loaded);
    return true;
}

void NativePluginHost::UnloadAll() {
    const std::vector<std::string> order = m_loadOrder;
    for (auto id = order.rbegin(); id != order.rend(); ++id) {
        static_cast<void>(Unload(*id));
    }
}

bool NativePluginHost::IsLoaded(const std::string_view pluginId) const noexcept {
    return m_loaded.find(std::string(pluginId)) != m_loaded.end();
}

std::size_t NativePluginHost::LoadedCount() const noexcept {
    return m_loaded.size();
}

} // namespace UVE::Plugins
