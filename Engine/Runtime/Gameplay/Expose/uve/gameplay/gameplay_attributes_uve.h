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
// Health deliberately stays OUT: combat hit points remain HealthComponentUVE (untouched, tested,
// wired into the strike pipeline), and the id "health" is reserved - AddGameplayAttributeUVE
// rejects it - so the status tick can route "health" effects to the health component without any
// ambiguity about which pool is meant.

inline constexpr std::size_t kMaximumGameplayAttributesUVE = 16U;
inline constexpr std::size_t kMaximumGameplayAttributeIdBytesUVE = 48U;
inline constexpr std::string_view kReservedHealthAttributeIdUVE = "health";

struct GameplayAttributeUVE final {
    std::string id;
    float current = 0.0F;
    float maximum = 0.0F;
    /// Passive drift per second, applied by the tick: positive regrows, negative decays.
    float regenPerSecond = 0.0F;

    [[nodiscard]] bool operator==(const GameplayAttributeUVE&) const = default;
};

struct GameplayAttributesComponentUVE final {
    std::vector<GameplayAttributeUVE> attributes;

    [[nodiscard]] bool operator==(const GameplayAttributesComponentUVE&) const = default;
};

/// Bounded count, usable ids (non-empty, bounded, unique, never the reserved health id),
/// finite numbers, and `0 <= current <= maximum`.
[[nodiscard]] bool IsGameplayAttributesComponentValidUVE(const GameplayAttributesComponentUVE& value) noexcept;

[[nodiscard]] GameplayAttributeUVE* FindGameplayAttributeUVE(GameplayAttributesComponentUVE& attributes,
                                                            std::string_view id) noexcept;
[[nodiscard]] const GameplayAttributeUVE* FindGameplayAttributeUVE(const GameplayAttributesComponentUVE& attributes,
                                                                  std::string_view id) noexcept;

/// Adds `id` with `maximum` and `initial` clamped into range. False for a bad id (empty,
/// overlong, reserved, duplicate), a negative or non-finite maximum/initial/regen, or a full set.
[[nodiscard]] bool AddGameplayAttributeUVE(GameplayAttributesComponentUVE& attributes, std::string id,
                                           float maximum, float initial, float regenPerSecond = 0.0F);

/// Removes `id`. False when it was never there.
[[nodiscard]] bool RemoveGameplayAttributeUVE(GameplayAttributesComponentUVE& attributes, std::string_view id);

struct GameplayAttributeDamageResultUVE final {
    float amount = 0.0F;
    float remaining = 0.0F;
    bool applied = false;
    bool depleted = false;

    [[nodiscard]] bool operator==(const GameplayAttributeDamageResultUVE&) const = default;
};

/// Subtracts up to `amount`, clamped at zero. `applied` means the operation ran (a valid amount),
/// even when there was nothing left to take; `depleted` means the pool hit zero.
[[nodiscard]] GameplayAttributeDamageResultUVE DamageGameplayAttributeUVE(GameplayAttributeUVE& attribute,
                                                                         float amount) noexcept;

/// Restores up to `amount`, clamped at maximum. Never depletes.
[[nodiscard]] GameplayAttributeDamageResultUVE HealGameplayAttributeUVE(GameplayAttributeUVE& attribute,
                                                                       float amount) noexcept;

/// Resets the ceiling, pulling `current` down when it pokes over. False for negative/non-finite.
[[nodiscard]] bool SetGameplayAttributeMaximumUVE(GameplayAttributeUVE& attribute, float maximum) noexcept;

} // namespace UVE::Scene
