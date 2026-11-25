// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "derived_artifact_cache.h"

#include "hash.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace UVE::Asset {
namespace {

[[nodiscard]] std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    std::error_code errorCode;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode);
    return errorCode ? path.lexically_normal() : absolutePath.lexically_normal();
}

[[nodiscard]] std::string ArtifactFileName(const std::filesystem::path& destinationPath) {
    const std::string identity = NormalizePath(destinationPath).generic_string();
    // The file name is persisted on disk, so these values must never change: one-shot FNV-1a
    // over exactly the path bytes, from the same basis as the hand-rolled loop it replaced.
    const std::uint64_t hash = Utilities::HashString(identity);
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str() + ".uvimportcache";
}

[[nodiscard]] nlohmann::json FingerprintToJson(const AssetContentFingerprint& fingerprint) {
    return nlohmann::json{{"hash", fingerprint.hash}, {"byteCount", fingerprint.byteCount}};
}

[[nodiscard]] AssetContentFingerprint FingerprintFromJson(const nlohmann::json& document) {
    return AssetContentFingerprint{document.at("hash").get<std::uint64_t>(),
                                      document.at("byteCount").get<std::uint64_t>()};
}

[[nodiscard]] nlohmann::json RecordToJson(const DerivedArtifactCacheRecord& record) {
    return nlohmann::json{{"schemaVersion", record.schemaVersion},
                          {"sourcePath", NormalizePath(record.sourcePath).generic_string()},
                          {"destinationPath", NormalizePath(record.destinationPath).generic_string()},
                          {"sourceFingerprint", FingerprintToJson(record.sourceFingerprint)},
                          {"destinationFingerprint", FingerprintToJson(record.destinationFingerprint)},
                          {"settingsVersion", record.settingsVersion},
                          {"assetGuid", record.assetGuid.value},
                          {"stale", record.stale}};
}

