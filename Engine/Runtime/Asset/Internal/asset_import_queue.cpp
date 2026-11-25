// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "asset_import_queue.h"

#include <algorithm>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <utility>
#include <vector>

namespace UVE::Asset {
namespace {

[[nodiscard]] std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    std::error_code errorCode;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode);
    return errorCode ? path.lexically_normal() : absolutePath.lexically_normal();
}

[[nodiscard]] bool IsCacheRecordValid(const DerivedArtifactCacheRecord& record,
                                         const AssetImportRequest& request,
                                         const AssetContentFingerprint& sourceFingerprint,
                                         IAssetDatabase& assetDatabase,
                                         AssetContentFingerprint& outDestinationFingerprint) {
    if (record.schemaVersion != kDerivedArtifactCacheSchemaVersion || record.stale ||
        record.sourcePath != request.sourcePath || record.destinationPath != request.destinationPath ||
        record.sourceFingerprint != sourceFingerprint || record.settingsVersion != request.settingsVersion ||
        record.assetGuid == kInvalidAssetGuid) {
        return false;
    }

    const std::optional<AssetContentFingerprint> destinationFingerprint =
        ComputeAssetContentFingerprint(request.destinationPath);
    if (!destinationFingerprint.has_value() || *destinationFingerprint != record.destinationFingerprint) {
        return false;
    }

    if (NormalizePath(assetDatabase.Resolve(record.assetGuid)) != request.destinationPath) {
        return false;
    }

    // For model files (.uvmodel) imported from FBX, verify that the companion .uvhierarchy
    // derived artifact exists beside the model. If an older cache lacks it, invalidate the
    // cache record so the importer re-runs and produces the authored hierarchy automatically.
    if (request.destinationPath.extension() == ".uvmodel") {
        const std::string ext = request.sourcePath.extension().string();
        const bool isFbx = (ext == ".fbx" || ext == ".FBX");
        if (isFbx) {
            const std::filesystem::path hierarchyPath =
                request.destinationPath.parent_path() / (request.destinationPath.stem().string() + ".uvhierarchy");
            std::error_code existsError;
            if (!std::filesystem::is_regular_file(hierarchyPath, existsError)) {
                return false;
            }
        }
    }

    outDestinationFingerprint = *destinationFingerprint;
    return true;
}

} // namespace

struct AssetImportQueue::Impl final {
    Impl(IAssetImporter& inImporter, IAssetDatabase& inAssetDatabase,
            IDerivedArtifactCache& inDerivedArtifactCache)
        : importer(inImporter), assetDatabase(inAssetDatabase), derivedArtifactCache(inDerivedArtifactCache) {}

    [[nodiscard]] AssetImportJob* FindJob(const AssetImportJobId id) {
        const auto it = std::find_if(jobs.begin(), jobs.end(), [id](const AssetImportJob& job) {
            return job.id == id;
        });
        return it == jobs.end() ? nullptr : &(*it);
    }

    IAssetImporter& importer;
    IAssetDatabase& assetDatabase;
    IDerivedArtifactCache& derivedArtifactCache;
    mutable std::mutex mutex;
    std::vector<AssetImportJob> jobs;
    std::deque<AssetImportJobId> queuedIds;
    std::uint64_t nextJobId = 1U;
};

AssetImportQueue::AssetImportQueue(IAssetImporter& importer, IAssetDatabase& assetDatabase,
                                         IDerivedArtifactCache& derivedArtifactCache)
    : m_impl(std::make_unique<Impl>(importer, assetDatabase, derivedArtifactCache)) {}

AssetImportQueue::~AssetImportQueue() = default;

std::optional<AssetImportJobId> AssetImportQueue::Enqueue(AssetImportRequest request) {
    if (request.sourcePath.empty() || request.destinationPath.empty() || !request.settings) {
        return std::nullopt;
    }

    request.sourcePath = NormalizePath(request.sourcePath);
    request.destinationPath = NormalizePath(request.destinationPath);
    if (request.sourcePath.empty() || request.destinationPath.empty()) {
        return std::nullopt;
    }
    if (request.settingsVersion.empty()) {
        request.settingsVersion = request.settings->GetCacheVersion();
    }
    if (request.settingsVersion.empty()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->nextJobId == kInvalidAssetImportJobId.value) {
        return std::nullopt;
    }

    const AssetImportJobId id{m_impl->nextJobId++};
    AssetImportJob job;
    job.id = id;
    job.request = std::move(request);
    m_impl->jobs.push_back(std::move(job));
    m_impl->queuedIds.push_back(id);
    return id;
}

