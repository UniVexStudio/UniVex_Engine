// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/prefab_instance_component_uve.h"

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
[[nodiscard]] PrefabOverrideOperationResultUVE ApplyPrefabOverridesUVE(
    const PrefabInstanceComponentUVE& instance, IPrefabOverrideTargetUVE& target) {
    if (!IsPrefabInstanceComponentValidUVE(instance)) {
        return {PrefabOverrideOperationCodeUVE::InvalidInstance, 0U,
                "Prefab override apply rejected because the instance data is invalid."};
    }
    // Complete the read/staging phase before mutating the target. Apart from making a failed
    // read observably read-only, this keeps string/vector allocation failures out of the write
    // phase: no target mutation can precede an exception while a rollback value is being staged.
    std::vector<PrefabPropertyOverrideUVE> previousValues;
    previousValues.reserve(instance.overrides.size());
    for (const PrefabPropertyOverrideUVE& override : instance.overrides) {
        std::string previousValue;
        bool readSucceeded = false;
        try {
            readSucceeded = target.ReadPropertyUVE(override.propertyPath, previousValue);
        } catch (...) {
            return {PrefabOverrideOperationCodeUVE::ReadFailed, previousValues.size(),
                    "Prefab override apply could not read the existing target property."};
        }
        if (!readSucceeded || previousValue.empty() ||
            previousValue.size() > kMaximumPrefabOverrideValueBytesUVE ||
            previousValue.find('\0') != std::string::npos) {
            return {PrefabOverrideOperationCodeUVE::ReadFailed, previousValues.size(),
                    "Prefab override apply could not read the existing target property."};
        }
        previousValues.push_back({override.propertyPath, std::move(previousValue)});
    }

    for (std::size_t index = 0U; index < instance.overrides.size(); ++index) {
        const PrefabPropertyOverrideUVE& override = instance.overrides[index];
        bool writeSucceeded = false;
        bool writeThrew = false;
        try {
            writeSucceeded = target.WritePropertyUVE(override.propertyPath, override.serializedValue);
        } catch (...) {
            writeThrew = true;
        }
        if (writeSucceeded) {
            continue;
        }
        const std::size_t rollbackEnd = writeThrew ? index + 1U : index;
        for (std::size_t rollbackIndex = rollbackEnd; rollbackIndex > 0U; --rollbackIndex) {
            const std::size_t propertyIndex = rollbackIndex - 1U;
            const PrefabPropertyOverrideUVE& previous = previousValues[propertyIndex];
            bool rollbackSucceeded = false;
            try {
                rollbackSucceeded = target.WritePropertyUVE(previous.propertyPath, previous.serializedValue);
            } catch (...) {
                rollbackSucceeded = false;
            }
            if (!rollbackSucceeded) {
                return {PrefabOverrideOperationCodeUVE::RollbackFailed, propertyIndex,
                        "Prefab override apply failed and rollback could not restore the target."};
            }
        }
        return {PrefabOverrideOperationCodeUVE::WriteFailed, index,
                "Prefab override apply failed; target writes were rolled back."};
    }
    return {PrefabOverrideOperationCodeUVE::Applied, instance.overrides.size(),
            "Prefab overrides applied to the live target."};
}

