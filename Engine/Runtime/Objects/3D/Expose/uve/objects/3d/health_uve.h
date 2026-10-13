// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr float kDefaultHitboxStrikeDamageUVE = 1.0F;

struct HealthComponentUVE final {
    float maxHealth = 100.0F;
    float health = 100.0F;
    bool invulnerable = false;
};

[[nodiscard]] bool IsHealthComponentValidUVE(const HealthComponentUVE& value) noexcept;

struct HealthDamageResultUVE final {
    EntityUVE entity = kInvalidEntityUVE;
    float amount = 0.0F;
    float remaining = 0.0F;
    bool applied = false;
    bool depleted = false;
};

[[nodiscard]] EntityUVE FindHealthEntityUVE(IEntityManagerUVE& entityManager, EntityUVE from);

[[nodiscard]] HealthDamageResultUVE ApplyHealthDamageUVE(HealthComponentUVE& health, float amount) noexcept;

/// Restores up to `maxHealth`: healing ignores invulnerability and revives the living dead (0 hp
/// is depleted, not gone), but the already-full and the invalid take nothing. Reports the
/// requested amount with the clamped remainder; `depleted` stays false - healing never empties.
[[nodiscard]] HealthDamageResultUVE ApplyHealthHealUVE(HealthComponentUVE& health, float amount) noexcept;

[[nodiscard]] HealthDamageResultUVE ApplyHitboxStrikeToHealthUVE(
    IEntityManagerUVE& entityManager, EntityUVE hurtbox,
    float amount = kDefaultHitboxStrikeDamageUVE);

} // namespace UVE::Scene
