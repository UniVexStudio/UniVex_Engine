// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_change_watcher.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "asset_content_fingerprint.h"
#include "i_asset_database.h"
#include "i_derived_artifact_cache.h"

#include "project_tree_helpers.h"

namespace UVE::Asset {
namespace {

struct BaselineEntry final {
    std::filesystem::path relativePath{};
    ProjectFileEntryKind kind = ProjectFileEntryKind::File;
    std::optional<AssetGuid> registeredAssetGuid;
    std::optional<AssetContentFingerprint> contentFingerprint;
    std::filesystem::file_time_type lastWriteTime{};
    std::uintmax_t fileSize = 0U;
};

[[nodiscard]] bool PhysicalIdentityMatches(const BaselineEntry& left,
                                              const BaselineEntry& right) noexcept {
    return left.kind == right.kind && left.contentFingerprint == right.contentFingerprint;
}

[[nodiscard]] bool ChangeLess(const ProjectFileChange& left,
                                 const ProjectFileChange& right) {
    if (left.relativePath.generic_string() != right.relativePath.generic_string()) {
        return left.relativePath.generic_string() < right.relativePath.generic_string();
    }
    if (left.kind != right.kind) {
        return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
    }
    return static_cast<std::uint8_t>(left.entryKind) < static_cast<std::uint8_t>(right.entryKind);
}

[[nodiscard]] std::optional<std::map<std::string, BaselineEntry>>
BuildBaseline(const std::filesystem::path& configuredContentRoot, const IAssetDatabase& assetDatabase,
                 const std::map<std::string, BaselineEntry>* const previousBaseline,
                 std::string& outDiagnostic) {
    outDiagnostic.clear();
    std::map<std::string, BaselineEntry> baseline;
    if (configuredContentRoot.empty()) {
        return baseline;
    }

    std::error_code errorCode;
    const std::filesystem::path absoluteContentRoot =
        std::filesystem::absolute(configuredContentRoot, errorCode).lexically_normal();
    if (errorCode) {
        outDiagnostic = "Unable to resolve the configured project content root: " + errorCode.message();
        return std::nullopt;
    }

    const std::filesystem::file_status rootStatus = std::filesystem::symlink_status(absoluteContentRoot, errorCode);
    if (errorCode && errorCode != std::errc::no_such_file_or_directory) {
        outDiagnostic = "Unable to inspect the project content root: " + errorCode.message();
        return std::nullopt;
    }
    if (errorCode == std::errc::no_such_file_or_directory || !std::filesystem::exists(rootStatus)) {
        return baseline;
    }
    if (std::filesystem::is_symlink(rootStatus)) {
        outDiagnostic = "The project content root is a symlink and cannot be watched.";
        return std::nullopt;
    }
    if (!std::filesystem::is_directory(rootStatus)) {
        outDiagnostic = "The project content root is not a directory.";
        return std::nullopt;
    }

    const std::map<std::string, AssetGuid> registeredAssetsByRelativePath =
        Detail::BuildRegisteredAssetsByRelativePath(absoluteContentRoot, assetDatabase);

    // The traversal itself - iterator lifecycle, symlink fencing, root-boundary checks - is
    // shared with ProjectFileIndex (Detail::WalkProjectContentTree); the fingerprint
    // failure stays this walker's sole per-entry abort, keeping its original diagnostic.
    const auto baselineOneEntry = [&baseline, &registeredAssetsByRelativePath, &outDiagnostic,
                                   previousBaseline](
                                      const std::filesystem::path& absoluteEntryPath,
                                      const std::filesystem::path& relativePath,
                                      const ProjectFileEntryKind kind) {
        BaselineEntry baselineEntry;
        baselineEntry.relativePath = relativePath;
        baselineEntry.kind = kind;
        if (kind == ProjectFileEntryKind::File) {
            std::error_code metaError;
            const auto writeTime = std::filesystem::last_write_time(absoluteEntryPath, metaError);
            const auto size = !metaError ? std::filesystem::file_size(absoluteEntryPath, metaError) : 0U;

            const std::string key = relativePath.generic_string();
            if (previousBaseline != nullptr) {
                const auto prevIt = previousBaseline->find(key);
                if (prevIt != previousBaseline->end() && prevIt->second.kind == kind) {
                    if (!metaError && prevIt->second.lastWriteTime == writeTime && prevIt->second.fileSize == size &&
                        prevIt->second.contentFingerprint.has_value()) {
                        // Fast path: file metadata (timestamp and size) is unchanged since the last baseline.
                        // Reuse the cached fingerprint without reading the file from disk!
                        baselineEntry.lastWriteTime = writeTime;
                        baselineEntry.fileSize = size;
                        baselineEntry.contentFingerprint = prevIt->second.contentFingerprint;
                        const auto registeredIt = registeredAssetsByRelativePath.find(key);
                        if (registeredIt != registeredAssetsByRelativePath.end()) {
                            baselineEntry.registeredAssetGuid = registeredIt->second;
                        } else {
                            baselineEntry.registeredAssetGuid = prevIt->second.registeredAssetGuid;
                        }
                        baseline.emplace(key, std::move(baselineEntry));
                        return true;
                    }
                }
            }

            // Fingerprint from disk when new, missing from previous baseline, or modified.
            baselineEntry.lastWriteTime = writeTime;
            baselineEntry.fileSize = size;
            baselineEntry.contentFingerprint = ComputeAssetContentFingerprint(absoluteEntryPath);
            if (!baselineEntry.contentFingerprint.has_value()) {
                // Unreadable for now (still being copied in, locked): leave it out so the next poll
                // reports it as new, instead of the whole scan failing and no change being seen at all.
                return true;
            }
            const auto registeredIt = registeredAssetsByRelativePath.find(key);
            if (registeredIt != registeredAssetsByRelativePath.end()) {
                baselineEntry.registeredAssetGuid = registeredIt->second;
            }
        }
        baseline.emplace(relativePath.generic_string(), std::move(baselineEntry));
        return true;
    };
    if (!Detail::WalkProjectContentTree(absoluteContentRoot, outDiagnostic, baselineOneEntry)) {
        return std::nullopt;
    }
    return baseline;
}

} // namespace

struct ProjectChangeWatcher::Impl final {
    Impl(std::filesystem::path configuredContentRoot, const double configuredPollIntervalSeconds,
            const std::size_t configuredJournalCapacity)
        : contentRoot(Detail::NormalizeProjectContentRoot(std::move(configuredContentRoot))),
          pollIntervalSeconds(std::max(0.0, configuredPollIntervalSeconds)),
          journalCapacity(configuredJournalCapacity) {
        snapshot.contentRoot = contentRoot;
    }

