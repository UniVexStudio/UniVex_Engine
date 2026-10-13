// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/health_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsHealthComponentValidUVE(const HealthComponentUVE& value) noexcept {
    return std::isfinite(value.maxHealth) && value.maxHealth > 0.0F && std::isfinite(value.health) &&
           value.health >= 0.0F;
}

EntityUVE FindHealthEntityUVE(IEntityManagerUVE& entityManager, EntityUVE from) {
    EntityUVE entity = from;
    while (entityManager.IsAliveUVE(entity)) {
        if (entityManager.HasComponentUVE<HealthComponentUVE>(entity) &&
            IsHealthComponentValidUVE(entityManager.GetComponentUVE<HealthComponentUVE>(entity))) {
            return entity;
        }
        if (!entityManager.HasComponentUVE<HierarchyComponentUVE>(entity)) {
            break;
        }
        const EntityUVE parent = entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent;
        if (parent == entity) {
            break;
        }
        entity = parent;
    }
    return kInvalidEntityUVE;
}

HealthDamageResultUVE ApplyHealthDamageUVE(HealthComponentUVE& health, const float amount) noexcept {
    HealthDamageResultUVE result;
    result.remaining = health.health;
    if (!IsHealthComponentValidUVE(health) || health.invulnerable || health.health <= 0.0F ||
        !std::isfinite(amount) || amount <= 0.0F) {
        return result;
    }
    health.health = std::max(0.0F, health.health - amount);
    result.applied = true;
    result.amount = amount;
    result.remaining = health.health;
    result.depleted = health.health <= 0.0F;
    return result;
}

HealthDamageResultUVE ApplyHealthHealUVE(HealthComponentUVE& health, const float amount) noexcept {
    HealthDamageResultUVE result;
    result.remaining = health.health;
    if (!IsHealthComponentValidUVE(health) || !std::isfinite(amount) || amount <= 0.0F ||
        health.health >= health.maxHealth) {
        return result;
    }
    health.health = std::min(health.maxHealth, health.health + amount);
    result.applied = true;
    result.amount = amount;
    result.remaining = health.health;
    return result;
}

HealthDamageResultUVE ApplyHitboxStrikeToHealthUVE(IEntityManagerUVE& entityManager, const EntityUVE hurtbox,
                                                   const float amount) {
    const EntityUVE entity = FindHealthEntityUVE(entityManager, hurtbox);
    if (entity == kInvalidEntityUVE) {
        return {};
    }
    HealthDamageResultUVE result =
        ApplyHealthDamageUVE(entityManager.GetComponentUVE<HealthComponentUVE>(entity), amount);
    result.entity = entity;
    return result;
}

} // namespace UVE::Scene
