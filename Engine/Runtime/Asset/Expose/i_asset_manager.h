// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <utility>

#include "asset_guid.h"
#include "asset_load_state.h"
#include "i_asset_database.h"

namespace UVE::Asset {

template <typename T>
class AssetHandle;

/// Type-erased loader registration record: how AssetManager actually loads/destroys a value
/// of some registered type `T`, without IAssetManager's header needing to be templated on `T`
/// itself. Internal plumbing between RegisterLoader<T>()/Load<T>() and the protected
/// erased primitives below — ordinary callers never construct one directly.
struct AssetLoaderInfo {
    /// Allocates a new `T`, runs the registered loader against a resolved file path, and returns
    /// the result as `void*` with the bool out-parameter reflecting success. On failure the
    /// allocated object is destroyed before returning and the returned pointer is null.
    std::function<void*(const std::filesystem::path&, bool&)> load;
    std::function<std::size_t(const void*)> residentBytes;
    /// Destroys a `void*` previously returned by `load`, equivalent to `delete static_cast<T*>(ptr)`.
    std::function<void(void*)> destroy;
};

/// IAssetManager is the engine's async asset loader: register a loader function once per C++
/// type, then Load<T>(guid, assetDatabase) any number of times to get a ref-counted
/// AssetHandle<T>. The underlying load runs once on a Threading::IThreadPool worker
/// (repeat Load() calls for an already-loading/loaded/failed GUID just add a reference and
/// reuse the same record); CollectGarbage() unloads assets whose reference count has dropped
/// to zero. Mirrors IEventSystem's/IEntityManager's shape: public non-virtual member
/// templates route through protected pure-virtual type-erased primitives, since a template
/// method cannot itself be virtual.
/// Thread-safety: thread-safe — every method here (and the ref-count operations AssetHandle
/// drives through the public non-virtual wrappers below) may be called concurrently from any
/// thread, including from inside a load job running on an IThreadPool worker.
/// CollectGarbage() is intended to be called once per frame from the main/engine thread (see
/// EngineCore::Update(), alongside SceneGraph::Update()) purely by engine-loop
/// convention, not because calling it elsewhere would be unsafe.
class IAssetManager {
public:
    virtual ~IAssetManager() = default;

    /// Registers the loader function used by every future Load<T>() call: reads the resolved
    /// file path and fills `outValue`, returning true on success or false on failure (a
    /// missing/corrupt file, say) — must never throw. Registering a second loader for the same
    /// `T` replaces the first. The optional residentBytes callback counts all owned payload
    /// capacities, including nested containers; finite-budget managers require it. Payloads must
    /// remain immutable while resident: admission cannot track arbitrary caller-side mutations.
    template <typename T>
    void RegisterLoader(std::function<bool(const std::filesystem::path&, T&)> loadFunc,
                           std::function<std::size_t(const T&)> residentBytes = {}) {
        AssetLoaderInfo info;
        info.load = [loadFunc](const std::filesystem::path& path, bool& outSuccess) -> void* {
            auto value = std::make_unique<T>();
            outSuccess = loadFunc(path, *value);
            return outSuccess ? value.release() : nullptr;
        };
        info.destroy = [](void* pointer) { delete static_cast<T*>(pointer); };
        if (residentBytes) info.residentBytes = [residentBytes](const void* value) {
            return residentBytes(*static_cast<const T*>(value));
        };
        RegisterLoaderErased(std::type_index(typeid(T)), std::move(info));
    }

    /// Resolves `guid` via `assetDatabase` and begins (or reuses an already in-flight/completed)
    /// an asynchronous load of it as type `T`, returning a ref-counted handle immediately — this
    /// call never blocks; poll AssetHandle::IsReady()/TryGet() on the returned handle.
    /// `T` should be registered via RegisterLoader<T>(); if it is missing, the returned handle
    /// enters Failed immediately with a diagnostic and no worker job or asset-database resolution.
    template <typename T>
    [[nodiscard]] AssetHandle<T> Load(AssetGuid guid, IAssetDatabase& assetDatabase) {
        LoadErased(guid, std::type_index(typeid(T)), assetDatabase);
        return AssetHandle<T>(*this, guid);
    }

    /// Unloads (runs the registered destroy function for) every currently tracked asset whose
    /// reference count has reached zero. A record whose state is still `Loading` is never
    /// collected even at refcount zero — the in-flight load job implicitly holds it alive until
    /// it finishes.
    virtual void CollectGarbage() = 0;

    /// Re-runs the previously-registered loader for `guid` in place, replacing its currently
    /// loaded data on success — used by HotReload when it detects the on-disk asset file has
    /// changed. A failed reload is failure-atomic: an existing loaded value remains the
    /// last-known-good data and the copied failure diagnostic is retained while the handle remains
    /// ready; a first-ever failed load remains Failed with no value. Logs a warning and no-ops if
    /// `guid` isn't currently tracked, or if it's still `Loading`. No type parameter needed: the
    /// record already remembers which registered loader produced it.
    virtual void Reload(AssetGuid guid, IAssetDatabase& assetDatabase) = 0;

    /// Number of asset records currently tracked with state `Loaded` (debugging/query only).
    [[nodiscard]] virtual std::size_t GetLoadedAssetCount() const = 0;

    /// Returns a copied, thread-safe diagnostic for the most recent failed load/reload of `guid`.
    /// A failed reload may return a non-empty diagnostic while the record remains Loaded with its
    /// last-known-good value; a successful replacement clears it. The returned value is detached
    /// from AssetManager-owned data so callers may retain it without extending asset lifetime or
    /// holding the manager mutex.
    [[nodiscard]] std::string GetFailureReason(AssetGuid guid) const {
        return GetFailureReasonErased(guid);
    }

    /// Public non-virtual wrappers around the protected erased primitives below, so
    /// AssetHandle<T> — a separate class template, not a member of this class hierarchy — can
    /// reach them (mirrors IEntityManager::GetComponentPointer()'s exact same reasoning).
    [[nodiscard]] AssetLoadState GetLoadState(AssetGuid guid) const { return GetStateErased(guid); }
    [[nodiscard]] void* GetRawPointer(AssetGuid guid) { return TryGetErased(guid); }
    void AddRef(AssetGuid guid) { AddRefErased(guid); }
    void Release(AssetGuid guid) { ReleaseErased(guid); }

protected:
    virtual void RegisterLoaderErased(std::type_index type, AssetLoaderInfo info) = 0;
    virtual void LoadErased(AssetGuid guid, std::type_index type, IAssetDatabase& assetDatabase) = 0;
    [[nodiscard]] virtual AssetLoadState GetStateErased(AssetGuid guid) const = 0;
    [[nodiscard]] virtual std::string GetFailureReasonErased(AssetGuid guid) const = 0;
    [[nodiscard]] virtual void* TryGetErased(AssetGuid guid) = 0;
    virtual void AddRefErased(AssetGuid guid) = 0;
    virtual void ReleaseErased(AssetGuid guid) = 0;
};

} // namespace UVE::Asset
