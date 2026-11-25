// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "save_payload_migration.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace UVE::Save {

namespace {

[[nodiscard]] std::string BoundedReason(const char* const reason) {
    std::string bounded(reason);
    if (bounded.size() > kMaximumSaveMigrationReasonBytes) {
        bounded.resize(kMaximumSaveMigrationReasonBytes);
    }
    return bounded;
}

} // namespace

SaveMigrationRegistrationResult SavePayloadMigrationRegistry::Register(
    const std::uint32_t sourceSchemaVersion, const std::uint32_t targetSchemaVersion,
    SavePayloadMigrationTransform transform) {
    if (targetSchemaVersion == 0U || sourceSchemaVersion == targetSchemaVersion) {
        return {SaveMigrationRegistrationCode::InvalidVersionRange,
                "Save migration source and target versions must differ and target must be non-zero."};
    }
    if (!transform) {
        return {SaveMigrationRegistrationCode::MissingTransform,
                "Save migration registration requires a callable transform."};
    }
    const auto duplicate = std::find_if(m_entries.cbegin(), m_entries.cend(),
                                        [sourceSchemaVersion, targetSchemaVersion](const Entry& entry) {
                                            return entry.sourceSchemaVersion == sourceSchemaVersion &&
                                                   entry.targetSchemaVersion == targetSchemaVersion;
                                        });
    if (duplicate != m_entries.cend()) {
        return {SaveMigrationRegistrationCode::DuplicateTransform,
                "A save migration transform for this source/target pair is already registered."};
    }
    if (m_entries.size() >= kMaximumSaveMigrationTransforms) {
        return {SaveMigrationRegistrationCode::CapacityExceeded,
                "The bounded save migration registry has reached its maximum capacity."};
    }
    m_entries.push_back(Entry{sourceSchemaVersion, targetSchemaVersion, std::move(transform)});
    return {SaveMigrationRegistrationCode::Accepted, "Save migration transform registered."};
}