[[nodiscard]] PrefabOverrideConflictReportUVE DetectPrefabOverrideConflictsUVE(
    const PrefabInstanceComponentUVE& instance, const std::vector<PrefabPropertyOverrideUVE>& baseline,
    const IPrefabOverrideTargetUVE& target,
    std::size_t maximumConflicts) {
    if (!IsPrefabInstanceComponentValidUVE(instance)) {
        return {PrefabOverrideOperationCodeUVE::InvalidInstance, 0U, false, {}};
    }
    const PrefabInstanceComponentUVE baselineInstance{instance.sourcePrefabGuid, baseline};
    if (!IsPrefabInstanceComponentValidUVE(baselineInstance) || baseline.size() != instance.overrides.size() ||
        !std::equal(baseline.begin(), baseline.end(), instance.overrides.begin(),
                    [](const PrefabPropertyOverrideUVE& lhs, const PrefabPropertyOverrideUVE& rhs) {
                        return lhs.propertyPath == rhs.propertyPath;
                    })) {
        return {PrefabOverrideOperationCodeUVE::InvalidBaseline, 0U, false, {}};
    }

    const std::size_t conflictLimit = std::min(maximumConflicts, kMaximumPrefabConflictsUVE);
    PrefabOverrideConflictReportUVE report;
    report.conflicts.reserve(std::min(baseline.size(), conflictLimit));
    for (const PrefabPropertyOverrideUVE& expected : baseline) {
        std::string actualValue;
        bool readSucceeded = false;
        try {
            readSucceeded = target.ReadPropertyUVE(expected.propertyPath, actualValue);
        } catch (...) {
            report.code = PrefabOverrideOperationCodeUVE::ReadFailed;
            return report;
        }
        if (!readSucceeded || actualValue.empty() ||
            actualValue.size() > kMaximumPrefabOverrideValueBytesUVE ||
            actualValue.find('\0') != std::string::npos) {
            report.code = PrefabOverrideOperationCodeUVE::ReadFailed;
            return report;
        }
        ++report.inspectedCount;
        if (actualValue == expected.serializedValue) {
            continue;
        }
        report.code = PrefabOverrideOperationCodeUVE::ConflictDetected;
        if (report.conflicts.size() < conflictLimit) {
            report.conflicts.push_back(
                {expected.propertyPath, expected.serializedValue, std::move(actualValue)});
        } else {
            report.truncated = true;
        }
    }
    return report;
}

[[nodiscard]] PrefabOverrideOperationResultUVE MergePrefabOverridesUVE(
    const PrefabInstanceComponentUVE& instance, const std::vector<PrefabPropertyOverrideUVE>& baseline,
    const std::vector<PrefabPropertyOverrideUVE>& remote,
    std::vector<PrefabPropertyOverrideUVE>& outMerged) {
    if (!IsPrefabInstanceComponentValidUVE(instance)) {
        return {PrefabOverrideOperationCodeUVE::InvalidInstance, 0U,
                "Prefab override merge rejected because the instance data is invalid."};
    }
    const PrefabInstanceComponentUVE baselineInstance{instance.sourcePrefabGuid, baseline};
    const PrefabInstanceComponentUVE remoteInstance{instance.sourcePrefabGuid, remote};
    if (!IsPrefabInstanceComponentValidUVE(baselineInstance) ||
        !IsPrefabInstanceComponentValidUVE(remoteInstance) || baseline.size() != instance.overrides.size() ||
        remote.size() != baseline.size()) {
        return {PrefabOverrideOperationCodeUVE::InvalidBaseline, 0U,
                "Prefab override merge rejected because the snapshots are invalid or have different paths."};
    }

    std::vector<PrefabPropertyOverrideUVE> merged;
    merged.reserve(instance.overrides.size());
    for (std::size_t index = 0U; index < instance.overrides.size(); ++index) {
        const PrefabPropertyOverrideUVE& local = instance.overrides[index];
        const PrefabPropertyOverrideUVE& base = baseline[index];
        const PrefabPropertyOverrideUVE& current = remote[index];
        if (local.propertyPath != base.propertyPath || base.propertyPath != current.propertyPath) {
            return {PrefabOverrideOperationCodeUVE::InvalidBaseline, index,
                    "Prefab override merge rejected because snapshot paths are not aligned."};
        }
        if (local.serializedValue != base.serializedValue &&
            current.serializedValue != base.serializedValue &&
            local.serializedValue != current.serializedValue) {
            return {PrefabOverrideOperationCodeUVE::ConflictDetected, index + 1U,
                    "Prefab override merge found divergent local and source edits."};
        }
        const std::string& mergedValue =
            local.serializedValue == base.serializedValue ? current.serializedValue : local.serializedValue;
        merged.push_back({local.propertyPath, mergedValue});
    }
    outMerged = std::move(merged);
    return {PrefabOverrideOperationCodeUVE::Applied, instance.overrides.size(),
            "Prefab overrides merged without conflicts."};
}

