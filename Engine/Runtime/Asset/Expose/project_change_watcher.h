// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

#include "i_project_change_watcher.h"

namespace UVE::Asset {

/// Portable, interval-driven project-content change journal. The watcher compares complete
/// symlink-safe snapshots only when due, publishes ordered copied deltas, and marks derived cache
/// records stale for observed regular-file source changes. It owns no OS watcher handle, importer,
/// runtime reload policy, or ProjectFileIndex; native backends may later implement the same
/// interface. Thread-safe: all state is protected by implementation-private synchronization.
class ProjectChangeWatcher final : public IProjectChangeWatcher {
public:
    ProjectChangeWatcher(std::filesystem::path contentRoot, double pollIntervalSeconds,
                            std::size_t journalCapacity);
    ~ProjectChangeWatcher() override;

    ProjectChangeWatcher(const ProjectChangeWatcher&) = delete;
    ProjectChangeWatcher& operator=(const ProjectChangeWatcher&) = delete;

    /// Negative or non-finite deltas are ignored before accumulator, scan, journal, or cache mutation.
    [[nodiscard]] bool Poll(double deltaTimeSeconds, const IAssetDatabase& assetDatabase,
                                IDerivedArtifactCache& derivedArtifactCache) override;
    [[nodiscard]] bool PollNow(const IAssetDatabase& assetDatabase,
                                   IDerivedArtifactCache& derivedArtifactCache) override;
    [[nodiscard]] ProjectChangeSnapshot GetSnapshot() const override;
    void AcknowledgeThrough(std::uint64_t sequence) override;
    void AcknowledgeRescan() override;

private:
    /// Performs one complete baseline comparison while m_impl's mutex is already held. Both
    /// interval-driven and explicit polls share this routine so they cannot drift semantically.
    [[nodiscard]] bool ScanNowLocked(const IAssetDatabase& assetDatabase,
                                        IDerivedArtifactCache& derivedArtifactCache);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Asset