[[nodiscard]] std::optional<DerivedArtifactCacheRecord>
RecordFromJson(const nlohmann::json& document) {
    try {
        DerivedArtifactCacheRecord record;
        record.schemaVersion = document.at("schemaVersion").get<std::uint32_t>();
        if (record.schemaVersion != kDerivedArtifactCacheSchemaVersion) {
            return std::nullopt;
        }
        record.sourcePath = NormalizePath(document.at("sourcePath").get<std::string>());
        record.destinationPath = NormalizePath(document.at("destinationPath").get<std::string>());
        record.sourceFingerprint = FingerprintFromJson(document.at("sourceFingerprint"));
        record.destinationFingerprint = FingerprintFromJson(document.at("destinationFingerprint"));
        record.settingsVersion = document.at("settingsVersion").get<std::string>();
        record.assetGuid = AssetGuid{document.at("assetGuid").get<std::uint64_t>()};
        record.stale = document.value("stale", false);
        if (record.sourcePath.empty() || record.destinationPath.empty() || record.settingsVersion.empty() ||
            record.assetGuid == kInvalidAssetGuid) {
            return std::nullopt;
        }
        return record;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] bool WriteRecordAtomically(const std::filesystem::path& artifactPath,
                                             const DerivedArtifactCacheRecord& record) {
    std::filesystem::path temporaryPath = artifactPath;
    temporaryPath += ".tmp";
    std::error_code errorCode;
    std::filesystem::remove(temporaryPath, errorCode);
    errorCode.clear();

    {
        std::ofstream file(temporaryPath, std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file << RecordToJson(record).dump(2) << '\n';
        file.flush();
        if (!file.good()) {
            file.close();
            std::filesystem::remove(temporaryPath, errorCode);
            return false;
        }
    }

    std::filesystem::rename(temporaryPath, artifactPath, errorCode);
    if (errorCode) {
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    return true;
}

} // namespace

struct DerivedArtifactCache::Impl final {
    explicit Impl(std::filesystem::path configuredRoot) : cacheRoot(NormalizePath(configuredRoot)) {}

    std::filesystem::path cacheRoot;
    mutable std::mutex mutex;
};

DerivedArtifactCache::DerivedArtifactCache(std::filesystem::path cacheRoot)
    : m_impl(std::make_unique<Impl>(std::move(cacheRoot))) {}

DerivedArtifactCache::~DerivedArtifactCache() = default;

std::optional<DerivedArtifactCacheRecord>
DerivedArtifactCache::LoadImportRecord(const std::filesystem::path& destinationPath) const {
    if (destinationPath.empty()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const std::filesystem::path artifactPath = m_impl->cacheRoot / ArtifactFileName(destinationPath);
    std::ifstream file(artifactPath);
    if (!file.is_open()) {
        return std::nullopt;
    }

    try {
        nlohmann::json document;
        file >> document;
        std::optional<DerivedArtifactCacheRecord> record = RecordFromJson(document);
        if (!record.has_value() || record->destinationPath != NormalizePath(destinationPath)) {
            return std::nullopt;
        }
        return record;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

bool DerivedArtifactCache::StoreImportRecord(const std::filesystem::path& destinationPath,
                                                    const DerivedArtifactCacheRecord& record) {
    if (destinationPath.empty() || record.schemaVersion != kDerivedArtifactCacheSchemaVersion ||
        record.sourcePath.empty() || record.destinationPath.empty() || record.settingsVersion.empty() ||
        record.assetGuid == kInvalidAssetGuid || record.stale ||
        NormalizePath(destinationPath) != NormalizePath(record.destinationPath)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::error_code errorCode;
    const std::filesystem::file_status rootStatus = std::filesystem::symlink_status(m_impl->cacheRoot, errorCode);
    if (!errorCode && std::filesystem::is_symlink(rootStatus)) {
        return false;
    }
    errorCode.clear();
    if (!std::filesystem::exists(m_impl->cacheRoot, errorCode)) {
        errorCode.clear();
        std::filesystem::create_directories(m_impl->cacheRoot, errorCode);
        if (errorCode) {
            return false;
        }
    } else if (errorCode || !std::filesystem::is_directory(m_impl->cacheRoot, errorCode) || errorCode) {
        return false;
    }

    const std::filesystem::path artifactPath = m_impl->cacheRoot / ArtifactFileName(destinationPath);
    return WriteRecordAtomically(artifactPath, record);
}

std::size_t DerivedArtifactCache::MarkStaleForSource(const std::filesystem::path& sourcePath) {
    if (sourcePath.empty()) {
        return 0U;
    }

    const std::filesystem::path normalizedSourcePath = NormalizePath(sourcePath);
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::error_code errorCode;
    const std::filesystem::file_status rootStatus = std::filesystem::symlink_status(m_impl->cacheRoot, errorCode);
    if (errorCode || std::filesystem::is_symlink(rootStatus) || !std::filesystem::is_directory(rootStatus)) {
        return 0U;
    }

    std::size_t markedCount = 0U;
    std::filesystem::directory_iterator iterator(m_impl->cacheRoot,
                                                 std::filesystem::directory_options::skip_permission_denied,
                                                 errorCode);
    if (errorCode) {
        return 0U;
    }
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        const std::filesystem::directory_entry entry = *iterator;
        const std::filesystem::file_status entryStatus = entry.symlink_status(errorCode);
        if (errorCode) {
            return markedCount;
        }
        if (std::filesystem::is_regular_file(entryStatus) && entry.path().extension() == ".uvimportcache") {
            std::ifstream file(entry.path());
            if (file.is_open()) {
                try {
                    nlohmann::json document;
                    file >> document;
                    std::optional<DerivedArtifactCacheRecord> record = RecordFromJson(document);
                    if (record.has_value() && record->sourcePath == normalizedSourcePath && !record->stale) {
                        record->stale = true;
                        if (WriteRecordAtomically(entry.path(), *record)) {
                            ++markedCount;
                        }
                    }
                } catch (const nlohmann::json::exception&) {
                    // A malformed artifact is already a cache miss; leave it untouched.
                }
            }
        }
        iterator.increment(errorCode);
        if (errorCode) {
            return markedCount;
        }
    }
    return markedCount;
}

std::filesystem::path DerivedArtifactCache::GetCacheRoot() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->cacheRoot;
}

} // namespace UVE::Asset
