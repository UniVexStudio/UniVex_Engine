// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "smart_object_3d.h"

#include <algorithm>
#include <cmath>

#include "disabled_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsSmartObject3DComponentValid(const SmartObject3DComponent& value) noexcept {
    return !value.interactionTag.empty() && IsBounded3DObjectString(value.interactionTag) &&
           IsFinite3DObjectVector(value.interactionOffset) && value.maxSimultaneousUsers >= 1U &&
           value.maxSimultaneousUsers <= 64U && std::isfinite(value.cooldownSeconds) &&
           value.cooldownSeconds >= 0.0F;
}

SmartObject3DComponent SanitizeSmartObject3DComponent(
    const SmartObject3DComponent& value) {
    SmartObject3DComponent out = value;
    if (out.interactionTag.empty()) {
        out.interactionTag = "Interact";
    } else if (!IsBounded3DObjectString(out.interactionTag)) {
        out.interactionTag.resize(kMaximum3DObjectStringLength);
    }
    if (!IsFinite3DObjectVector(out.interactionOffset)) {
        out.interactionOffset = Math::Vector3{0.0F, 0.0F, 1.0F};
    }
    out.maxSimultaneousUsers = std::clamp(out.maxSimultaneousUsers, 1U, 64U);
    if (!std::isfinite(out.cooldownSeconds) || out.cooldownSeconds < 0.0F) {
        out.cooldownSeconds = 0.0F;
    }
    return out;
}

bool IsSmartObject3DObjectDefinitionValid(
    const SmartObject3DObjectDefinition& value) noexcept {
    return IsSmartObject3DComponentValid(value.smartObject);
}

void ApplySmartObject3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                           const SmartObject3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, SmartObject3DObjectDefinition::defaultName);
    entityManager.AddComponent<SmartObject3DComponent>(
        entity, SanitizeSmartObject3DComponent(value.smartObject));
}

bool SmartObject3D::CanClaim(const SmartObject3DComponent& smartObject,
                                   const Entity agent) noexcept {
    if (!smartObject.enabled || agent == kInvalidEntity ||
        smartObject.remainingCooldownSeconds > 0.0F) {
        return false;
    }
    if (std::find(smartObject.activeUsers.begin(), smartObject.activeUsers.end(), agent) !=
        smartObject.activeUsers.end()) {
        return true;
    }
    return smartObject.activeUsers.size() < smartObject.maxSimultaneousUsers;
}

bool SmartObject3D::TryClaim(SmartObject3DComponent& smartObject, const Entity agent) {
    if (!CanClaim(smartObject, agent)) {
        return false;
    }
    if (std::find(smartObject.activeUsers.begin(), smartObject.activeUsers.end(), agent) ==
        smartObject.activeUsers.end()) {
        smartObject.activeUsers.push_back(agent);
    }
    return true;
}

bool SmartObject3D::Release(SmartObject3DComponent& smartObject,
                                  const Entity agent) noexcept {
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

Math::Vector3 SmartObject3D::ResolveWorldInteractionPoint(
    const SmartObject3DComponent& smartObject,
    const WorldTransformComponent& worldTransform) noexcept {
    return TransformWorldPoint(worldTransform, smartObject.interactionOffset);
}

bool IsWorldQuery3DComponentValid(const WorldQuery3DComponent& value) noexcept {
    return IsBounded3DObjectString(value.requiredTag) &&
           std::isfinite(value.searchRadiusMetres) && value.searchRadiusMetres > 0.0F &&
           std::isfinite(value.optimalDistanceMetres) && value.optimalDistanceMetres >= 0.0F &&
           value.optimalDistanceMetres <= value.searchRadiusMetres;
}

WorldQuery3DComponent SanitizeWorldQuery3DComponent(const WorldQuery3DComponent& value) {
    WorldQuery3DComponent out = value;
    if (!IsBounded3DObjectString(out.requiredTag)) {
        out.requiredTag.resize(kMaximum3DObjectStringLength);
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

bool IsWorldQuery3DObjectDefinitionValid(const WorldQuery3DObjectDefinition& value) noexcept {
    return IsWorldQuery3DComponentValid(value.query);
}

void ApplyWorldQuery3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                          const WorldQuery3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, WorldQuery3DObjectDefinition::defaultName);
    entityManager.AddComponent<WorldQuery3DComponent>(
        entity, SanitizeWorldQuery3DComponent(value.query));
}

std::size_t UpdateSmartObjectAndWorldQuery3D(IEntityManager& entityManager,
                                                const float deltaTimeSeconds) {
    const float dt =
        (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) ? deltaTimeSeconds : 0.0F;
    entityManager.ForEach<SmartObject3DComponent>(
        [&](const Entity, SmartObject3DComponent& smart) {
            std::erase_if(smart.activeUsers, [&](const Entity user) {
                return !entityManager.IsAlive(user);
            });
            if (smart.remainingCooldownSeconds > 0.0F && dt > 0.0F) {
                smart.remainingCooldownSeconds =
                    std::max(0.0F, smart.remainingCooldownSeconds - dt);
            }
        });

    std::size_t matchedQueries = 0U;
    entityManager.ForEach<WorldQuery3DComponent, WorldTransformComponent>(
        [&](const Entity querierEntity, WorldQuery3DComponent& query,
            const WorldTransformComponent& querierWorld) {
            query.bestEntity = kInvalidEntity;
            query.bestWorldPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
            query.bestScore = 0.0F;
            if (!query.enabled ||
                entityManager.HasComponent<DisabledComponent>(querierEntity) ||
                !IsWorldQuery3DComponentValid(query) ||
                !IsWorldTransformComponentValid(querierWorld)) {
                return;
            }
            entityManager.ForEach<SmartObject3DComponent, WorldTransformComponent>(
                [&](const Entity candidateEntity, const SmartObject3DComponent& smart,
                    const WorldTransformComponent& candidateWorld) {
                    if (candidateEntity == querierEntity || !smart.enabled ||
                        entityManager.HasComponent<DisabledComponent>(candidateEntity) ||
                        !IsWorldTransformComponentValid(candidateWorld)) {
                        return;
                    }
                    if (!query.requiredTag.empty() && smart.interactionTag != query.requiredTag) {
                        return;
                    }
                    if (query.preferUnclaimedSmartObjects &&
                        !SmartObject3D::CanClaim(smart, querierEntity)) {
                        return;
                    }
                    const Math::Vector3 interactPos =
                        SmartObject3D::ResolveWorldInteractionPoint(smart, candidateWorld);
                    const float dist = std::sqrt(
                        Math::LengthSquared(interactPos - querierWorld.worldPosition));
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
            if (query.bestEntity != kInvalidEntity) {
                ++matchedQueries;
            }
        });
    return matchedQueries;
}

} // namespace UVE::Scene
