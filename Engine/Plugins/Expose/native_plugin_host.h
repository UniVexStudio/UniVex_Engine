// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "plugin_manifest_validation.h"
#include "plugin_registry.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace UVE::Plugins {

// ---- The plugin side: protocol 1 ---------------------------------------------------
//
// A native plugin is a shared library (`.so`, `.dll`, `.dylib`) that exports ONE C symbol,
// UveNativePluginEntry, returning a pointer to a static NativePluginDescriptor. The host
// copies every string out of the descriptor before the plugin's initialize runs, so the plugin
// owns that memory for exactly as long as the library stays loaded and the host never holds a
// pointer into it afterwards.
//
// Rules a plugin must follow: the descriptor's abiVersion is kNativePluginProtocolVersion,
// every string is NUL-terminated inside the host's scan bound, capabilityCount matches its array,
// and initialize/shutdown never throw (the host treats an exception as failure, but unwinding
// through a foreign library is already undefined behaviour - do not rely on the catch).

inline constexpr std::string_view kNativePluginEntrySymbol = "UveNativePluginEntry";

using NativePluginInitializeFn = int (*)();
using NativePluginShutdownFn = void (*)();

/// The entry contract, plain data only: no std::string, no vector, nothing whose layout depends
/// on the standard library a plugin was built against.
struct NativePluginDescriptor final {
    std::uint32_t abiVersion = 0U;
    const char* pluginId = nullptr;
    const char* displayName = nullptr;
    std::uint16_t versionMajor = 0U;
    std::uint16_t versionMinor = 0U;
    std::uint16_t versionPatch = 0U;
    std::uint32_t requiredEngineProtocol = 0U;
    const char* const* capabilityIds = nullptr;
    std::uint32_t capabilityCount = 0U;
    NativePluginInitializeFn initialize = nullptr;
    NativePluginShutdownFn shutdown = nullptr;
};

using NativePluginEntryFn = const NativePluginDescriptor* (*)();

enum class NativePluginDescriptorCode : std::uint8_t {
    Valid = 0,
    NullDescriptor,
    UnsupportedAbi,
    InvalidIdentifier,
    InvalidDisplayName,
    TooManyCapabilities,
    InvalidCapabilityId,
};

struct NativePluginDescriptorResult final {
    NativePluginDescriptorCode code = NativePluginDescriptorCode::NullDescriptor;
    std::string message;

    [[nodiscard]] bool IsValid() const noexcept {
        return code == NativePluginDescriptorCode::Valid;
    }
};

/// Reads a foreign descriptor defensively - every pointer checked, every string NUL-bounded -
/// and copies it into `outManifest` on success. Pure: no loading, no registry, no threads.
/// Policy (name lengths, allowed capabilities) is NOT judged here; ValidateNativePluginManifest
/// remains the arbiter after this passes. `outManifest` is reset on failure.
[[nodiscard]] NativePluginDescriptorResult ValidateNativePluginDescriptor(
    const NativePluginDescriptor* descriptor, NativePluginManifest& outManifest);

// ---- The host side -----------------------------------------------------------------
//
// NativePluginHost binds to ONE registry for life and owns every OS library handle it opens:
// load validates the descriptor, negotiates the ABI, validates the manifest, registers, then runs initialize; unload
// unregisters, runs shutdown and closes the handle. A scope opened through the registry blocks an
// unload (Busy), so engine subsystems borrowing a plugin cannot have it yanked out from under them.
// Destruction unloads whatever is still loaded, best effort, in reverse load order.
//
// Like the rest of the plugin module this is caller-synchronised: one thread at a time.

enum class NativePluginLoadCode : std::uint8_t {
    Loaded = 0,
    FileNotFound,
    OpenFailed,
    EntryMissing,
    InvalidDescriptor,
    UnsupportedProtocol,
    ManifestRejected,
    InitializeFailed,
};

struct NativePluginLoadResult final {
    NativePluginLoadCode code = NativePluginLoadCode::FileNotFound;
    std::string message;
    std::string pluginId;

    [[nodiscard]] bool IsLoaded() const noexcept {
        return code == NativePluginLoadCode::Loaded;
    }
};

class NativePluginHost final {
public:
    explicit NativePluginHost(NativePluginRegistry& registry) noexcept;
    ~NativePluginHost();

    NativePluginHost(const NativePluginHost&) = delete;
    NativePluginHost& operator=(const NativePluginHost&) = delete;

    /// Loads `libraryPath` end to end: file check, OS open, entry lookup, descriptor read,
    /// ABI negotiation, manifest validation (under `policy`), registration, initialize. Any step
    /// failing rolls the earlier ones back - a failed load leaves no handle, no registration and
    /// no half-initialised plugin behind.
    [[nodiscard]] NativePluginLoadResult Load(const std::filesystem::path& libraryPath,
                                                   const NativePluginCapabilityPolicy& policy = {});

    /// Unregisters, shuts down and closes `pluginId`. False when it was never loaded, or when a
    /// registry scope is still open over it (nothing is touched then).
    [[nodiscard]] bool Unload(std::string_view pluginId);

    /// Unloads everything still loaded, in reverse load order. Entries blocked by an open scope
    /// stay loaded; destruction calls this once, best effort.
    void UnloadAll();

    [[nodiscard]] bool IsLoaded(std::string_view pluginId) const noexcept;
    [[nodiscard]] std::size_t LoadedCount() const noexcept;

private:
    struct LoadedPlugin final {
        void* handle = nullptr;
        NativePluginShutdownFn shutdown = nullptr;
    };

    NativePluginRegistry& m_registry;
    std::unordered_map<std::string, LoadedPlugin> m_loaded;
    std::vector<std::string> m_loadOrder;
};

} // namespace UVE::Plugins
