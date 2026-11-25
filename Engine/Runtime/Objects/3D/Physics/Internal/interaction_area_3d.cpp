// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "interaction_area_3d.h"

#include <algorithm>
#include <vector>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsEarlierEntity(const Entity& left, const Entity& right) noexcept {
    if (left.index != right.index) {
        return left.index < right.index;
    }
    return left.generation < right.generation;
}

} // namespace

bool IsInteractionArea3DObjectComponentValid(const InteractionArea3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F && value.collisionLayer != 0U &&
           value.maximumCandidates > 0U && value.maximumCandidates <= 4096U &&
           IsBounded3DObjectString(value.interactionTag, false);
}

bool InteractionArea3D::IsArmed(const InteractionArea3DComponent& area) noexcept {
    return area.enabled && IsInteractionArea3DObjectComponentValid(area);
}

bool InteractionArea3D::AcceptsInteractor(const Entity areaEntity, const InteractionArea3DComponent& area,
                                                const Entity interactorEntity, const std::uint32_t interactorLayer,
                                                const std::uint32_t interactorMask) noexcept {
    if (interactorEntity == kInvalidEntity || interactorEntity == areaEntity) {
        return false;
    }
    if (area.ignoreEntity != kInvalidEntity && interactorEntity == area.ignoreEntity) {
        return false;
    }
    return (interactorLayer & area.collisionMask) != 0U && (area.collisionLayer & interactorMask) != 0U;
}

void InteractionArea3D::ClearInteractors(InteractionArea3DComponent& area) noexcept {
    area.interactorCount = 0U;
    area.interactorsTruncated = false;
    area.focusedByPrimaryInteractor = false;
}

const Entity* InteractionArea3D::GetInteractor(const InteractionArea3DComponent& area,
                                                        const std::size_t index) noexcept {
    if (index >= area.interactorCount) {
        return nullptr;
    }
    return &area.interactors[index];
}

bool InteractionArea3D::HasInteractor(const InteractionArea3DComponent& area,
                                            const Entity interactor) noexcept {
    for (std::uint8_t index = 0U; index < area.interactorCount; ++index) {
        if (area.interactors[index] == interactor) {
            return true;
        }
    }
    return false;
}

std::size_t InteractionArea3D::ResolveCandidateCap(const std::uint32_t authoredMaximumCandidates,
                                                         const std::size_t storageBound) noexcept {
    return std::min<std::size_t>(static_cast<std::size_t>(authoredMaximumCandidates), storageBound);
}

std::optional<Entity> InteractionArea3D::ResolvePrimaryInteractor(
    const std::span<const Entity> interactorCandidates) noexcept {
    std::optional<Entity> best;
    for (const Entity& candidate : interactorCandidates) {
        if (candidate == kInvalidEntity) {
            continue;
        }
        if (!best.has_value() || IsEarlierEntity(candidate, *best)) {
            best = candidate;
        }
    }
    return best;
}

std::optional<Entity> InteractionArea3D::ResolveFocus(
    const std::span<const InteractionFocusCandidate> candidates) noexcept {
    std::optional<Entity> best;
    float bestDistanceSquared = 0.0F;
    for (const InteractionFocusCandidate& candidate : candidates) {
        if (candidate.areaEntity == kInvalidEntity) {
            continue;
        }
        if (!best.has_value() || candidate.distanceSquared < bestDistanceSquared ||
            (candidate.distanceSquared == bestDistanceSquared && IsEarlierEntity(candidate.areaEntity, *best))) {
            best = candidate.areaEntity;
            bestDistanceSquared = candidate.distanceSquared;
        }
    }
    return best;
}

void InteractionArea3D::CommitInteractors(InteractionArea3DComponent& area, const Entity* interactors,
                                                const std::size_t interactorCount) {
    const bool focused = area.focusedByPrimaryInteractor;
    ClearInteractors(area);
    area.focusedByPrimaryInteractor = focused;
    if (interactors == nullptr || interactorCount == 0U) {
        return;
    }

    std::vector<Entity> unique;
    unique.reserve(interactorCount);
    for (std::size_t index = 0U; index < interactorCount; ++index) {
        const Entity entity = interactors[index];
        if (entity == kInvalidEntity) {
            continue;
        }
        bool seen = false;
        for (const Entity existing : unique) {
            if (existing == entity) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            unique.push_back(entity);
        }
    }
    std::sort(unique.begin(), unique.end(), IsEarlierEntity);

    const std::size_t cap = ResolveCandidateCap(area.maximumCandidates, kMaximumInteractionAreaCandidates);
    area.interactorsTruncated = unique.size() > cap;
    const std::size_t committed = std::min(unique.size(), cap);
    for (std::size_t index = 0U; index < committed; ++index) {
        area.interactors[index] = unique[index];
    }
    area.interactorCount = static_cast<std::uint8_t>(committed);
}

std::size_t ResolveInteractionAreaCandidateCap(const std::uint32_t authoredMaximumCandidates,
                                                  const std::size_t storageBound) noexcept {
    return InteractionArea3D::ResolveCandidateCap(authoredMaximumCandidates, storageBound);
}

std::optional<Entity> ResolvePrimaryInteractor(const std::span<const Entity> interactorCandidates) noexcept {
    return InteractionArea3D::ResolvePrimaryInteractor(interactorCandidates);
}

std::optional<Entity> ResolveInteractionFocus(
    const std::span<const InteractionFocusCandidate> candidates) noexcept {
    return InteractionArea3D::ResolveFocus(candidates);
}

} // namespace UVE::Scene
