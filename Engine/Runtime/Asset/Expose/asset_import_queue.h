// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <memory>

#include "i_asset_database.h"
#include "i_asset_import_queue.h"
#include "i_derived_artifact_cache.h"

namespace UVE::Asset {

/// Engine-standard deterministic v1 import scheduler. It calls the already-registered generic
/// IAssetImporter implementation synchronously from Tick(), with no hidden worker jobs or
/// automatic filesystem watches. Thread-safe queue access; callers still control when Tick()
/// runs, and EngineCore invokes it once from the main update pipeline.
class AssetImportQueue final : public IAssetImportQueue {
public:
    AssetImportQueue(IAssetImporter& importer, IAssetDatabase& assetDatabase,
                        IDerivedArtifactCache& derivedArtifactCache);
    ~AssetImportQueue() override;

    AssetImportQueue(const AssetImportQueue&) = delete;
    AssetImportQueue& operator=(const AssetImportQueue&) = delete;

    [[nodiscard]] std::optional<AssetImportJobId> Enqueue(AssetImportRequest request) override;
    [[nodiscard]] bool Tick() override;
    [[nodiscard]] bool Retry(AssetImportJobId id) override;
    [[nodiscard]] std::vector<AssetImportJob> GetJobs() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Asset
