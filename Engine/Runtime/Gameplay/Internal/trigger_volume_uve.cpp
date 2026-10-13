// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/trigger_volume_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsTriggerVolumeComponentValidUVE(const TriggerVolumeComponentUVE& value) noexcept {
    return static_cast<std::uint8_t>(value.policy) <=
               static_cast<std::uint8_t>(TriggerFirePolicyUVE::WhileOccupied) &&
           std::isfinite(value.intervalSeconds) && value.intervalSeconds > 0.0F &&
           std::isfinite(value.cooldownSeconds) && value.cooldownSeconds >= 0.0F &&
           std::isfinite(value.cooldownRemaining) && value.cooldownRemaining >= 0.0F &&
           std::isfinite(value.intervalRemaining) && value.intervalRemaining >= 0.0F;
}

std::vector<TriggerFiredResultUVE> UpdateTriggerVolumesUVE(
    IEntityManagerUVE& entityManager, const Physics::AreaOverlapQueryResultUVE& snapshot,
    const std::span<const Physics::AreaOverlapTransitionUVE> transitions, const float deltaSeconds) {
    std::vector<TriggerFiredResultUVE> fires;
    std::vector<EntityUVE> triggers;
    entityManager.ForEachUVE<TriggerVolumeComponentUVE>(
        [&triggers](const EntityUVE entity, const TriggerVolumeComponentUVE&) {
            triggers.push_back(entity);
        });
    std::sort(triggers.begin(), triggers.end(), [](const EntityUVE& lhs, const EntityUVE& rhs) {
        return lhs.index != rhs.index ? lhs.index < rhs.index : lhs.generation < rhs.generation;
    });
    // A bad step freezes the clocks; the edges below still process, because transitions are
    // ephemeral - the tracker never repeats them, so skipping them would lose fires.
    const float step = (std::isfinite(deltaSeconds) && deltaSeconds > 0.0F) ? deltaSeconds : 0.0F;
    for (const EntityUVE trigger : triggers) {
        TriggerVolumeComponentUVE& volume =
            entityManager.GetComponentUVE<TriggerVolumeComponentUVE>(trigger);
        if (!IsTriggerVolumeComponentValidUVE(volume) || !volume.armed) {
            continue;
        }
        volume.cooldownRemaining = std::max(0.0F, volume.cooldownRemaining - step);
        volume.intervalRemaining = std::max(0.0F, volume.intervalRemaining - step);
        std::size_t occupancy = 0U;
        EntityUVE firstOccupant = kInvalidEntityUVE;
        for (const Physics::AreaOverlapPairUVE& overlap : snapshot.overlaps) {
            if (overlap.area != trigger) {
                continue;
            }
            if (occupancy == 0U) {
                firstOccupant = overlap.other;
            }
            ++occupancy;
        }
        const auto fire = [&](const EntityUVE interactor) {
            ++volume.firedCount;
            volume.cooldownRemaining = volume.cooldownSeconds;
            if (volume.policy == TriggerFirePolicyUVE::WhileOccupied) {
                volume.intervalRemaining = volume.intervalSeconds;
            }
            fires.push_back(TriggerFiredResultUVE{trigger, interactor, volume.firedCount});
        };
        for (const Physics::AreaOverlapTransitionUVE& transition : transitions) {
            if (transition.kind != Physics::AreaOverlapTransitionKindUVE::Entered ||
                transition.pair.area != trigger) {
                continue;
            }
            if (volume.cooldownRemaining > 0.0F) {
                continue;
            }
            fire(transition.pair.other);
            if (volume.policy == TriggerFirePolicyUVE::OneShot) {
                volume.armed = false;
                break;
            }
        }
        if (volume.policy == TriggerFirePolicyUVE::WhileOccupied && occupancy > 0U &&
            volume.cooldownRemaining <= 0.0F && volume.intervalRemaining <= 0.0F) {
            fire(firstOccupant);
        }
        if (occupancy == 0U) {
            volume.intervalRemaining = 0.0F;
        }
    }
    return fires;
}

bool RearmTriggerVolumeUVE(IEntityManagerUVE& entityManager, const EntityUVE trigger) {
    if (!entityManager.HasComponentUVE<TriggerVolumeComponentUVE>(trigger)) {
        return false;
    }
    TriggerVolumeComponentUVE& volume =
        entityManager.GetComponentUVE<TriggerVolumeComponentUVE>(trigger);
    volume.armed = true;
    volume.cooldownRemaining = 0.0F;
    volume.intervalRemaining = 0.0F;
    return true;
}

bool DisarmTriggerVolumeUVE(IEntityManagerUVE& entityManager, const EntityUVE trigger) {
    if (!entityManager.HasComponentUVE<TriggerVolumeComponentUVE>(trigger)) {
        return false;
    }
    entityManager.GetComponentUVE<TriggerVolumeComponentUVE>(trigger).armed = false;
    return true;
}

} // namespace UVE::Scene