bool AssetImportQueue::Tick() {
    AssetImportJobId id;
    AssetImportRequest request;
    std::uint32_t attempt = 0U;
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (m_impl->queuedIds.empty()) {
            return false;
        }
        id = m_impl->queuedIds.front();
        m_impl->queuedIds.pop_front();
        AssetImportJob* const job = m_impl->FindJob(id);
        if (job == nullptr || job->state != AssetImportJobState::Queued) {
            return false;
        }
        job->state = AssetImportJobState::Running;
        ++job->attemptCount;
        attempt = job->attemptCount;
        request = job->request;
    }

    const auto completeFailure = [this, id, attempt, &request](const AssetImportDiagnosticCode code,
                                                                 std::string message) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        AssetImportJob* const job = m_impl->FindJob(id);
        if (job == nullptr) {
            return;
        }
        job->state = AssetImportJobState::Failed;
        job->cacheHit = false;
        job->resultGuid.reset();
        job->diagnostics.push_back(AssetImportDiagnostic{AssetImportDiagnosticSeverity::Error, code,
                                                             std::move(message), request.sourcePath,
                                                             request.destinationPath, attempt});
    };

    const std::optional<AssetContentFingerprint> sourceFingerprint =
        ComputeAssetContentFingerprint(request.sourcePath);
    if (!sourceFingerprint.has_value()) {
        completeFailure(AssetImportDiagnosticCode::SourceFingerprintFailed,
                        "Unable to fingerprint the queued import source.");
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (AssetImportJob* const job = m_impl->FindJob(id); job != nullptr) {
            job->sourceFingerprint = *sourceFingerprint;
        }
    }

    AssetContentFingerprint cachedDestinationFingerprint;
    std::optional<DerivedArtifactCacheRecord> cachedRecord;
    try {
        cachedRecord = m_impl->derivedArtifactCache.LoadImportRecord(request.destinationPath);
        if (cachedRecord.has_value() &&
            !IsCacheRecordValid(*cachedRecord, request, *sourceFingerprint, m_impl->assetDatabase,
                                   cachedDestinationFingerprint)) {
            cachedRecord.reset();
        }
    } catch (const std::exception& exception) {
        completeFailure(AssetImportDiagnosticCode::CacheReadFailed,
                        std::string("The derived import cache threw an exception while reading: ") +
                            exception.what());
        return true;
    } catch (...) {
        completeFailure(AssetImportDiagnosticCode::CacheReadFailed,
                        "The derived import cache threw an unknown exception while reading.");
        return true;
    }
    if (cachedRecord.has_value()) {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (AssetImportJob* const job = m_impl->FindJob(id); job != nullptr) {
            job->destinationFingerprint = cachedDestinationFingerprint;
            job->resultGuid = cachedRecord->assetGuid;
            job->cacheHit = true;
            job->state = AssetImportJobState::Succeeded;
        }
        return true;
    }

    AssetGuid assetGuid;
    try {
        assetGuid = m_impl->importer.Import(request.sourcePath, request.destinationPath,
                                               m_impl->assetDatabase, *request.settings);
    } catch (const std::exception& exception) {
        completeFailure(AssetImportDiagnosticCode::ImporterFailed,
                        std::string("The registered importer threw an exception: ") + exception.what());
        return true;
    } catch (...) {
        completeFailure(AssetImportDiagnosticCode::ImporterFailed,
                        "The registered importer threw an unknown exception.");
        return true;
    }
    if (assetGuid == kInvalidAssetGuid) {
        completeFailure(AssetImportDiagnosticCode::ImporterFailed,
                        "The registered importer failed or no importer accepted this source extension.");
        return true;
    }

    const std::optional<AssetContentFingerprint> destinationFingerprint =
        ComputeAssetContentFingerprint(request.destinationPath);
    if (!destinationFingerprint.has_value()) {
        completeFailure(AssetImportDiagnosticCode::DestinationFingerprintFailed,
                        "Importer returned a GUID but the destination file could not be fingerprinted.");
        return true;
    }

    const DerivedArtifactCacheRecord cacheRecord{kDerivedArtifactCacheSchemaVersion,
                                                     request.sourcePath,
                                                     request.destinationPath,
                                                     *sourceFingerprint,
                                                     *destinationFingerprint,
                                                     request.settingsVersion,
                                                     assetGuid};
    bool cacheStored = false;
    std::string cacheWriteDiagnostic;
    try {
        cacheStored = m_impl->derivedArtifactCache.StoreImportRecord(request.destinationPath, cacheRecord);
    } catch (const std::exception& exception) {
        cacheWriteDiagnostic =
            std::string("Import succeeded but derived cache metadata threw an exception: ") + exception.what();
    } catch (...) {
        cacheWriteDiagnostic = "Import succeeded but derived cache metadata threw an unknown exception.";
    }

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    AssetImportJob* const job = m_impl->FindJob(id);
    if (job == nullptr) {
        return true;
    }
    job->destinationFingerprint = *destinationFingerprint;
    job->resultGuid = assetGuid;
    job->cacheHit = false;
    job->state = AssetImportJobState::Succeeded;
    if (!cacheStored) {
        if (cacheWriteDiagnostic.empty()) {
            cacheWriteDiagnostic = "Import succeeded but derived cache metadata could not be written.";
        }
        job->diagnostics.push_back(AssetImportDiagnostic{AssetImportDiagnosticSeverity::Warning,
                                                             AssetImportDiagnosticCode::CacheWriteFailed,
                                                             std::move(cacheWriteDiagnostic), request.sourcePath,
                                                             request.destinationPath, attempt});
    }
    return true;
}

bool AssetImportQueue::Retry(const AssetImportJobId id) {
    if (id == kInvalidAssetImportJobId) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    AssetImportJob* const job = m_impl->FindJob(id);
    if (job == nullptr || job->state != AssetImportJobState::Failed) {
        return false;
    }
    job->state = AssetImportJobState::Queued;
    job->sourceFingerprint.reset();
    job->destinationFingerprint.reset();
    job->resultGuid.reset();
    job->cacheHit = false;
    job->diagnostics.clear();
    m_impl->queuedIds.push_back(id);
    return true;
}

std::vector<AssetImportJob> AssetImportQueue::GetJobs() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->jobs;
}

} // namespace UVE::Asset