    std::filesystem::path contentRoot;
    double pollIntervalSeconds = 0.0;
    double accumulatedSeconds = 0.0;
    std::size_t journalCapacity = 0U;
    bool baselineInitialized = false;
    std::map<std::string, BaselineEntry> baseline;
    mutable std::mutex mutex;
    ProjectChangeSnapshot snapshot;
};

ProjectChangeWatcher::ProjectChangeWatcher(std::filesystem::path contentRoot,
                                                 const double pollIntervalSeconds,
                                                 const std::size_t journalCapacity)
    : m_impl(std::make_unique<Impl>(std::move(contentRoot), pollIntervalSeconds, journalCapacity)) {}

ProjectChangeWatcher::~ProjectChangeWatcher() = default;

bool ProjectChangeWatcher::ScanNowLocked(const IAssetDatabase& assetDatabase,
                                                IDerivedArtifactCache& derivedArtifactCache) {
    std::string diagnostic;
    std::optional<std::map<std::string, BaselineEntry>> refreshedBaseline =
        BuildBaseline(m_impl->contentRoot, assetDatabase,
                         m_impl->baselineInitialized ? &m_impl->baseline : nullptr, diagnostic);
    if (!refreshedBaseline.has_value()) {
        m_impl->snapshot.lastScanDiagnostic = std::move(diagnostic);
        return false;
    }

    if (!m_impl->baselineInitialized) {
        m_impl->baseline = std::move(*refreshedBaseline);
        m_impl->baselineInitialized = true;
        ++m_impl->snapshot.successfulScanGeneration;
        m_impl->snapshot.lastScanDiagnostic.reset();
        return true;
    }

    std::vector<ProjectFileChange> changes;
    for (const auto& [pathKey, previousEntry] : m_impl->baseline) {
        const auto currentIt = refreshedBaseline->find(pathKey);
        if (currentIt == refreshedBaseline->end()) {
            changes.push_back(ProjectFileChange{0U, previousEntry.relativePath, ProjectFileChangeKind::Removed,
                                                   previousEntry.kind, previousEntry.registeredAssetGuid, 0U});
            continue;
        }
        if (!PhysicalIdentityMatches(previousEntry, currentIt->second)) {
            if (previousEntry.kind != currentIt->second.kind) {
                changes.push_back(ProjectFileChange{0U, previousEntry.relativePath,
                                                       ProjectFileChangeKind::Removed, previousEntry.kind,
                                                       previousEntry.registeredAssetGuid, 0U});
                changes.push_back(ProjectFileChange{0U, currentIt->second.relativePath,
                                                       ProjectFileChangeKind::Created, currentIt->second.kind,
                                                       currentIt->second.registeredAssetGuid, 0U});
            } else {
                changes.push_back(ProjectFileChange{0U, currentIt->second.relativePath,
                                                       ProjectFileChangeKind::Modified, currentIt->second.kind,
                                                       currentIt->second.registeredAssetGuid, 0U});
            }
        }
    }
    for (const auto& [pathKey, currentEntry] : *refreshedBaseline) {
        if (!m_impl->baseline.contains(pathKey)) {
            changes.push_back(ProjectFileChange{0U, currentEntry.relativePath, ProjectFileChangeKind::Created,
                                                   currentEntry.kind, currentEntry.registeredAssetGuid, 0U});
        }
    }
    std::sort(changes.begin(), changes.end(), ChangeLess);

    for (ProjectFileChange& change : changes) {
        if (change.entryKind == ProjectFileEntryKind::File) {
            change.staleArtifactCount = derivedArtifactCache.MarkStaleForSource(
                m_impl->contentRoot / change.relativePath);
        }
        change.sequence = ++m_impl->snapshot.latestSequence;
        if (m_impl->journalCapacity == 0U) {
            m_impl->snapshot.rescanRequired = true;
            continue;
        }
        if (m_impl->snapshot.changes.size() >= m_impl->journalCapacity) {
            m_impl->snapshot.changes.erase(m_impl->snapshot.changes.begin());
            m_impl->snapshot.rescanRequired = true;
        }
        m_impl->snapshot.changes.push_back(std::move(change));
    }

    m_impl->baseline = std::move(*refreshedBaseline);
    ++m_impl->snapshot.successfulScanGeneration;
    m_impl->snapshot.lastScanDiagnostic.reset();
    return true;
}

bool ProjectChangeWatcher::Poll(const double deltaTimeSeconds, const IAssetDatabase& assetDatabase,
                                      IDerivedArtifactCache& derivedArtifactCache) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds < 0.0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->accumulatedSeconds += deltaTimeSeconds;
    if (m_impl->pollIntervalSeconds > 0.0 &&
        m_impl->accumulatedSeconds < m_impl->pollIntervalSeconds) {
        return false;
    }
    m_impl->accumulatedSeconds = 0.0;
    return ScanNowLocked(assetDatabase, derivedArtifactCache);
}

bool ProjectChangeWatcher::PollNow(const IAssetDatabase& assetDatabase,
                                         IDerivedArtifactCache& derivedArtifactCache) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->accumulatedSeconds = 0.0;
    return ScanNowLocked(assetDatabase, derivedArtifactCache);
}

ProjectChangeSnapshot ProjectChangeWatcher::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->snapshot;
}

void ProjectChangeWatcher::AcknowledgeThrough(const std::uint64_t sequence) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto firstUnacknowledged = std::remove_if(
        m_impl->snapshot.changes.begin(), m_impl->snapshot.changes.end(),
        [sequence](const ProjectFileChange& change) { return change.sequence <= sequence; });
    m_impl->snapshot.changes.erase(firstUnacknowledged, m_impl->snapshot.changes.end());
}

void ProjectChangeWatcher::AcknowledgeRescan() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->snapshot.rescanRequired = false;
    m_impl->snapshot.changes.clear();
}

} // namespace UVE::Asset
