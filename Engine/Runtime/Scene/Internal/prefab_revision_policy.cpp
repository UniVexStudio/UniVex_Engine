// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prefab_revision_policy.h"
namespace UVE::Scene {
PrefabRevisionStatus EvaluatePrefabRevision(const std::uint64_t sourceRevision,
                                                  const std::uint64_t instanceRevision) noexcept {
    if (sourceRevision == 0U || instanceRevision == 0U) {
        return PrefabRevisionStatus::Invalid;
    }
    return sourceRevision == instanceRevision ? PrefabRevisionStatus::Current
                                               : PrefabRevisionStatus::Stale;
}

bool RefreshPrefabInstanceRevision(PrefabInstanceComponent& instance,
                                      const std::uint64_t observedSourceRevision) noexcept {
    if (!IsPrefabInstanceComponentValid(instance) || observedSourceRevision == 0U ||
        observedSourceRevision < instance.sourceRevision || !instance.overrides.empty()) {
        return false;
    }
    instance.sourceRevision = observedSourceRevision;
    instance.instanceRevision = observedSourceRevision;
    return true;
}

PrefabRevisionRefreshDecision EvaluatePrefabRevisionRefreshDecision(
    const std::uint64_t observedSourceRevision, const std::uint64_t instanceRevision,
    const bool hasLocalOverrides) noexcept {
    if (observedSourceRevision == 0U || instanceRevision == 0U ||
        observedSourceRevision < instanceRevision) {
        return PrefabRevisionRefreshDecision::Invalid;
    }
    if (observedSourceRevision == instanceRevision) {
        return PrefabRevisionRefreshDecision::NoOp;
    }
    return hasLocalOverrides ? PrefabRevisionRefreshDecision::MergeRequired
                             : PrefabRevisionRefreshDecision::Refresh;
}
} // namespace UVE::Scene
