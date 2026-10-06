// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/interaction_area_3d_uve.h"

#include <algorithm>
#include <vector>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsEarlierEntityUVE(const EntityUVE& left, const EntityUVE& right) noexcept {
    if (left.index != right.index) {
        return left.index < right.index;
    }
    return left.generation < right.generation;
}

} // namespace

bool IsInteractionArea3DObjectComponentValidUVE(const InteractionArea3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F && value.collisionLayer != 0U &&
           value.maximumCandidates > 0U && value.maximumCandidates <= 4096U &&
           IsBounded3DObjectStringUVE(value.interactionTag, false);
}

bool InteractionArea3DUVE::IsArmedUVE(const InteractionArea3DComponentUVE& area) noexcept {
    return area.enabled && IsInteractionArea3DObjectComponentValidUVE(area);
}

bool InteractionArea3DUVE::AcceptsInteractorUVE(const EntityUVE areaEntity, const InteractionArea3DComponentUVE& area,
                                                const EntityUVE interactorEntity, const std::uint32_t interactorLayer,
                                                const std::uint32_t interactorMask) noexcept {
    if (interactorEntity == kInvalidEntityUVE || interactorEntity == areaEntity) {
        return false;
    }
    if (area.ignoreEntity != kInvalidEntityUVE && interactorEntity == area.ignoreEntity) {
        return false;
    }
    return (interactorLayer & area.collisionMask) != 0U && (area.collisionLayer & interactorMask) != 0U;
}

void InteractionArea3DUVE::ClearInteractorsUVE(InteractionArea3DComponentUVE& area) noexcept {
    area.interactorCount = 0U;
    area.interactorsTruncated = false;
    area.focusedByPrimaryInteractor = false;
}

const EntityUVE* InteractionArea3DUVE::GetInteractorUVE(const InteractionArea3DComponentUVE& area,
                                                        const std::size_t index) noexcept {
    if (index >= area.interactorCount) {
        return nullptr;
    }
    return &area.interactors[index];
}

bool InteractionArea3DUVE::HasInteractorUVE(const InteractionArea3DComponentUVE& area,
                                            const EntityUVE interactor) noexcept {
    for (std::uint8_t index = 0U; index < area.interactorCount; ++index) {
        if (area.interactors[index] == interactor) {
            return true;
        }
    }
    return false;
}

std::size_t InteractionArea3DUVE::ResolveCandidateCapUVE(const std::uint32_t authoredMaximumCandidates,
                                                         const std::size_t storageBound) noexcept {
    return std::min<std::size_t>(static_cast<std::size_t>(authoredMaximumCandidates), storageBound);
}

std::optional<EntityUVE> InteractionArea3DUVE::ResolvePrimaryInteractorUVE(
    const std::span<const EntityUVE> interactorCandidates) noexcept {
    std::optional<EntityUVE> best;
    for (const EntityUVE& candidate : interactorCandidates) {
        if (candidate == kInvalidEntityUVE) {
            continue;
        }
        if (!best.has_value() || IsEarlierEntityUVE(candidate, *best)) {
            best = candidate;
        }
    }
    return best;
}

std::optional<EntityUVE> InteractionArea3DUVE::ResolveFocusUVE(
    const std::span<const InteractionFocusCandidateUVE> candidates) noexcept {
    std::optional<EntityUVE> best;
    float bestDistanceSquared = 0.0F;
    for (const InteractionFocusCandidateUVE& candidate : candidates) {
        if (candidate.areaEntity == kInvalidEntityUVE) {
            continue;
        }
        if (!best.has_value() || candidate.distanceSquared < bestDistanceSquared ||
            (candidate.distanceSquared == bestDistanceSquared && IsEarlierEntityUVE(candidate.areaEntity, *best))) {
            best = candidate.areaEntity;
            bestDistanceSquared = candidate.distanceSquared;
        }
    }
    return best;
}

void InteractionArea3DUVE::CommitInteractorsUVE(InteractionArea3DComponentUVE& area, const EntityUVE* interactors,
                                                const std::size_t interactorCount) {
    const bool focused = area.focusedByPrimaryInteractor;
    ClearInteractorsUVE(area);
    area.focusedByPrimaryInteractor = focused;
    if (interactors == nullptr || interactorCount == 0U) {
        return;
    }

    std::vector<EntityUVE> unique;
    unique.reserve(interactorCount);
    for (std::size_t index = 0U; index < interactorCount; ++index) {
        const EntityUVE entity = interactors[index];
        if (entity == kInvalidEntityUVE) {
            continue;
        }
        bool seen = false;
        for (const EntityUVE existing : unique) {
            if (existing == entity) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            unique.push_back(entity);
        }
    }
    std::sort(unique.begin(), unique.end(), IsEarlierEntityUVE);

    const std::size_t cap = ResolveCandidateCapUVE(area.maximumCandidates, kMaximumInteractionAreaCandidatesUVE);
    area.interactorsTruncated = unique.size() > cap;
    const std::size_t committed = std::min(unique.size(), cap);
    for (std::size_t index = 0U; index < committed; ++index) {
        area.interactors[index] = unique[index];
    }
    area.interactorCount = static_cast<std::uint8_t>(committed);
}

std::size_t ResolveInteractionAreaCandidateCapUVE(const std::uint32_t authoredMaximumCandidates,
                                                  const std::size_t storageBound) noexcept {
    return InteractionArea3DUVE::ResolveCandidateCapUVE(authoredMaximumCandidates, storageBound);
}

std::optional<EntityUVE> ResolvePrimaryInteractorUVE(const std::span<const EntityUVE> interactorCandidates) noexcept {
    return InteractionArea3DUVE::ResolvePrimaryInteractorUVE(interactorCandidates);
}

std::optional<EntityUVE> ResolveInteractionFocusUVE(
    const std::span<const InteractionFocusCandidateUVE> candidates) noexcept {
    return InteractionArea3DUVE::ResolveFocusUVE(candidates);
}

} // namespace UVE::Scene
