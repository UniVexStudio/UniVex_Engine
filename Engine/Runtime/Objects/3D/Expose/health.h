// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "entity.h"

namespace UVE::Scene {

class IEntityManager;

inline constexpr float kDefaultHitboxStrikeDamage = 1.0F;

struct HealthComponent final {
    float maxHealth = 100.0F;
    float health = 100.0F;
    bool invulnerable = false;
};

[[nodiscard]] bool IsHealthComponentValid(const HealthComponent& value) noexcept;

struct HealthDamageResult final {
    Entity entity = kInvalidEntity;
    float amount = 0.0F;
    float remaining = 0.0F;
    bool applied = false;
    bool depleted = false;
};

[[nodiscard]] Entity FindHealthEntity(IEntityManager& entityManager, Entity from);

[[nodiscard]] HealthDamageResult ApplyHealthDamage(HealthComponent& health, float amount) noexcept;

/// Restores up to `maxHealth`: healing ignores invulnerability and revives the living dead (0 hp
/// is depleted, not gone), but the already-full and the invalid take nothing. Reports the
/// requested amount with the clamped remainder; `depleted` stays false - healing never empties.
[[nodiscard]] HealthDamageResult ApplyHealthHeal(HealthComponent& health, float amount) noexcept;

[[nodiscard]] HealthDamageResult ApplyHitboxStrikeToHealth(
    IEntityManager& entityManager, Entity hurtbox,
    float amount = kDefaultHitboxStrikeDamage);

} // namespace UVE::Scene
