// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "prefab_instance_component.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Scene {

/// Applies the instance's sorted override records with rollback if any target write fails. The
/// instance record and source prefab remain unchanged; only the caller-owned live target mutates.
[[nodiscard]] PrefabOverrideOperationResult ApplyPrefabOverrides(
    const PrefabInstanceComponent& instance, IPrefabOverrideTarget& target) {
    if (!IsPrefabInstanceComponentValid(instance)) {
        return {PrefabOverrideOperationCode::InvalidInstance, 0U,
                "Prefab override apply rejected because the instance data is invalid."};
    }
    // Complete the read/staging phase before mutating the target. Apart from making a failed
    // read observably read-only, this keeps string/vector allocation failures out of the write
    // phase: no target mutation can precede an exception while a rollback value is being staged.
    std::vector<PrefabPropertyOverride> previousValues;
    previousValues.reserve(instance.overrides.size());
    for (const PrefabPropertyOverride& override : instance.overrides) {
        std::string previousValue;
        bool readSucceeded = false;
        try {
            readSucceeded = target.ReadProperty(override.propertyPath, previousValue);
        } catch (...) {
            return {PrefabOverrideOperationCode::ReadFailed, previousValues.size(),
                    "Prefab override apply could not read the existing target property."};
        }
        if (!readSucceeded || previousValue.empty() ||
            previousValue.size() > kMaximumPrefabOverrideValueBytes ||
            previousValue.contains('\0')) {
            return {PrefabOverrideOperationCode::ReadFailed, previousValues.size(),
                    "Prefab override apply could not read the existing target property."};
        }
        previousValues.push_back({override.propertyPath, std::move(previousValue)});
    }

    for (std::size_t index = 0U; index < instance.overrides.size(); ++index) {
        const PrefabPropertyOverride& override = instance.overrides[index];
        bool writeSucceeded = false;
        bool writeThrew = false;
        try {
            writeSucceeded = target.WriteProperty(override.propertyPath, override.serializedValue);
        } catch (...) {
            writeThrew = true;
        }
        if (writeSucceeded) {
            continue;
        }
        const std::size_t rollbackEnd = writeThrew ? index + 1U : index;
        for (std::size_t rollbackIndex = rollbackEnd; rollbackIndex > 0U; --rollbackIndex) {
            const std::size_t propertyIndex = rollbackIndex - 1U;
            const PrefabPropertyOverride& previous = previousValues[propertyIndex];
            bool rollbackSucceeded = false;
            try {
                rollbackSucceeded = target.WriteProperty(previous.propertyPath, previous.serializedValue);
            } catch (...) {
                rollbackSucceeded = false;
            }
            if (!rollbackSucceeded) {
                return {PrefabOverrideOperationCode::RollbackFailed, propertyIndex,
                        "Prefab override apply failed and rollback could not restore the target."};
            }
        }
        return {PrefabOverrideOperationCode::WriteFailed, index,
                "Prefab override apply failed; target writes were rolled back."};
    }
    return {PrefabOverrideOperationCode::Applied, instance.overrides.size(),
            "Prefab overrides applied to the live target."};
}

