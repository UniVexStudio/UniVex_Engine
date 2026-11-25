// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asset_guid.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumPrefabOverrides = 256U;
inline constexpr std::size_t kMaximumPrefabOverridePathBytes = 256U;
inline constexpr std::size_t kMaximumPrefabOverrideValueBytes = 4096U;
inline constexpr std::size_t kMaximumPrefabConflicts = 64U;

/// A copied property-path/value pair persisted on a prefab instance. The value is serialized data;
/// this foundation deliberately does not interpret or apply it.
struct PrefabPropertyOverride final {
    std::string propertyPath;
    std::string serializedValue;
};

/// Records which prefab asset an entity was instantiated from — internal infrastructure
/// PrefabSystem owns, not one of the master spec's named built-in components (like
/// HierarchyComponent/WorldTransformComponent are SceneGraph-owned infrastructure).
/// Added by PrefabSystem::Instantiate() to the instantiated root entity; preserved
/// through ordinary save/load like any other component, including its bounded override records
/// (it never drives special re-instantiation behavior during a plain SceneSerializer load — only
/// an explicit Instantiate() call actually instantiates a prefab).
struct PrefabInstanceComponent final {
    Asset::AssetGuid sourcePrefabGuid;
    std::vector<PrefabPropertyOverride> overrides;
    /// Revision of the source prefab used when this instance was created or last refreshed.
    std::uint64_t sourceRevision = 1U;
    /// Revision represented by the instance's persisted authored state; legacy payloads default to 1.
    std::uint64_t instanceRevision = 1U;
};

enum class PrefabOverrideOperationCode : std::uint8_t {
    Applied = 0,
    InvalidInstance,
    InvalidBaseline,
    ConflictDetected,
    ReadFailed,
    WriteFailed,
    RollbackFailed,
};

struct PrefabOverrideOperationResult final {
    PrefabOverrideOperationCode code = PrefabOverrideOperationCode::InvalidInstance;
    std::size_t affectedCount = 0U;
    std::string message;

    [[nodiscard]] bool IsApplied() const noexcept {
        return code == PrefabOverrideOperationCode::Applied;
    }
};

/// Main-thread value boundary for applying serialized override values to one live instance. The
/// target resolves property paths for a concrete component/property implementation; it never owns
/// the prefab asset or the instance entity lifetime.
class IPrefabOverrideTarget {
public:
    virtual ~IPrefabOverrideTarget() = default;

    [[nodiscard]] virtual bool ReadProperty(std::string_view propertyPath,
                                               std::string& serializedValue) const = 0;
    [[nodiscard]] virtual bool WriteProperty(std::string_view propertyPath,
                                                std::string_view serializedValue) = 0;
};

/// Applies the instance's sorted override records with rollback if any target write fails. The
/// instance record and source prefab remain unchanged; only the caller-owned live target mutates.
[[nodiscard]] PrefabOverrideOperationResult ApplyPrefabOverrides(
    const PrefabInstanceComponent& instance, IPrefabOverrideTarget& target);

struct PrefabOverrideConflict final {
    std::string propertyPath;
    std::string expectedSerializedValue;
    std::string actualSerializedValue;
};

struct PrefabOverrideConflictReport final {
    PrefabOverrideOperationCode code = PrefabOverrideOperationCode::Applied;
    std::size_t inspectedCount = 0U;
    bool truncated = false;
    std::vector<PrefabOverrideConflict> conflicts;

    [[nodiscard]] bool IsConflictFree() const noexcept {
        return code == PrefabOverrideOperationCode::Applied;
    }
};

/// Compares the caller-supplied source-prefab baseline with the current live target without writing
/// anything. A conflict means another edit changed a property after the baseline was captured; the
/// caller must resolve or reject the conflict before calling ApplyPrefabOverrides().
[[nodiscard]] PrefabOverrideConflictReport DetectPrefabOverrideConflicts(
    const PrefabInstanceComponent& instance, const std::vector<PrefabPropertyOverride>& baseline,
    const IPrefabOverrideTarget& target,
    std::size_t maximumConflicts = kMaximumPrefabConflicts);

/// Performs a deterministic value-only three-way merge of one local instance override set against
/// a caller-supplied baseline and current source snapshot. A local change wins when the source is
/// unchanged; a source change wins when the instance is unchanged; divergent edits are reported as
/// conflicts. No source target, asset database, instance component, or live entity is mutated, and
/// the output vector is published only after every path has been validated and merged.
[[nodiscard]] PrefabOverrideOperationResult MergePrefabOverrides(
    const PrefabInstanceComponent& instance, const std::vector<PrefabPropertyOverride>& baseline,
    const std::vector<PrefabPropertyOverride>& remote,
    std::vector<PrefabPropertyOverride>& outMerged);

/// Commits the instance's overrides into a caller-owned source-prefab target only when the
/// supplied baseline still matches. The source target is mutated through ApplyPrefabOverrides;
/// the instance override records are cleared only after every source write succeeds. When supplied,
/// committedSourceRevision is copied into both revision fields only after the commit succeeds.
[[nodiscard]] PrefabOverrideOperationResult CommitPrefabOverridesToSource(
    PrefabInstanceComponent& instance, const std::vector<PrefabPropertyOverride>& baseline,
    IPrefabOverrideTarget& sourceTarget,
    const std::optional<std::uint64_t> committedSourceRevision = std::nullopt);

/// Restores a caller-supplied sorted baseline and clears the instance override records only after
/// the target is fully restored. The baseline is explicit because persistence intentionally does
/// not invent or silently fetch source-prefab state.
[[nodiscard]] PrefabOverrideOperationResult RevertPrefabOverrides(
    PrefabInstanceComponent& instance, const std::vector<PrefabPropertyOverride>& baseline,
    IPrefabOverrideTarget& target);

/// Validates the persisted source reference without resolving the prefab or reading the asset
/// database. A prefab-instance tag must identify a real source asset; nested instances preserve
/// this value and are still loaded as data rather than recursively re-instantiated.
[[nodiscard]] bool IsPrefabInstanceComponentValid(
    const PrefabInstanceComponent& component) noexcept;

} // namespace UVE::Scene

