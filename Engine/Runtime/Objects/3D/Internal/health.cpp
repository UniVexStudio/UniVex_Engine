// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "health.h"

#include <algorithm>
#include <cmath>

#include "hierarchy_component.h"
#include "i_entity_manager.h"

namespace UVE::Scene {

bool IsHealthComponentValid(const HealthComponent& value) noexcept {
    return std::isfinite(value.maxHealth) && value.maxHealth > 0.0F && std::isfinite(value.health) &&
           value.health >= 0.0F;
}

Entity FindHealthEntity(IEntityManager& entityManager, Entity from) {
    Entity entity = from;
    while (entityManager.IsAlive(entity)) {
        if (entityManager.HasComponent<HealthComponent>(entity) &&
            IsHealthComponentValid(entityManager.GetComponent<HealthComponent>(entity))) {
            return entity;
        }
        if (!entityManager.HasComponent<HierarchyComponent>(entity)) {
            break;
        }
        const Entity parent = entityManager.GetComponent<HierarchyComponent>(entity).parent;
        if (parent == entity) {
            break;
        }
        entity = parent;
    }
    return kInvalidEntity;
}

HealthDamageResult ApplyHealthDamage(HealthComponent& health, const float amount) noexcept {
    HealthDamageResult result;
    result.remaining = health.health;
    if (!IsHealthComponentValid(health) || health.invulnerable || health.health <= 0.0F ||
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

HealthDamageResult ApplyHealthHeal(HealthComponent& health, const float amount) noexcept {
    HealthDamageResult result;
    result.remaining = health.health;
    if (!IsHealthComponentValid(health) || !std::isfinite(amount) || amount <= 0.0F ||
        health.health >= health.maxHealth) {
        return result;
    }
    health.health = std::min(health.maxHealth, health.health + amount);
    result.applied = true;
    result.amount = amount;
    result.remaining = health.health;
    return result;
}

HealthDamageResult ApplyHitboxStrikeToHealth(IEntityManager& entityManager, const Entity hurtbox,
                                                   const float amount) {
    const Entity entity = FindHealthEntity(entityManager, hurtbox);
    if (entity == kInvalidEntity) {
        return {};
    }
    HealthDamageResult result =
        ApplyHealthDamage(entityManager.GetComponent<HealthComponent>(entity), amount);
    result.entity = entity;
    return result;
}

} // namespace UVE::Scene