[[nodiscard]] PrefabOverrideConflictReport DetectPrefabOverrideConflicts(
    const PrefabInstanceComponent& instance, const std::vector<PrefabPropertyOverride>& baseline,
    const IPrefabOverrideTarget& target,
    std::size_t maximumConflicts) {
    if (!IsPrefabInstanceComponentValid(instance)) {
        return {PrefabOverrideOperationCode::InvalidInstance, 0U, false, {}};
    }
    const PrefabInstanceComponent baselineInstance{instance.sourcePrefabGuid, baseline};
    if (!IsPrefabInstanceComponentValid(baselineInstance) || baseline.size() != instance.overrides.size() ||
        !std::equal(baseline.begin(), baseline.end(), instance.overrides.begin(),
                    [](const PrefabPropertyOverride& lhs, const PrefabPropertyOverride& rhs) {
                        return lhs.propertyPath == rhs.propertyPath;
                    })) {
        return {PrefabOverrideOperationCode::InvalidBaseline, 0U, false, {}};
    }

    const std::size_t conflictLimit = std::min(maximumConflicts, kMaximumPrefabConflicts);
    PrefabOverrideConflictReport report;
    report.conflicts.reserve(std::min(baseline.size(), conflictLimit));
    for (const PrefabPropertyOverride& expected : baseline) {
        std::string actualValue;
        bool readSucceeded = false;
        try {
            readSucceeded = target.ReadProperty(expected.propertyPath, actualValue);
        } catch (...) {
            report.code = PrefabOverrideOperationCode::ReadFailed;
            return report;
        }
        if (!readSucceeded || actualValue.empty() ||
            actualValue.size() > kMaximumPrefabOverrideValueBytes ||
            actualValue.contains('\0')) {
            report.code = PrefabOverrideOperationCode::ReadFailed;
            return report;
        }
        ++report.inspectedCount;
        if (actualValue == expected.serializedValue) {
            continue;
        }
        report.code = PrefabOverrideOperationCode::ConflictDetected;
        if (report.conflicts.size() < conflictLimit) {
            report.conflicts.push_back(
                {expected.propertyPath, expected.serializedValue, std::move(actualValue)});
        } else {
            report.truncated = true;
        }
    }
    return report;
}

[[nodiscard]] PrefabOverrideOperationResult MergePrefabOverrides(
    const PrefabInstanceComponent& instance, const std::vector<PrefabPropertyOverride>& baseline,
    const std::vector<PrefabPropertyOverride>& remote,
    std::vector<PrefabPropertyOverride>& outMerged) {
    if (!IsPrefabInstanceComponentValid(instance)) {
        return {PrefabOverrideOperationCode::InvalidInstance, 0U,
                "Prefab override merge rejected because the instance data is invalid."};
    }
    const PrefabInstanceComponent baselineInstance{instance.sourcePrefabGuid, baseline};
    const PrefabInstanceComponent remoteInstance{instance.sourcePrefabGuid, remote};
    if (!IsPrefabInstanceComponentValid(baselineInstance) ||
        !IsPrefabInstanceComponentValid(remoteInstance) || baseline.size() != instance.overrides.size() ||
        remote.size() != baseline.size()) {
        return {PrefabOverrideOperationCode::InvalidBaseline, 0U,
                "Prefab override merge rejected because the snapshots are invalid or have different paths."};
    }

    std::vector<PrefabPropertyOverride> merged;
    merged.reserve(instance.overrides.size());
    for (std::size_t index = 0U; index < instance.overrides.size(); ++index) {
        const PrefabPropertyOverride& local = instance.overrides[index];
        const PrefabPropertyOverride& base = baseline[index];
        const PrefabPropertyOverride& current = remote[index];
        if (local.propertyPath != base.propertyPath || base.propertyPath != current.propertyPath) {
            return {PrefabOverrideOperationCode::InvalidBaseline, index,
                    "Prefab override merge rejected because snapshot paths are not aligned."};
        }
        if (local.serializedValue != base.serializedValue &&
            current.serializedValue != base.serializedValue &&
            local.serializedValue != current.serializedValue) {
            return {PrefabOverrideOperationCode::ConflictDetected, index + 1U,
                    "Prefab override merge found divergent local and source edits."};
        }
        const std::string& mergedValue =
            local.serializedValue == base.serializedValue ? current.serializedValue : local.serializedValue;
        merged.push_back({local.propertyPath, mergedValue});
    }
    outMerged = std::move(merged);
    return {PrefabOverrideOperationCode::Applied, instance.overrides.size(),
            "Prefab overrides merged without conflicts."};
}

