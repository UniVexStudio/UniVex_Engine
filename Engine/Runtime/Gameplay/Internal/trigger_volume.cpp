// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "trigger_volume.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsTriggerVolumeComponentValid(const TriggerVolumeComponent& value) noexcept {
    return static_cast<std::uint8_t>(value.policy) <=
               static_cast<std::uint8_t>(TriggerFirePolicy::WhileOccupied) &&
           std::isfinite(value.intervalSeconds) && value.intervalSeconds > 0.0F &&
           std::isfinite(value.cooldownSeconds) && value.cooldownSeconds >= 0.0F &&
           std::isfinite(value.cooldownRemaining) && value.cooldownRemaining >= 0.0F &&
           std::isfinite(value.intervalRemaining) && value.intervalRemaining >= 0.0F;
}

std::vector<TriggerFiredResult> UpdateTriggerVolumes(
    IEntityManager& entityManager, const Physics::AreaOverlapQueryResult& snapshot,
    const std::span<const Physics::AreaOverlapTransition> transitions, const float deltaSeconds) {
    std::vector<TriggerFiredResult> fires;
    std::vector<Entity> triggers;
    entityManager.ForEach<TriggerVolumeComponent>(
        [&triggers](const Entity entity, const TriggerVolumeComponent&) {
            triggers.push_back(entity);
        });
    std::sort(triggers.begin(), triggers.end(), [](const Entity& lhs, const Entity& rhs) {
        return lhs.index != rhs.index ? lhs.index < rhs.index : lhs.generation < rhs.generation;
    });
    // A bad step freezes the clocks; the edges below still process, because transitions are
    // ephemeral - the tracker never repeats them, so skipping them would lose fires.
    const float step = (std::isfinite(deltaSeconds) && deltaSeconds > 0.0F) ? deltaSeconds : 0.0F;
    for (const Entity trigger : triggers) {
        TriggerVolumeComponent& volume =
            entityManager.GetComponent<TriggerVolumeComponent>(trigger);
        if (!IsTriggerVolumeComponentValid(volume) || !volume.armed) {
            continue;
        }
        volume.cooldownRemaining = std::max(0.0F, volume.cooldownRemaining - step);
        volume.intervalRemaining = std::max(0.0F, volume.intervalRemaining - step);
        std::size_t occupancy = 0U;
        Entity firstOccupant = kInvalidEntity;
        for (const Physics::AreaOverlapPair& overlap : snapshot.overlaps) {
            if (overlap.area != trigger) {
                continue;
            }
            if (occupancy == 0U) {
                firstOccupant = overlap.other;
            }
            ++occupancy;
        }
        const auto fire = [&](const Entity interactor) {
            ++volume.firedCount;
            volume.cooldownRemaining = volume.cooldownSeconds;
            if (volume.policy == TriggerFirePolicy::WhileOccupied) {
                volume.intervalRemaining = volume.intervalSeconds;
            }
            fires.push_back(TriggerFiredResult{trigger, interactor, volume.firedCount});
        };
        for (const Physics::AreaOverlapTransition& transition : transitions) {
            if (transition.kind != Physics::AreaOverlapTransitionKind::Entered ||
                transition.pair.area != trigger) {
                continue;
            }
            if (volume.cooldownRemaining > 0.0F) {
                continue;
            }
            fire(transition.pair.other);
            if (volume.policy == TriggerFirePolicy::OneShot) {
                volume.armed = false;
                break;
            }
        }
        if (volume.policy == TriggerFirePolicy::WhileOccupied && occupancy > 0U &&
            volume.cooldownRemaining <= 0.0F && volume.intervalRemaining <= 0.0F) {
            fire(firstOccupant);
        }
        if (occupancy == 0U) {
            volume.intervalRemaining = 0.0F;
        }
    }
    return fires;
}

bool RearmTriggerVolume(IEntityManager& entityManager, const Entity trigger) {
    if (!entityManager.HasComponent<TriggerVolumeComponent>(trigger)) {
        return false;
    }
    TriggerVolumeComponent& volume =
        entityManager.GetComponent<TriggerVolumeComponent>(trigger);
    volume.armed = true;
    volume.cooldownRemaining = 0.0F;
    volume.intervalRemaining = 0.0F;
    return true;
}

bool DisarmTriggerVolume(IEntityManager& entityManager, const Entity trigger) {
    if (!entityManager.HasComponent<TriggerVolumeComponent>(trigger)) {
        return false;
    }
    entityManager.GetComponent<TriggerVolumeComponent>(trigger).armed = false;
    return true;
}

} // namespace UVE::Scene
