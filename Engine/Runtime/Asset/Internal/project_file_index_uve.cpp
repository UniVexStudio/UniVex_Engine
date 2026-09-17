// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/project_file_index_uve.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "project_tree_helpers_uve.h"

#include "uve/asset/i_asset_database_uve.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] bool ProjectFileEntryLessUVE(const ProjectFileEntryUVE& left, const ProjectFileEntryUVE& right) {
    if (left.kind != right.kind) {
        return left.kind == ProjectFileEntryKindUVE::Directory;
    }
    return left.relativePath.generic_string() < right.relativePath.generic_string();
}

} // namespace

struct ProjectFileIndexUVE::ImplUVE {
    explicit ImplUVE(std::filesystem::path configuredContentRoot)
        : contentRoot(Detail::NormalizeProjectContentRootUVE(std::move(configuredContentRoot))) {
        snapshot.contentRoot = contentRoot;
    }

    std::filesystem::path contentRoot;
    mutable std::mutex mutex;
    ProjectFileSnapshotUVE snapshot;
};

ProjectFileIndexUVE::ProjectFileIndexUVE(std::filesystem::path contentRoot)
    : m_impl(std::make_unique<ImplUVE>(std::move(contentRoot))) {}

ProjectFileIndexUVE::~ProjectFileIndexUVE() = default;

bool ProjectFileIndexUVE::RefreshUVE(const IAssetDatabaseUVE& assetDatabase) {
    const std::filesystem::path configuredContentRoot = m_impl->contentRoot;
    ProjectFileSnapshotUVE refreshedSnapshot;
    refreshedSnapshot.contentRoot = configuredContentRoot;

    // An empty configured root never means the process working directory. It is
    // a deliberate disabled/empty browser state, identical to a missing root.
    if (configuredContentRoot.empty()) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        refreshedSnapshot.refreshGeneration = m_impl->snapshot.refreshGeneration + 1U;
        m_impl->snapshot = std::move(refreshedSnapshot);
        return true;
    }

    std::error_code errorCode;
    const std::filesystem::path absoluteContentRoot =
        std::filesystem::absolute(configuredContentRoot, errorCode).lexically_normal();
    if (errorCode) {
        return false;
    }

    const std::filesystem::file_status rootStatus = std::filesystem::symlink_status(absoluteContentRoot, errorCode);
    if (errorCode && errorCode != std::errc::no_such_file_or_directory) {
        return false;
    }
    if (errorCode == std::errc::no_such_file_or_directory || !std::filesystem::exists(rootStatus)) {
        errorCode.clear();
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        refreshedSnapshot.refreshGeneration = m_impl->snapshot.refreshGeneration + 1U;
        m_impl->snapshot = std::move(refreshedSnapshot);
        return true;
    }
    if (std::filesystem::is_symlink(rootStatus) || !std::filesystem::is_directory(rootStatus)) {
        return false;
    }
    refreshedSnapshot.contentRootExists = true;

    const std::map<std::string, AssetGuidUVE> registeredAssetsByRelativePath =
        Detail::BuildRegisteredAssetsByRelativePathUVE(absoluteContentRoot, assetDatabase);

    // The traversal itself - iterator lifecycle, symlink fencing, root-boundary checks - is
    // shared with ProjectChangeWatcherUVE (Detail::WalkProjectContentTreeUVE); the index keeps
    // its historical silent-failure contract and discards the diagnostic.
    std::string walkDiagnostic;
    const auto indexOneEntry = [&refreshedSnapshot, &registeredAssetsByRelativePath](
                                   const std::filesystem::path& /*absoluteEntryPath*/,
                                   const std::filesystem::path& relativePath,
                                   const ProjectFileEntryKindUVE kind) {
        ProjectFileEntryUVE indexedEntry;
        indexedEntry.relativePath = relativePath;
        indexedEntry.kind = kind;
        if (kind == ProjectFileEntryKindUVE::File) {
            const auto registeredIt =
                registeredAssetsByRelativePath.find(relativePath.generic_string());
            if (registeredIt != registeredAssetsByRelativePath.end()) {
                indexedEntry.registeredAssetGuid = registeredIt->second;
            }
        }
        refreshedSnapshot.entries.push_back(std::move(indexedEntry));
        return true;
    };
    if (!Detail::WalkProjectContentTreeUVE(absoluteContentRoot, walkDiagnostic, indexOneEntry)) {
        return false;
    }

    std::sort(refreshedSnapshot.entries.begin(), refreshedSnapshot.entries.end(), ProjectFileEntryLessUVE);
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        refreshedSnapshot.refreshGeneration = m_impl->snapshot.refreshGeneration + 1U;
        m_impl->snapshot = std::move(refreshedSnapshot);
    }
    return true;
}

ProjectFileSnapshotUVE ProjectFileIndexUVE::GetSnapshotUVE() const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->snapshot;
}

} // namespace UVE::Asset
