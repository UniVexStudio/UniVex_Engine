// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Shared project-content-tree path helpers for the Asset module. ProjectFileIndexUVE and
// ProjectChangeWatcherUVE previously each carried their own anonymous-namespace copies of
// MakeRootRelativePathUVE / NormalizeProjectContentRootUVE / the registered-assets-by-path
// map builder (the watcher's copy even noted "Match ProjectFileIndexUVE:") - the
// 2026-09-17 audit flagged that cluster. Both owners now share this one
// implementation; behavior is unchanged from the index's original copy.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/i_project_file_index_uve.h"

namespace UVE::Asset::Detail {

/// Resolves `path` against the absolute `root`, returning the root-relative spelling, or
/// nullopt when the path is outside the root, cannot be absolutized, or lands on the root
/// itself. Shared by the content-tree traversal in ProjectFileIndexUVE and
/// ProjectChangeWatcherUVE.
[[nodiscard]] std::optional<std::filesystem::path>
MakeRootRelativePathUVE(const std::filesystem::path& path, const std::filesystem::path& root);

/// Normalizes a configured content root: `lexically_normal()` deliberately retains a trailing
/// separator on a relative directory such as `assets/`. Removes only that empty final
/// component so path iteration cannot mistake it for a root-boundary mismatch, while
/// preserving absolute filesystem roots such as `/`.
[[nodiscard]] std::filesystem::path NormalizeProjectContentRootUVE(std::filesystem::path path);

/// Builds the shared lower-case-stable relative-path -> AssetGuidUVE lookup every content-tree
/// traversal needs in order to cross-reference on-disk files with the registered asset
/// database. Entries outside absoluteContentRoot are skipped.
[[nodiscard]] std::map<std::string, AssetGuidUVE>
BuildRegisteredAssetsByRelativePathUVE(const std::filesystem::path& absoluteContentRoot,
                                       const IAssetDatabaseUVE& assetDatabase);

/// Recursively walks the verified-existing directory at absoluteContentRoot, delivering every
/// directory or regular-file entry to perEntry as (absoluteEntryPath, rootRelativePath, kind).
/// Symlinks and other entry kinds are silently crossed out (recursion is disabled there),
/// matching both former private walk loops. When perEntry returns false the walk aborts and
/// the function returns false; perEntry is expected to have recorded *why* itself (the
/// watcher's fingerprint-failure case does this out-of-band). Infrastructure failures -
/// iterator construction, per-entry status inspection, a path escaping the root boundary, or
/// iterator advancement - are reported in outDiagnostic using the concrete English messages
/// ProjectChangeWatcherUVE already published and then the walk fails. ProjectFileIndexUVE,
/// which historically failed silently, simply discards the diagnostic.
[[nodiscard]] bool WalkProjectContentTreeUVE(
    const std::filesystem::path& absoluteContentRoot,
    std::string& outDiagnostic,
    const std::function<bool(const std::filesystem::path& absoluteEntryPath,
                             const std::filesystem::path& rootRelativePath,
                             ProjectFileEntryKindUVE kind)>& perEntry);

} // namespace UVE::Asset::Detail
