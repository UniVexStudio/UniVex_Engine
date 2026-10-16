// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/audio_anim_nav_input_2d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] Math::Vector2UVE GetPos2DUVE(const IEntityManagerUVE& entityManager,
                                           const EntityUVE entity) noexcept {
    if (entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
        const auto& p = entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition;
        return Math::Vector2UVE{p.x, p.y};
    }
    return Math::Vector2UVE{0.0F, 0.0F};
}

} // namespace

void ApplyAudioListener2DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                             const EntityUVE entity,
                                             const AudioListener2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<AudioListener2DComponentUVE>(entity, value.listener);
}

void ApplyAudioStreamPlayer2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const AudioStreamPlayer2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<AudioStreamPlayer2DComponentUVE>(entity, value.player);
}

void ApplyBone2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const Bone2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Bone2DComponentUVE>(entity, value.bone);
}

void ApplySkeleton2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const Skeleton2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Skeleton2DComponentUVE>(entity, value.skeleton);
}

void ApplyNavigationRegion2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const NavigationRegion2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<NavigationRegion2DComponentUVE>(entity, value.region);
}

void ApplyNavigationAgent2DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                               const EntityUVE entity,
                                               const NavigationAgent2DObjectDefinitionUVE& value) {
    if (!entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
        entityManager.AddComponentUVE<TransformComponentUVE>(entity, TransformComponentUVE{});
    }
    entityManager.AddComponentUVE<NavigationAgent2DComponentUVE>(entity, value.agent);
}

void ApplyNavigationObstacle2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const NavigationObstacle2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<NavigationObstacle2DComponentUVE>(entity, value.obstacle);
}

void ApplyNavigationLink2DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                              const EntityUVE entity,
                                              const NavigationLink2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<NavigationLink2DComponentUVE>(entity, value.link);
}

void ApplyGPUParticles2DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const GPUParticles2DObjectDefinitionUVE& value) {
    Particles2DComponentUVE p = value.particles;
    p.gpuAccelerated = true;
    entityManager.AddComponentUVE<Particles2DComponentUVE>(entity, p);
}

void ApplyCPUParticles2DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const CPUParticles2DObjectDefinitionUVE& value) {
    Particles2DComponentUVE p = value.particles;
    p.gpuAccelerated = false;
    entityManager.AddComponentUVE<Particles2DComponentUVE>(entity, p);
}

void ApplyTouchScreenButtonObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const TouchScreenButtonObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<TouchScreenButtonComponentUVE>(entity, value.button);
}

std::size_t UpdateAudioAnimNavInput2DObjectsUVE(IEntityManagerUVE& entityManager,
                                                const float deltaTimeSeconds) {
    const float dt =
        (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) ? deltaTimeSeconds : 0.0F;
    std::size_t updatedCount = 0U;

    Math::Vector2UVE listenerPos{0.0F, 0.0F};
    entityManager.ForEachUVE<AudioListener2DComponentUVE>(
        [&](const EntityUVE entity, const AudioListener2DComponentUVE& listener) {
            if (!entityManager.HasComponentUVE<DisabledComponentUVE>(entity) && listener.current) {
                listenerPos = GetPos2DUVE(entityManager, entity);
            }
        });

    entityManager.ForEachUVE<AudioStreamPlayer2DComponentUVE>(
        [&](const EntityUVE entity, AudioStreamPlayer2DComponentUVE& player) {
            if (!player.playing || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                player.maxDistancePixels <= 0.0F) {
                player.effectiveGain = 0.0F;
                return;
            }
            const Math::Vector2UVE d = GetPos2DUVE(entityManager, entity) - listenerPos;
            const float dist = std::sqrt(d.x * d.x + d.y * d.y);
            const float atten = std::clamp(1.0F - (dist / player.maxDistancePixels), 0.0F, 1.0F);
            player.effectiveGain = std::max(0.0F, player.volumeLinear) * atten;
            ++updatedCount;
        });

    std::vector<std::pair<Math::Vector2UVE, float>> obstacles2D;
    entityManager.ForEachUVE<NavigationObstacle2DComponentUVE>(
        [&](const EntityUVE entity, const NavigationObstacle2DComponentUVE& obs) {
            if (obs.enabled && !entityManager.HasComponentUVE<DisabledComponentUVE>(entity) &&
                obs.radiusPixels > 0.0F) {
                obstacles2D.emplace_back(GetPos2DUVE(entityManager, entity), obs.radiusPixels);
            }
        });

    entityManager.ForEachUVE<NavigationAgent2DComponentUVE>(
        [&](const EntityUVE entity, NavigationAgent2DComponentUVE& agent) {
            if (!agent.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity)) {
                agent.desiredVelocity = Math::Vector2UVE{0.0F, 0.0F};
                return;
            }
            const Math::Vector2UVE pos = GetPos2DUVE(entityManager, entity);
            const Math::Vector2UVE toTarget = agent.targetPosition - pos;
            const float dist = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y);
            if (dist <= 2.0F) {
                agent.targetReached = true;
                agent.desiredVelocity = Math::Vector2UVE{0.0F, 0.0F};
            } else {
                agent.targetReached = false;
                agent.desiredVelocity =
                    toTarget * (agent.maxSpeedPixelsPerSecond / dist);
            }
            for (const auto& [obsPos, obsR] : obstacles2D) {
                const Math::Vector2UVE delta = pos - obsPos;
                const float d = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                const float combinedR = obsR + agent.radiusPixels;
                if (d > 1.0e-5F && d < combinedR) {
                    const float push = (combinedR - d) / combinedR;
                    agent.desiredVelocity =
                        agent.desiredVelocity +
                        delta * ((agent.maxSpeedPixelsPerSecond * push) / d);
                }
            }
            if (dt > 0.0F && entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
                auto& tr = entityManager.GetComponentUVE<TransformComponentUVE>(entity);
                tr.localPosition.x += agent.desiredVelocity.x * dt;
                tr.localPosition.y += agent.desiredVelocity.y * dt;
            }
            ++updatedCount;
        });

    entityManager.ForEachUVE<Particles2DComponentUVE>(
        [&](const EntityUVE entity, Particles2DComponentUVE& p) {
            p.activeParticleCount =
                (p.emitting && !entityManager.HasComponentUVE<DisabledComponentUVE>(entity))
                    ? p.amount
                    : 0U;
            if (p.activeParticleCount > 0U) {
                ++updatedCount;
            }
        });

    return updatedCount;
}

} // namespace UVE::Scene
