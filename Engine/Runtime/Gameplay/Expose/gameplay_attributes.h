// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Scene {

// ---- Gameplay attributes -----------------------------------------------------------
//
// Named meters beyond hit points - stamina, mana, charge, whatever a game needs - as one
// component instead of one-off floats per game. Each attribute is `{current, maximum,
// regenPerSecond}`; damage/heal clamp into `[0, maximum]` and report what happened so the caller
// can queue events. The per-frame regen tick lives with the status-effect tick, not here: this
// file is pure data plus pure operations.
//
// Health deliberately stays OUT: combat hit points remain HealthComponent (untouched, tested,
// wired into the strike pipeline), and the id "health" is reserved - AddGameplayAttribute
// rejects it - so the status tick can route "health" effects to the health component without any
// ambiguity about which pool is meant.

inline constexpr std::size_t kMaximumGameplayAttributes = 16U;
inline constexpr std::size_t kMaximumGameplayAttributeIdBytes = 48U;
inline constexpr std::string_view kReservedHealthAttributeId = "health";

struct GameplayAttribute final {
    std::string id;
    float current = 0.0F;
    float maximum = 0.0F;
    /// Passive drift per second, applied by the tick: positive regrows, negative decays.
    float regenPerSecond = 0.0F;

    [[nodiscard]] bool operator==(const GameplayAttribute&) const = default;
};

struct GameplayAttributesComponent final {
    std::vector<GameplayAttribute> attributes;

    [[nodiscard]] bool operator==(const GameplayAttributesComponent&) const = default;
};

/// Bounded count, usable ids (non-empty, bounded, unique, never the reserved health id),
/// finite numbers, and `0 <= current <= maximum`.
[[nodiscard]] bool IsGameplayAttributesComponentValid(const GameplayAttributesComponent& value) noexcept;

[[nodiscard]] GameplayAttribute* FindGameplayAttribute(GameplayAttributesComponent& attributes,
                                                            std::string_view id) noexcept;
[[nodiscard]] const GameplayAttribute* FindGameplayAttribute(const GameplayAttributesComponent& attributes,
                                                                  std::string_view id) noexcept;

/// Adds `id` with `maximum` and `initial` clamped into range. False for a bad id (empty,
/// overlong, reserved, duplicate), a negative or non-finite maximum/initial/regen, or a full set.
[[nodiscard]] bool AddGameplayAttribute(GameplayAttributesComponent& attributes, std::string id,
                                           float maximum, float initial, float regenPerSecond = 0.0F);

/// Removes `id`. False when it was never there.
[[nodiscard]] bool RemoveGameplayAttribute(GameplayAttributesComponent& attributes, std::string_view id);

struct GameplayAttributeDamageResult final {
    float amount = 0.0F;
    float remaining = 0.0F;
    bool applied = false;
    bool depleted = false;

    [[nodiscard]] bool operator==(const GameplayAttributeDamageResult&) const = default;
};

/// Subtracts up to `amount`, clamped at zero. `applied` means the operation ran (a valid amount),
/// even when there was nothing left to take; `depleted` means the pool hit zero.
[[nodiscard]] GameplayAttributeDamageResult DamageGameplayAttribute(GameplayAttribute& attribute,
                                                                         float amount) noexcept;

/// Restores up to `amount`, clamped at maximum. Never depletes.
[[nodiscard]] GameplayAttributeDamageResult HealGameplayAttribute(GameplayAttribute& attribute,
                                                                       float amount) noexcept;

/// Resets the ceiling, pulling `current` down when it pokes over. False for negative/non-finite.
[[nodiscard]] bool SetGameplayAttributeMaximum(GameplayAttribute& attribute, float maximum) noexcept;

} // namespace UVE::Scene
