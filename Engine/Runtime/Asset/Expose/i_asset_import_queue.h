// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "asset_content_fingerprint.h"
#include "asset_guid.h"
#include "i_asset_importer.h"

namespace UVE::Asset {

/// Strong identifier for one queue-owned import job. Zero is never assigned and is invalid.
struct AssetImportJobId final {
    std::uint64_t value = 0U;

    [[nodiscard]] bool operator==(const AssetImportJobId&) const noexcept = default;
};

inline constexpr AssetImportJobId kInvalidAssetImportJobId{};

/// The only lifecycle states available to a v1 import job. Terminal states stay observable until
/// callers explicitly retry a failed job or the queue itself is destroyed.
enum class AssetImportJobState {
    Queued,
    Running,
    Succeeded,
    Failed,
};

enum class AssetImportDiagnosticSeverity {
    Warning,
    Error,
};

/// Stable category for structured import diagnostics. Human-readable messages remain supplemental;
/// tests and future UI should prefer this code for deterministic branching.
enum class AssetImportDiagnosticCode {
    InvalidRequest,
    SourceFingerprintFailed,
    DestinationFingerprintFailed,
    ImporterFailed,
    CacheReadFailed,
    CacheWriteFailed,
};

/// Read-only import request data retained by a queued job. `settings` is shared immutable ownership
/// so an enqueue caller may release its original handle without invalidating a later tick.
struct AssetImportRequest final {
    std::filesystem::path sourcePath{};
    std::filesystem::path destinationPath{};
    std::shared_ptr<const AssetImportSettings> settings{};
    std::string settingsVersion{};
};

struct AssetImportDiagnostic final {
    AssetImportDiagnosticSeverity severity = AssetImportDiagnosticSeverity::Error;
    AssetImportDiagnosticCode code = AssetImportDiagnosticCode::InvalidRequest;
    std::string message{};
    std::filesystem::path sourcePath{};
    std::filesystem::path destinationPath{};
    std::uint32_t attempt = 0U;
};

/// Copied observable state of one queue-owned import job. It is safe to retain and edit after
/// GetJobs() returns; edits cannot modify queue internals or importer/cache ownership.
struct AssetImportJob final {
    AssetImportJobId id{};
    AssetImportRequest request{};
    AssetImportJobState state = AssetImportJobState::Queued;
    std::uint32_t attemptCount = 0U;
    std::optional<AssetContentFingerprint> sourceFingerprint;
    std::optional<AssetContentFingerprint> destinationFingerprint;
    std::optional<AssetGuid> resultGuid;
    bool cacheHit = false;
    std::vector<AssetImportDiagnostic> diagnostics;
};

/// Main-thread deterministic import scheduler. Enqueueing and retrying mutate queue state only;
/// exactly one oldest queued job executes synchronously per Tick() call. Retry appends a failed
/// job to the FIFO tail, preserving all jobs already waiting ahead of it. The interface deliberately
/// has no worker, cancellation, filesystem watching, or automatic reimport policy in v1.
class IAssetImportQueue {
public:
    virtual ~IAssetImportQueue() = default;

    /// Normalizes and queues a copied request without touching source, destination, cache, or
    /// AssetDatabase. Returns std::nullopt for empty paths or missing immutable settings.
    [[nodiscard]] virtual std::optional<AssetImportJobId>
    Enqueue(AssetImportRequest request) = 0;

    /// Synchronously processes at most one FIFO job. Returns true only when a queued job was
    /// transitioned through Running to Succeeded or Failed; caller-boundary exceptions are translated
    /// into terminal diagnostics, and returns false when no queued job exists.
    [[nodiscard]] virtual bool Tick() = 0;

    /// Requeues one failed job at the FIFO tail while preserving its stable id. Result data and
    /// diagnostics are cleared; attemptCount increments only when the retry later executes.
    [[nodiscard]] virtual bool Retry(AssetImportJobId id) = 0;

    /// Returns all jobs in creation order as copied snapshots, never as references to internals.
    [[nodiscard]] virtual std::vector<AssetImportJob> GetJobs() const = 0;
};

} // namespace UVE::Asset
