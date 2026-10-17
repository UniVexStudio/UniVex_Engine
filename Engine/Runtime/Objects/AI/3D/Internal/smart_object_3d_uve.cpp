// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/smart_object_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsSmartObject3DComponentValidUVE(const SmartObject3DComponentUVE& value) noexcept {
    return !value.interactionTag.empty() && IsBounded3DObjectStringUVE(value.interactionTag) &&
           IsFinite3DObjectVectorUVE(value.interactionOffset) && value.maxSimultaneousUsers >= 1U &&
           value.maxSimultaneousUsers <= 64U && std::isfinite(value.cooldownSeconds) &&
           value.cooldownSeconds >= 0.0F;
}

SmartObject3DComponentUVE SanitizeSmartObject3DComponentUVE(
    const SmartObject3DComponentUVE& value) {
    SmartObject3DComponentUVE out = value;
    if (out.interactionTag.empty()) {
        out.interactionTag = "Interact";
    } else if (!IsBounded3DObjectStringUVE(out.interactionTag)) {
        out.interactionTag.resize(kMaximum3DObjectStringLengthUVE);
    }
    if (!IsFinite3DObjectVectorUVE(out.interactionOffset)) {
        out.interactionOffset = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    }
    out.maxSimultaneousUsers = std::clamp(out.maxSimultaneousUsers, 1U, 64U);
    if (!std::isfinite(out.cooldownSeconds) || out.cooldownSeconds < 0.0F) {
        out.cooldownSeconds = 0.0F;
    }
    return out;
}

bool IsSmartObject3DObjectDefinitionValidUVE(
    const SmartObject3DObjectDefinitionUVE& value) noexcept {
    return IsSmartObject3DComponentValidUVE(value.smartObject);
}

void ApplySmartObject3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                           const SmartObject3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, SmartObject3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<SmartObject3DComponentUVE>(
        entity, SanitizeSmartObject3DComponentUVE(value.smartObject));
}

bool SmartObject3DUVE::CanClaimUVE(const SmartObject3DComponentUVE& smartObject,
                                   const EntityUVE agent) noexcept {
    if (!smartObject.enabled || agent == kInvalidEntityUVE ||
        smartObject.remainingCooldownSeconds > 0.0F) {
        return false;
    }
    if (std::find(smartObject.activeUsers.begin(), smartObject.activeUsers.end(), agent) !=
        smartObject.activeUsers.end()) {
        return true;
    }
    return smartObject.activeUsers.size() < smartObject.maxSimultaneousUsers;
}

bool SmartObject3DUVE::TryClaimUVE(SmartObject3DComponentUVE& smartObject, const EntityUVE agent) {
    if (!CanClaimUVE(smartObject, agent)) {
        return false;
    }
    if (std::find(smartObject.activeUsers.begin(), smartObject.activeUsers.end(), agent) ==
        smartObject.activeUsers.end()) {
        smartObject.activeUsers.push_back(agent);
    }
    return true;
}

bool SmartObject3DUVE::ReleaseUVE(SmartObject3DComponentUVE& smartObject,
                                  const EntityUVE agent) noexcept {
    const auto it =
        std::find(smartObject.activeUsers.begin(), smartObject.activeUsers.end(), agent);
    if (it == smartObject.activeUsers.end()) {
        return false;
    }
    smartObject.activeUsers.erase(it);
    if (smartObject.activeUsers.empty() && smartObject.cooldownSeconds > 0.0F) {
        smartObject.remainingCooldownSeconds = smartObject.cooldownSeconds;
    }
    return true;
}

Math::Vector3UVE SmartObject3DUVE::ResolveWorldInteractionPointUVE(
    const SmartObject3DComponentUVE& smartObject,
    const WorldTransformComponentUVE& worldTransform) noexcept {
    return TransformWorldPointUVE(worldTransform, smartObject.interactionOffset);
}

bool IsWorldQuery3DComponentValidUVE(const WorldQuery3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.requiredTag) &&
           std::isfinite(value.searchRadiusMetres) && value.searchRadiusMetres > 0.0F &&
           std::isfinite(value.optimalDistanceMetres) && value.optimalDistanceMetres >= 0.0F &&
           value.optimalDistanceMetres <= value.searchRadiusMetres;
}

