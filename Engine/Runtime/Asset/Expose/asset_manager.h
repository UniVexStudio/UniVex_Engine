// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <typeindex>

#include "i_asset_manager.h"
#include "i_event_system.h"
#include "i_thread_pool.h"

namespace UVE::Asset {

class IHotReload;

/// AssetManager is the concrete, engine-standard implementation of IAssetManager. Runs
/// every load on `threadPool`, publishes AssetLoadCompletedEvent via `eventSystem` on
/// completion (from whichever worker thread ran the load — IEventSystem::QueueEvent()
/// documents this as safe), and — if `hotReload` is non-null — tells it which GUIDs are
/// currently loaded so it knows what to poll.
/// Thread-safety: thread-safe. Every method is guarded by an internal mutex, matching
/// ConfigManager's/AssetDatabase's contract. The destructor blocks until every in-flight
/// load job finishes before tearing down internal state, so a load job's captured `this` never
/// outlives the object it points to.
class AssetManager final : public IAssetManager {
public:
    /// `hotReload` may be null (hot reload disabled) — AssetManager works identically either
    /// way, simply skipping the Track/Untrack calls when it's null.
    AssetManager(Threading::IThreadPool& threadPool, Events::IEventSystem& eventSystem,
                    IHotReload* hotReload = nullptr);
    ~AssetManager() override;

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    /// Zero disables admission limits. Counts loader-reported CPU payload capacities only;
    /// excludes transient decoding, allocator overhead, renderer copies and GPU resources.
    /// Live handles pin data. Lowering the limit never invalidates them.
    void SetResidentBudget(std::size_t bytes);
    [[nodiscard]] std::size_t GetResidentBytes() const;
    [[nodiscard]] std::size_t GetPeakResidentBytes() const;
    void CollectGarbage() override;
    void Reload(AssetGuid guid, IAssetDatabase& assetDatabase) override;
    [[nodiscard]] std::size_t GetLoadedAssetCount() const override;

protected:
    void RegisterLoaderErased(std::type_index type, AssetLoaderInfo info) override;
    void LoadErased(AssetGuid guid, std::type_index type, IAssetDatabase& assetDatabase) override;
    [[nodiscard]] AssetLoadState GetStateErased(AssetGuid guid) const override;
    [[nodiscard]] std::string GetFailureReasonErased(AssetGuid guid) const override;
    [[nodiscard]] void* TryGetErased(AssetGuid guid) override;
    void AddRefErased(AssetGuid guid) override;
    void ReleaseErased(AssetGuid guid) override;

private:
    /// Shared by LoadErased()'s and Reload()'s submitted job: runs the registered loader for
    /// `type` against `path`, updates the record (destroying any previously-loaded data on a
    /// successful reload), tracks/untracks with HotReload as appropriate, and queues
    /// AssetLoadCompletedEvent. Runs on a IThreadPool worker thread.
    void ExecuteLoad(AssetGuid guid, std::type_index type, std::filesystem::path path);
    void FailResolution(AssetGuid guid, std::string failureReason);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Asset
