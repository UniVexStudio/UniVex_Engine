// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game_state_metadata.h"

namespace UVE::Save {

inline constexpr std::size_t kMaximumSaveMigrationReasonBytes = 256U;
inline constexpr std::size_t kMaximumSaveMigrationPayloadBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kMaximumSaveMigrationTransforms = 32U;

using SavePayloadMigrationTransform =
    std::function<bool(std::vector<std::byte>& payload, std::string& failureReason)>;

enum class SaveMigrationStatus : std::uint8_t {
    NotRequired = 0,
    Migrated = 1,
    UnsupportedSourceVersion = 2,
    UnsupportedTargetVersion = 3,
    InvalidPayload = 4,
};

struct SaveMigrationDiagnostics final {
    SaveMigrationStatus status = SaveMigrationStatus::NotRequired;
    std::uint32_t sourceSchemaVersion = kCurrentSavePayloadSchemaVersion;
    std::uint32_t targetSchemaVersion = kCurrentSavePayloadSchemaVersion;
    std::size_t appliedStepCount = 0U;
    std::string reason;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == SaveMigrationStatus::NotRequired || status == SaveMigrationStatus::Migrated;
    }
};

enum class SaveMigrationRegistrationCode : std::uint8_t {
    Accepted = 0,
    InvalidVersionRange,
    MissingTransform,
    DuplicateTransform,
    CapacityExceeded,
};

struct SaveMigrationRegistrationResult final {
    SaveMigrationRegistrationCode code = SaveMigrationRegistrationCode::Accepted;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == SaveMigrationRegistrationCode::Accepted;
    }
};

class SavePayloadMigrationRegistry final {
public:
    SavePayloadMigrationRegistry() = default;
    SavePayloadMigrationRegistry(const SavePayloadMigrationRegistry&) = delete;
    SavePayloadMigrationRegistry& operator=(const SavePayloadMigrationRegistry&) = delete;

    [[nodiscard]] SaveMigrationRegistrationResult Register(
        std::uint32_t sourceSchemaVersion, std::uint32_t targetSchemaVersion,
        SavePayloadMigrationTransform transform);

    [[nodiscard]] SaveMigrationDiagnostics Migrate(
        std::uint32_t sourceSchemaVersion, std::uint32_t targetSchemaVersion,
        std::vector<std::byte>& payload) const;

    [[nodiscard]] std::size_t GetTransformCount() const noexcept;

private:
    struct Entry final {
        std::uint32_t sourceSchemaVersion = 0U;
        std::uint32_t targetSchemaVersion = 0U;
        SavePayloadMigrationTransform transform;
    };

    std::vector<Entry> m_entries;
};

/// Bounded schema-dispatch seam for the fixed `.uvsave` payload. Current-version payloads pass
/// through unchanged; registered transforms can be composed through a deterministic shortest path
/// of at most the bounded registry capacity, with failure-atomic staging before scene deserialization.
/// Compression, encryption, cloud sync, and gameplay-domain transforms remain outside this seam.
[[nodiscard]] SaveMigrationDiagnostics MigrateSavePayload(std::uint32_t sourceSchemaVersion,
                                                               std::uint32_t targetSchemaVersion,
                                                               std::vector<std::byte>& payload);

} // namespace UVE::Save