WorldQuery3DComponentUVE SanitizeWorldQuery3DComponentUVE(const WorldQuery3DComponentUVE& value) {
    WorldQuery3DComponentUVE out = value;
    if (!IsBounded3DObjectStringUVE(out.requiredTag)) {
        out.requiredTag.resize(kMaximum3DObjectStringLengthUVE);
    }
    if (!std::isfinite(out.searchRadiusMetres) || out.searchRadiusMetres <= 0.0F) {
        out.searchRadiusMetres = 25.0F;
    }
    if (!std::isfinite(out.optimalDistanceMetres) || out.optimalDistanceMetres < 0.0F) {
        out.optimalDistanceMetres = 5.0F;
    }
    out.optimalDistanceMetres = std::min(out.optimalDistanceMetres, out.searchRadiusMetres);
    return out;
}

bool IsWorldQuery3DObjectDefinitionValidUVE(const WorldQuery3DObjectDefinitionUVE& value) noexcept {
    return IsWorldQuery3DComponentValidUVE(value.query);
}

void ApplyWorldQuery3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                          const WorldQuery3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, WorldQuery3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<WorldQuery3DComponentUVE>(
        entity, SanitizeWorldQuery3DComponentUVE(value.query));
}

std::size_t UpdateSmartObjectAndWorldQuery3DUVE(IEntityManagerUVE& entityManager,
                                                const float deltaTimeSeconds) {
    const float dt =
        (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) ? deltaTimeSeconds : 0.0F;
    entityManager.ForEachUVE<SmartObject3DComponentUVE>(
        [&](const EntityUVE, SmartObject3DComponentUVE& smart) {
            std::erase_if(smart.activeUsers, [&](const EntityUVE user) {
                return !entityManager.IsAliveUVE(user);
            });
            if (smart.remainingCooldownSeconds > 0.0F && dt > 0.0F) {
                smart.remainingCooldownSeconds =
                    std::max(0.0F, smart.remainingCooldownSeconds - dt);
            }
        });

    std::size_t matchedQueries = 0U;
    entityManager.ForEachUVE<WorldQuery3DComponentUVE, WorldTransformComponentUVE>(
        [&](const EntityUVE querierEntity, WorldQuery3DComponentUVE& query,
            const WorldTransformComponentUVE& querierWorld) {
            query.bestEntity = kInvalidEntityUVE;
            query.bestWorldPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
            query.bestScore = 0.0F;
            if (!query.enabled ||
                entityManager.HasComponentUVE<DisabledComponentUVE>(querierEntity) ||
                !IsWorldQuery3DComponentValidUVE(query) ||
                !IsWorldTransformComponentValidUVE(querierWorld)) {
                return;
            }
            entityManager.ForEachUVE<SmartObject3DComponentUVE, WorldTransformComponentUVE>(
                [&](const EntityUVE candidateEntity, const SmartObject3DComponentUVE& smart,
                    const WorldTransformComponentUVE& candidateWorld) {
                    if (candidateEntity == querierEntity || !smart.enabled ||
                        entityManager.HasComponentUVE<DisabledComponentUVE>(candidateEntity) ||
                        !IsWorldTransformComponentValidUVE(candidateWorld)) {
                        return;
                    }
                    if (!query.requiredTag.empty() && smart.interactionTag != query.requiredTag) {
                        return;
                    }
                    if (query.preferUnclaimedSmartObjects &&
                        !SmartObject3DUVE::CanClaimUVE(smart, querierEntity)) {
                        return;
                    }
                    const Math::Vector3UVE interactPos =
                        SmartObject3DUVE::ResolveWorldInteractionPointUVE(smart, candidateWorld);
                    const float dist = std::sqrt(
                        Math::LengthSquaredUVE(interactPos - querierWorld.worldPosition));
                    if (dist > query.searchRadiusMetres) {
                        return;
                    }
                    const float distDeviation = std::fabs(dist - query.optimalDistanceMetres);
                    const float score = std::clamp(
                        1.0F - (distDeviation / query.searchRadiusMetres), 0.001F, 1.0F);
                    if (score > query.bestScore) {
                        query.bestScore = score;
                        query.bestEntity = candidateEntity;
                        query.bestWorldPosition = interactPos;
                    }
                });
            if (query.bestEntity != kInvalidEntityUVE) {
                ++matchedQueries;
            }
        });
    return matchedQueries;
}

} // namespace UVE::Scene