SaveMigrationDiagnostics SavePayloadMigrationRegistry::Migrate(
    const std::uint32_t sourceSchemaVersion, const std::uint32_t targetSchemaVersion,
    std::vector<std::byte>& payload) const {
    SaveMigrationDiagnostics diagnostics;
    diagnostics.sourceSchemaVersion = sourceSchemaVersion;
    diagnostics.targetSchemaVersion = targetSchemaVersion;

    if (sourceSchemaVersion == targetSchemaVersion &&
        sourceSchemaVersion == kCurrentSavePayloadSchemaVersion) {
        return MigrateSavePayload(sourceSchemaVersion, targetSchemaVersion, payload);
    }
    if (sourceSchemaVersion == targetSchemaVersion) {
        diagnostics.status = SaveMigrationStatus::UnsupportedSourceVersion;
        diagnostics.reason = BoundedReason("source save payload schema version is unsupported");
        return diagnostics;
    }

    const std::size_t invalidIndex = std::numeric_limits<std::size_t>::max();
    std::vector<std::uint32_t> discoveredVersions{sourceSchemaVersion};
    std::vector<std::size_t> predecessorVersion{invalidIndex};
    std::vector<std::size_t> predecessorEntry{invalidIndex};
    std::size_t queueIndex = 0U;
    std::size_t targetVersionIndex = invalidIndex;
    while (queueIndex < discoveredVersions.size() && targetVersionIndex == invalidIndex) {
        const std::uint32_t currentVersion = discoveredVersions[queueIndex];
        for (std::size_t entryIndex = 0U; entryIndex < m_entries.size(); ++entryIndex) {
            const Entry& entry = m_entries[entryIndex];
            if (entry.sourceSchemaVersion != currentVersion ||
                std::find(discoveredVersions.cbegin(), discoveredVersions.cend(), entry.targetSchemaVersion) !=
                    discoveredVersions.cend()) {
                continue;
            }
            discoveredVersions.push_back(entry.targetSchemaVersion);
            predecessorVersion.push_back(queueIndex);
            predecessorEntry.push_back(entryIndex);
            if (entry.targetSchemaVersion == targetSchemaVersion) {
                targetVersionIndex = discoveredVersions.size() - 1U;
                break;
            }
        }
        ++queueIndex;
    }

    if (targetVersionIndex == invalidIndex) {
        diagnostics.status = targetSchemaVersion == kCurrentSavePayloadSchemaVersion
                                 ? SaveMigrationStatus::UnsupportedSourceVersion
                                 : SaveMigrationStatus::UnsupportedTargetVersion;
        diagnostics.reason = BoundedReason(targetSchemaVersion == kCurrentSavePayloadSchemaVersion
                                                  ? "no migration path reaches the current save schema version"
                                                  : "no migration path reaches the requested save schema version");
        return diagnostics;
    }
    if (payload.empty() || payload.size() > kMaximumSaveMigrationPayloadBytes) {
        diagnostics.status = SaveMigrationStatus::InvalidPayload;
        diagnostics.reason = BoundedReason(payload.empty() ? "save payload is empty"
                                                               : "save payload exceeds migration cap");
        return diagnostics;
    }

    std::vector<std::size_t> path;
    for (std::size_t versionIndex = targetVersionIndex;
         predecessorEntry[versionIndex] != invalidIndex;
         versionIndex = predecessorVersion[versionIndex]) {
        path.push_back(predecessorEntry[versionIndex]);
    }
    std::reverse(path.begin(), path.end());

    std::vector<std::byte> migratedPayload = payload;
    std::string failureReason;
    for (const std::size_t entryIndex : path) {
        failureReason.clear();
        try {
            if (!m_entries[entryIndex].transform(migratedPayload, failureReason)) {
                diagnostics.status = SaveMigrationStatus::InvalidPayload;
                diagnostics.reason = failureReason.empty()
                                         ? BoundedReason("save migration transform rejected payload")
                                         : std::move(failureReason);
                if (diagnostics.reason.size() > kMaximumSaveMigrationReasonBytes) {
                    diagnostics.reason.resize(kMaximumSaveMigrationReasonBytes);
                }
                return diagnostics;
            }
        } catch (...) {
            diagnostics.status = SaveMigrationStatus::InvalidPayload;
            diagnostics.reason = BoundedReason("save migration transform raised an exception");
            return diagnostics;
        }
        if (migratedPayload.empty() || migratedPayload.size() > kMaximumSaveMigrationPayloadBytes) {
            diagnostics.status = SaveMigrationStatus::InvalidPayload;
            diagnostics.reason = BoundedReason(migratedPayload.empty()
                                                       ? "save migration transform produced an empty payload"
                                                       : "save migration transform exceeded migration cap");
            return diagnostics;
        }
        ++diagnostics.appliedStepCount;
    }
    payload.swap(migratedPayload);
    diagnostics.status = SaveMigrationStatus::Migrated;
    diagnostics.reason = BoundedReason("save payload migrated successfully");
    return diagnostics;
}

std::size_t SavePayloadMigrationRegistry::GetTransformCount() const noexcept {
    return m_entries.size();
}

SaveMigrationDiagnostics MigrateSavePayload(const std::uint32_t sourceSchemaVersion,
                                                   const std::uint32_t targetSchemaVersion,
                                                   std::vector<std::byte>& payload) {
    SaveMigrationDiagnostics diagnostics;
    diagnostics.sourceSchemaVersion = sourceSchemaVersion;
    diagnostics.targetSchemaVersion = targetSchemaVersion;

    if (targetSchemaVersion != kCurrentSavePayloadSchemaVersion) {
        diagnostics.status = SaveMigrationStatus::UnsupportedTargetVersion;
        diagnostics.reason = BoundedReason("target save payload schema version is unsupported");
        return diagnostics;
    }
    if (sourceSchemaVersion != kCurrentSavePayloadSchemaVersion) {
        diagnostics.status = SaveMigrationStatus::UnsupportedSourceVersion;
        diagnostics.reason = BoundedReason("source save payload schema version is unsupported");
        return diagnostics;
    }
    if (payload.empty() || payload.size() > kMaximumSaveMigrationPayloadBytes) {
        diagnostics.status = SaveMigrationStatus::InvalidPayload;
        diagnostics.reason = BoundedReason(payload.empty() ? "save payload is empty"
                                                               : "save payload exceeds migration cap");
        return diagnostics;
    }

    diagnostics.status = SaveMigrationStatus::NotRequired;
    return diagnostics;
}

} // namespace UVE::Save
