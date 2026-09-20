// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/asset/asset_guid_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumPrefabOverridesUVE = 256U;
inline constexpr std::size_t kMaximumPrefabOverridePathBytesUVE = 256U;
inline constexpr std::size_t kMaximumPrefabOverrideValueBytesUVE = 4096U;
inline constexpr std::size_t kMaximumPrefabConflictsUVE = 64U;

/// A copied property-path/value pair persisted on a prefab instance. The value is serialized data;
/// this foundation deliberately does not interpret or apply it.
struct PrefabPropertyOverrideUVE final {
    std::string propertyPath;
    std::string serializedValue;
};

/// Records which prefab asset an entity was instantiated from — internal infrastructure
/// PrefabSystemUVE owns, not one of the master spec's named built-in components (like
/// HierarchyComponentUVE/WorldTransformComponentUVE are SceneGraphUVE-owned infrastructure).
/// Added by PrefabSystemUVE::InstantiateUVE() to the instantiated root entity; preserved
/// through ordinary save/load like any other component, including its bounded override records
/// (it never drives special re-instantiation behavior during a plain SceneSerializerUVE load — only
/// an explicit InstantiateUVE() call actually instantiates a prefab).
struct PrefabInstanceComponentUVE final {
    Asset::AssetGuidUVE sourcePrefabGuid;
    std::vector<PrefabPropertyOverrideUVE> overrides;
    /// Revision of the source prefab used when this instance was created or last refreshed.
    std::uint64_t sourceRevision = 1U;
    /// Revision represented by the instance's persisted authored state; legacy payloads default to 1.
    std::uint64_t instanceRevision = 1U;
};

enum class PrefabOverrideOperationCodeUVE : std::uint8_t {
    Applied = 0,
    InvalidInstance,
    InvalidBaseline,
    ConflictDetected,
    ReadFailed,
    WriteFailed,
    RollbackFailed,
};

struct PrefabOverrideOperationResultUVE final {
    PrefabOverrideOperationCodeUVE code = PrefabOverrideOperationCodeUVE::InvalidInstance;
    std::size_t affectedCount = 0U;
    std::string message;

    [[nodiscard]] bool IsAppliedUVE() const noexcept {
        return code == PrefabOverrideOperationCodeUVE::Applied;
    }
};

/// Main-thread value boundary for applying serialized override values to one live instance. The
/// target resolves property paths for a concrete component/property implementation; it never owns
/// the prefab asset or the instance entity lifetime.
class IPrefabOverrideTargetUVE {
public:
    virtual ~IPrefabOverrideTargetUVE() = default;

    [[nodiscard]] virtual bool ReadPropertyUVE(std::string_view propertyPath,
                                               std::string& serializedValue) const = 0;
    [[nodiscard]] virtual bool WritePropertyUVE(std::string_view propertyPath,
                                                std::string_view serializedValue) = 0;
};

/// Applies the instance's sorted override records with rollback if any target write fails. The
/// instance record and source prefab remain unchanged; only the caller-owned live target mutates.
[[nodiscard]] PrefabOverrideOperationResultUVE ApplyPrefabOverridesUVE(
    const PrefabInstanceComponentUVE& instance, IPrefabOverrideTargetUVE& target);

struct PrefabOverrideConflictUVE final {
    std::string propertyPath;
    std::string expectedSerializedValue;
    std::string actualSerializedValue;
};

struct PrefabOverrideConflictReportUVE final {
    PrefabOverrideOperationCodeUVE code = PrefabOverrideOperationCodeUVE::Applied;
    std::size_t inspectedCount = 0U;
    bool truncated = false;
    std::vector<PrefabOverrideConflictUVE> conflicts;

    [[nodiscard]] bool IsConflictFreeUVE() const noexcept {
        return code == PrefabOverrideOperationCodeUVE::Applied;
    }
};

/// Compares the caller-supplied source-prefab baseline with the current live target without writing
/// anything. A conflict means another edit changed a property after the baseline was captured; the
/// caller must resolve or reject the conflict before calling ApplyPrefabOverridesUVE().
[[nodiscard]] PrefabOverrideConflictReportUVE DetectPrefabOverrideConflictsUVE(
    const PrefabInstanceComponentUVE& instance, const std::vector<PrefabPropertyOverrideUVE>& baseline,
    const IPrefabOverrideTargetUVE& target,
    std::size_t maximumConflicts = kMaximumPrefabConflictsUVE);

/// Performs a deterministic value-only three-way merge of one local instance override set against
/// a caller-supplied baseline and current source snapshot. A local change wins when the source is
/// unchanged; a source change wins when the instance is unchanged; divergent edits are reported as
/// conflicts. No source target, asset database, instance component, or live entity is mutated, and
/// the output vector is published only after every path has been validated and merged.
[[nodiscard]] PrefabOverrideOperationResultUVE MergePrefabOverridesUVE(
    const PrefabInstanceComponentUVE& instance, const std::vector<PrefabPropertyOverrideUVE>& baseline,
    const std::vector<PrefabPropertyOverrideUVE>& remote,
    std::vector<PrefabPropertyOverrideUVE>& outMerged);

/// Commits the instance's overrides into a caller-owned source-prefab target only when the
/// supplied baseline still matches. The source target is mutated through ApplyPrefabOverridesUVE;
/// the instance override records are cleared only after every source write succeeds. When supplied,
/// committedSourceRevision is copied into both revision fields only after the commit succeeds.
[[nodiscard]] PrefabOverrideOperationResultUVE CommitPrefabOverridesToSourceUVE(
    PrefabInstanceComponentUVE& instance, const std::vector<PrefabPropertyOverrideUVE>& baseline,
    IPrefabOverrideTargetUVE& sourceTarget,
    const std::optional<std::uint64_t> committedSourceRevision = std::nullopt);

/// Restores a caller-supplied sorted baseline and clears the instance override records only after
/// the target is fully restored. The baseline is explicit because persistence intentionally does
/// not invent or silently fetch source-prefab state.
[[nodiscard]] PrefabOverrideOperationResultUVE RevertPrefabOverridesUVE(
    PrefabInstanceComponentUVE& instance, const std::vector<PrefabPropertyOverrideUVE>& baseline,
    IPrefabOverrideTargetUVE& target);

/// Validates the persisted source reference without resolving the prefab or reading the asset
/// database. A prefab-instance tag must identify a real source asset; nested instances preserve
/// this value and are still loaded as data rather than recursively re-instantiated.
[[nodiscard]] bool IsPrefabInstanceComponentValidUVE(
    const PrefabInstanceComponentUVE& component) noexcept;

} // namespace UVE::Scene

