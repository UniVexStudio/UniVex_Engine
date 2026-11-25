// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <memory>

#include "i_derived_artifact_cache.h"

namespace UVE::Asset {

/// Engine-standard project-local metadata cache for import-derived artifacts. Cache records are
/// JSON metadata files under `cacheRoot`; source and destination paths are never created, written,
/// renamed, or removed by this service. Thread-safe: public operations serialize cache-file access.
class DerivedArtifactCache final : public IDerivedArtifactCache {
public:
    explicit DerivedArtifactCache(std::filesystem::path cacheRoot);
    ~DerivedArtifactCache() override;

    DerivedArtifactCache(const DerivedArtifactCache&) = delete;
    DerivedArtifactCache& operator=(const DerivedArtifactCache&) = delete;

    [[nodiscard]] std::optional<DerivedArtifactCacheRecord>
    LoadImportRecord(const std::filesystem::path& destinationPath) const override;
    /// Rejects stale caller records before cache-root or artifact mutation; only fresh import results
    /// may be persisted through the generic store path.
    [[nodiscard]] bool StoreImportRecord(const std::filesystem::path& destinationPath,
                                             const DerivedArtifactCacheRecord& record) override;
    [[nodiscard]] std::size_t MarkStaleForSource(const std::filesystem::path& sourcePath) override;
    [[nodiscard]] std::filesystem::path GetCacheRoot() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Asset
