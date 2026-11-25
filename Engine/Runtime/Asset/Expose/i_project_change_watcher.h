// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "asset_guid.h"
#include "i_project_file_index.h"

namespace UVE::Asset {

class IAssetDatabase;
class IDerivedArtifactCache;

/// Physical project-content changes normalized by the portable watcher. A rename is deliberately
/// represented as a remove-plus-create pair in v1; no fragile platform-specific rename pairing is
/// inferred.
enum class ProjectFileChangeKind : std::uint8_t {
    Created,
    Modified,
    Removed,
};

/// One copied project-content change. Removed entries retain `registeredAssetGuid` from the
/// watcher's own last-successful baseline so presentation never relies on a possibly stale current
/// AssetDatabase lookup after the file has disappeared.
struct ProjectFileChange final {
    std::uint64_t sequence = 0U;
    std::filesystem::path relativePath{};
    ProjectFileChangeKind kind = ProjectFileChangeKind::Modified;
    ProjectFileEntryKind entryKind = ProjectFileEntryKind::File;
    std::optional<AssetGuid> registeredAssetGuid;
    std::size_t staleArtifactCount = 0U;
};

/// Immutable copied change-journal snapshot. `rescanRequired` remains set after journal retention
/// overflows until an editor caller successfully replaces the full ProjectFileIndex snapshot and
/// explicitly acknowledges the rescan boundary.
struct ProjectChangeSnapshot final {
    std::filesystem::path contentRoot{};
    std::vector<ProjectFileChange> changes;
    std::uint64_t latestSequence = 0U;
    std::uint64_t successfulScanGeneration = 0U;
    bool rescanRequired = false;
    std::optional<std::string> lastScanDiagnostic;
};

/// Project-content polling journal distinct from IHotReload. It observes ordinary entries only
/// beneath one configured root, never follows symlinks, and can mark matching derived import
/// artifacts stale. It never calls an importer, reloads runtime assets, changes AssetDatabase,
/// mutates a scene, or performs filesystem content writes. Thread-safe: polling and snapshot/
/// acknowledgement calls may be made concurrently; snapshots are always copied.
class IProjectChangeWatcher {
public:
    virtual ~IProjectChangeWatcher() = default;

    /// Accumulates finite, non-negative `deltaTimeSeconds` and performs at most one portable
    /// baseline scan when the configured interval has elapsed, or immediately when no baseline
    /// exists yet. Negative or non-finite deltas are ignored before accumulator, baseline, journal,
    /// derived-cache, or filesystem state mutation. Returns true only when a successful scan
    /// replaced the baseline; failed scans retain the last good baseline and report a copied
    /// diagnostic through GetSnapshot().
    [[nodiscard]] virtual bool Poll(double deltaTimeSeconds, const IAssetDatabase& assetDatabase,
                                       IDerivedArtifactCache& derivedArtifactCache) = 0;

    /// Performs one immediate scan, bypassing interval accumulation. This exists for deterministic
    /// tests and explicit editor review; it does not refresh ProjectFileIndex or enqueue imports.
    [[nodiscard]] virtual bool PollNow(const IAssetDatabase& assetDatabase,
                                          IDerivedArtifactCache& derivedArtifactCache) = 0;

    /// Returns a copied journal and diagnostic snapshot without filesystem I/O. Thread-safe.
    [[nodiscard]] virtual ProjectChangeSnapshot GetSnapshot() const = 0;

    /// Removes retained ordinary journal records with sequence <= `sequence`. It never clears an
    /// overflow boundary (`rescanRequired`); only AcknowledgeRescan() can do that after a full
    /// successful ProjectFileIndex refresh.
    virtual void AcknowledgeThrough(std::uint64_t sequence) = 0;

    /// Clears a prior journal-overflow boundary and its retained journal. Callers must invoke this
    /// only after a successful full project-index refresh; the watcher cannot itself refresh the
    /// index because it owns no ProjectFileIndex dependency.
    virtual void AcknowledgeRescan() = 0;
};

} // namespace UVE::Asset
