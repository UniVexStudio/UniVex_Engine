// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "asset_guid.h"
#include "asset_load_state.h"
#include "i_asset_manager.h"

namespace UVE::Asset {

/// AssetHandle<T> is a ref-counted RAII reference to one asset tracked by an IAssetManager,
/// obtained only via IAssetManager::Load<T>(). Copying increments the manager's reference
/// count for this asset; destruction (and move-from) decrements it. The referenced
/// IAssetManager& must outlive every handle to assets it produced (the same "systems don't
/// outlive their owner" contract already implicit elsewhere in the engine, e.g. IEntityManager&
/// arguments never outliving the EntityManager that owns them).
/// Thread-safety: safe to copy/move/destroy from any thread — every operation routes through
/// IAssetManager's own thread-safe ref-count primitives.
template <typename T>
class AssetHandle final {
public:
    AssetHandle(const AssetHandle& other) noexcept : m_manager(other.m_manager), m_guid(other.m_guid) {
        m_manager->AddRef(m_guid);
    }

    AssetHandle(AssetHandle&& other) noexcept : m_manager(other.m_manager), m_guid(other.m_guid) {
        other.m_manager = nullptr;
    }

    AssetHandle& operator=(const AssetHandle& other) noexcept {
        if (this != &other) {
            ReleaseIfOwned();
            m_manager = other.m_manager;
            m_guid = other.m_guid;
            m_manager->AddRef(m_guid);
        }
        return *this;
    }

    AssetHandle& operator=(AssetHandle&& other) noexcept {
        if (this != &other) {
            ReleaseIfOwned();
            m_manager = other.m_manager;
            m_guid = other.m_guid;
            other.m_manager = nullptr;
        }
        return *this;
    }

    ~AssetHandle() { ReleaseIfOwned(); }

    /// True once the underlying load has completed successfully — TryGet() returns non-null.
    [[nodiscard]] bool IsReady() const { return m_manager->GetLoadState(m_guid) == AssetLoadState::Loaded; }

    /// True once the underlying first load has completed and failed. A failed reload of an
    /// already-loaded record preserves the last-known-good value, so this remains false in that
    /// case while GetFailureReason() exposes the copied reload diagnostic.
    [[nodiscard]] bool HasFailed() const {
        return m_manager->GetLoadState(m_guid) == AssetLoadState::Failed;
    }

    /// Returns a copied diagnostic for the most recent failed load/reload. A failed reload may
    /// provide a diagnostic while TryGet() still returns the last-known-good value; a successful
    /// replacement clears it. The copy remains valid independently of the manager record.
    [[nodiscard]] std::string GetFailureReason() const { return m_manager->GetFailureReason(m_guid); }

    /// Returns a pointer to the loaded `T`, or `nullptr` if the load hasn't finished or has
    /// failed without a previous loaded value. The pointer may be invalidated by a future
    /// HotReload-triggered successful replacement — callers
    /// that need a stable reference across frames should re-call TryGet() each time.
    [[nodiscard]] T* TryGet() const {
        return IsReady() ? static_cast<T*>(m_manager->GetRawPointer(m_guid)) : nullptr;
    }

    [[nodiscard]] AssetGuid GetGuid() const noexcept { return m_guid; }

private:
    friend class IAssetManager;

    /// Constructs a handle for a reference IAssetManager::Load<T>() has already accounted
    /// for — deliberately private (only Load<T>() may call this) so no caller can construct a
    /// handle for a reference that was never actually counted.
    AssetHandle(IAssetManager& manager, AssetGuid guid) noexcept : m_manager(&manager), m_guid(guid) {}

    void ReleaseIfOwned() {
        if (m_manager != nullptr) {
            m_manager->Release(m_guid);
        }
    }

    IAssetManager* m_manager;
    AssetGuid m_guid;
};

} // namespace UVE::Asset
