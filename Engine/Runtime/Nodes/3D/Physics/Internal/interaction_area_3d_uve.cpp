// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/interaction_area_3d_uve.h"

namespace UVE::Scene {

bool IsInteractionArea3DNodeComponentValidUVE(const InteractionArea3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F && value.collisionLayer != 0U &&
           value.maximumCandidates > 0U && value.maximumCandidates <= 4096U &&
           IsBounded3DNodeStringUVE(value.interactionTag, false);
}

std::size_t ResolveInteractionAreaCandidateCapUVE(
    const std::uint32_t authoredMaximumCandidates, const std::size_t storageBound) noexcept {
    return std::min<std::size_t>(static_cast<std::size_t>(authoredMaximumCandidates), storageBound);
}

std::optional<EntityUVE> ResolvePrimaryInteractorUVE(
    const std::span<const EntityUVE> interactorCandidates) noexcept {
    std::optional<EntityUVE> best;
    for (const EntityUVE& candidate : interactorCandidates) {
        if (candidate == kInvalidEntityUVE) {
            continue;
        }
        if (!best.has_value() || candidate.index < best->index ||
            (candidate.index == best->index && candidate.generation < best->generation)) {
            best = candidate;
        }
    }
    return best;
}

std::optional<EntityUVE> ResolveInteractionFocusUVE(
    const std::span<const InteractionFocusCandidateUVE> candidates) noexcept {
    std::optional<EntityUVE> best;
    float bestDistanceSquared = 0.0F;
    for (const InteractionFocusCandidateUVE& candidate : candidates) {
        if (candidate.areaEntity == kInvalidEntityUVE) {
            continue;
        }
        if (!best.has_value() || candidate.distanceSquared < bestDistanceSquared ||
            (candidate.distanceSquared == bestDistanceSquared &&
             (candidate.areaEntity.index < best->index ||
              (candidate.areaEntity.index == best->index &&
               candidate.areaEntity.generation < best->generation)))) {
            best = candidate.areaEntity;
            bestDistanceSquared = candidate.distanceSquared;
        }
    }
    return best;
}

} // namespace UVE::Scene
