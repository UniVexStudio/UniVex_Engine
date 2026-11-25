// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "asset_guid.h"

namespace UVE::Asset {

class IAssetDatabase;

/// Describes the physical kind of one project-content entry retained by a
/// ProjectFileSnapshot. Symlinks are deliberately absent: ProjectFileIndex
/// never follows or exposes them in order to preserve its configured root boundary.
enum class ProjectFileEntryKind : std::uint8_t {
    Directory,
    File,
};

/// Immutable presentation data for one ordinary directory or file physically
/// enumerated beneath the configured project-content root. relativePath is
/// normalized relative to that root; registeredAssetGuid is populated only when
/// the existing AssetDatabase contains the same normalized in-root path.
struct ProjectFileEntry final {
    std::filesystem::path relativePath;
    ProjectFileEntryKind kind = ProjectFileEntryKind::File;
    std::optional<AssetGuid> registeredAssetGuid;
};

/// A copied, deterministic project-content tree snapshot. A missing content
/// root is a valid successfully refreshed empty snapshot; inaccessible roots or
/// iterator failures retain the prior snapshot and cause Refresh() to return
/// false. refreshGeneration increases only after a successful replacement.
struct ProjectFileSnapshot final {
    std::filesystem::path contentRoot;
    std::vector<ProjectFileEntry> entries;
    bool contentRootExists = false;
    std::uint64_t refreshGeneration = 0;
};

/// Read-only project-content index used by the editor Asset Browser. Refreshes
/// occur only on explicit caller request; reads return copied cached snapshots
/// and never enumerate the filesystem. Implementations must not create,
/// import, rename, delete, write, or follow symlinks anywhere under the root.
/// Thread-safety: callers may refresh and read concurrently.
class IProjectFileIndex {
public:
    virtual ~IProjectFileIndex() = default;

    /// Re-enumerates ordinary files/directories directly under contentRoot and
    /// its ordinary subdirectories, correlating the result with a copied
    /// AssetDatabase registry snapshot. Returns true only when the cached
    /// snapshot was successfully replaced. A non-existent root is a successful
    /// empty snapshot; symlink roots, inaccessible roots, and traversal errors
    /// return false without replacing the previous successful snapshot.
    virtual bool Refresh(const IAssetDatabase& assetDatabase) = 0;

    /// Returns a copied cached snapshot without filesystem I/O. Thread-safe.
    [[nodiscard]] virtual ProjectFileSnapshot GetSnapshot() const = 0;

    /// The cached snapshot's refreshGeneration, without copying it: a caller that redraws only
    /// when the project changed asks this every frame and copies the snapshot only when it moved.
    [[nodiscard]] virtual std::uint64_t GetRefreshGeneration() const { return GetSnapshot().refreshGeneration; }
};

} // namespace UVE::Asset