[[nodiscard]] PrefabOverrideOperationResultUVE CommitPrefabOverridesToSourceUVE(
    PrefabInstanceComponentUVE& instance, const std::vector<PrefabPropertyOverrideUVE>& baseline,
    IPrefabOverrideTargetUVE& sourceTarget,
    const std::optional<std::uint64_t> committedSourceRevision) {
    if (!IsPrefabInstanceComponentValidUVE(instance) ||
        (committedSourceRevision.has_value() &&
         (committedSourceRevision.value() == 0U || committedSourceRevision.value() < instance.sourceRevision))) {
        return {PrefabOverrideOperationCodeUVE::InvalidInstance, 0U,
                "Prefab source commit rejected because the instance or committed revision is invalid."};
    }
    const PrefabOverrideConflictReportUVE conflictReport =
        DetectPrefabOverrideConflictsUVE(instance, baseline, sourceTarget);
    if (!conflictReport.IsConflictFreeUVE()) {
        return {conflictReport.code, conflictReport.inspectedCount,
                "Prefab source commit rejected because the source baseline is stale or unreadable."};
    }
    const PrefabOverrideOperationResultUVE applied = ApplyPrefabOverridesUVE(instance, sourceTarget);
    if (!applied.IsAppliedUVE()) {
        return applied;
    }
    const std::size_t committedCount = instance.overrides.size();
    if (committedSourceRevision.has_value()) {
        instance.sourceRevision = committedSourceRevision.value();
        instance.instanceRevision = committedSourceRevision.value();
    }
    instance.overrides.clear();
    return {PrefabOverrideOperationCodeUVE::Applied, committedCount,
            "Prefab overrides committed to the source target."};
}

[[nodiscard]] PrefabOverrideOperationResultUVE RevertPrefabOverridesUVE(
    PrefabInstanceComponentUVE& instance, const std::vector<PrefabPropertyOverrideUVE>& baseline,
    IPrefabOverrideTargetUVE& target) {
    if (!IsPrefabInstanceComponentValidUVE(instance)) {
        return {PrefabOverrideOperationCodeUVE::InvalidInstance, 0U,
                "Prefab override revert rejected because the instance data is invalid."};
    }
    PrefabInstanceComponentUVE baselineInstance{instance.sourcePrefabGuid, baseline};
    if (!IsPrefabInstanceComponentValidUVE(baselineInstance) || baseline.size() != instance.overrides.size() ||
        !std::equal(baseline.begin(), baseline.end(), instance.overrides.begin(),
                    [](const PrefabPropertyOverrideUVE& lhs, const PrefabPropertyOverrideUVE& rhs) {
                        return lhs.propertyPath == rhs.propertyPath;
                    })) {
        return {PrefabOverrideOperationCodeUVE::InvalidBaseline, 0U,
                "Prefab override revert rejected because the baseline paths do not match the instance overrides."};
    }
    const PrefabOverrideOperationResultUVE applied = ApplyPrefabOverridesUVE(baselineInstance, target);
    if (!applied.IsAppliedUVE()) {
        return applied;
    }
    const std::size_t revertedCount = instance.overrides.size();
    instance.overrides.clear();
    return {PrefabOverrideOperationCodeUVE::Applied, revertedCount,
            "Prefab overrides reverted to the supplied baseline."};
}

[[nodiscard]] bool IsPrefabInstanceComponentValidUVE(
    const PrefabInstanceComponentUVE& component) noexcept {
    if (component.sourcePrefabGuid == Asset::kInvalidAssetGuidUVE || component.sourceRevision == 0U ||
        component.instanceRevision == 0U || component.overrides.size() > kMaximumPrefabOverridesUVE) {
        return false;
    }

    std::string_view previousPath;
    for (const PrefabPropertyOverrideUVE& override : component.overrides) {
        if (override.propertyPath.empty() || override.propertyPath.size() > kMaximumPrefabOverridePathBytesUVE ||
            override.serializedValue.empty() || override.serializedValue.size() > kMaximumPrefabOverrideValueBytesUVE ||
            override.propertyPath.find('\0') != std::string_view::npos ||
            override.serializedValue.find('\0') != std::string_view::npos ||
            (!previousPath.empty() && override.propertyPath <= previousPath)) {
            return false;
        }
        previousPath = override.propertyPath;
    }
    return true;
}

} // namespace UVE::Scene