[[nodiscard]] PrefabOverrideOperationResult CommitPrefabOverridesToSource(
    PrefabInstanceComponent& instance, const std::vector<PrefabPropertyOverride>& baseline,
    IPrefabOverrideTarget& sourceTarget,
    const std::optional<std::uint64_t> committedSourceRevision) {
    if (!IsPrefabInstanceComponentValid(instance) ||
        (committedSourceRevision.has_value() &&
         (committedSourceRevision.value() == 0U || committedSourceRevision.value() < instance.sourceRevision))) {
        return {PrefabOverrideOperationCode::InvalidInstance, 0U,
                "Prefab source commit rejected because the instance or committed revision is invalid."};
    }
    const PrefabOverrideConflictReport conflictReport =
        DetectPrefabOverrideConflicts(instance, baseline, sourceTarget);
    if (!conflictReport.IsConflictFree()) {
        return {conflictReport.code, conflictReport.inspectedCount,
                "Prefab source commit rejected because the source baseline is stale or unreadable."};
    }
    const PrefabOverrideOperationResult applied = ApplyPrefabOverrides(instance, sourceTarget);
    if (!applied.IsApplied()) {
        return applied;
    }
    const std::size_t committedCount = instance.overrides.size();
    if (committedSourceRevision.has_value()) {
        instance.sourceRevision = committedSourceRevision.value();
        instance.instanceRevision = committedSourceRevision.value();
    }
    instance.overrides.clear();
    return {PrefabOverrideOperationCode::Applied, committedCount,
            "Prefab overrides committed to the source target."};
}

[[nodiscard]] PrefabOverrideOperationResult RevertPrefabOverrides(
    PrefabInstanceComponent& instance, const std::vector<PrefabPropertyOverride>& baseline,
    IPrefabOverrideTarget& target) {
    if (!IsPrefabInstanceComponentValid(instance)) {
        return {PrefabOverrideOperationCode::InvalidInstance, 0U,
                "Prefab override revert rejected because the instance data is invalid."};
    }
    PrefabInstanceComponent baselineInstance{instance.sourcePrefabGuid, baseline};
    if (!IsPrefabInstanceComponentValid(baselineInstance) || baseline.size() != instance.overrides.size() ||
        !std::equal(baseline.begin(), baseline.end(), instance.overrides.begin(),
                    [](const PrefabPropertyOverride& lhs, const PrefabPropertyOverride& rhs) {
                        return lhs.propertyPath == rhs.propertyPath;
                    })) {
        return {PrefabOverrideOperationCode::InvalidBaseline, 0U,
                "Prefab override revert rejected because the baseline paths do not match the instance overrides."};
    }
    const PrefabOverrideOperationResult applied = ApplyPrefabOverrides(baselineInstance, target);
    if (!applied.IsApplied()) {
        return applied;
    }
    const std::size_t revertedCount = instance.overrides.size();
    instance.overrides.clear();
    return {PrefabOverrideOperationCode::Applied, revertedCount,
            "Prefab overrides reverted to the supplied baseline."};
}

[[nodiscard]] bool IsPrefabInstanceComponentValid(
    const PrefabInstanceComponent& component) noexcept {
    if (component.sourcePrefabGuid == Asset::kInvalidAssetGuid || component.sourceRevision == 0U ||
        component.instanceRevision == 0U || component.overrides.size() > kMaximumPrefabOverrides) {
        return false;
    }

    std::string_view previousPath;
    for (const PrefabPropertyOverride& override : component.overrides) {
        if (override.propertyPath.empty() || override.propertyPath.size() > kMaximumPrefabOverridePathBytes ||
            override.serializedValue.empty() || override.serializedValue.size() > kMaximumPrefabOverrideValueBytes ||
            override.propertyPath.contains('\0') ||
            override.serializedValue.contains('\0') ||
            (!previousPath.empty() && override.propertyPath <= previousPath)) {
            return false;
        }
        previousPath = override.propertyPath;
    }
    return true;
}

} // namespace UVE::Scene
