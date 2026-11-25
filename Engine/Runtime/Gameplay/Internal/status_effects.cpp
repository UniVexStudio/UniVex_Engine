// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "status_effects.h"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsValidId(const std::string_view id) noexcept {
    return !id.empty() && id.size() <= kMaximumStatusEffectIdBytes;
}

[[nodiscard]] bool IsValidTarget(const std::string_view attributeId) noexcept {
    return !attributeId.empty() && attributeId.size() <= kMaximumGameplayAttributeIdBytes;
}

[[nodiscard]] bool IsUsable(const StatusEffect& effect) noexcept {
    return IsValidId(effect.effectId) && IsValidTarget(effect.attributeId) &&
           std::isfinite(effect.magnitudePerSecond) && std::isfinite(effect.remainingSeconds) &&
           effect.remainingSeconds >= 0.0F &&
           static_cast<std::uint8_t>(effect.stacking) <=
               static_cast<std::uint8_t>(StatusEffectStacking::Stack) &&
           effect.stacks >= 1U && effect.maxStacks >= 1U && effect.stacks <= effect.maxStacks &&
           std::isfinite(effect.maxDelta);
}

void ClampPoolToMaximum(GameplayAttribute& pool) noexcept {
    if (!std::isfinite(pool.maximum) || pool.maximum < 0.0F) {
        pool.maximum = 0.0F;
    }
    if (!std::isfinite(pool.current) || pool.current < 0.0F) {
        pool.current = 0.0F;
    } else if (pool.current > pool.maximum) {
        pool.current = pool.maximum;
    }
}

void ShiftPoolMaximum(GameplayAttribute& pool, const float delta) noexcept {
    const double shifted = static_cast<double>(pool.maximum) + static_cast<double>(delta);
    if (!std::isfinite(shifted)) {
        pool.maximum = delta > 0.0F ? std::numeric_limits<float>::max() : 0.0F;
    } else {
        pool.maximum = static_cast<float>(shifted);
    }
    ClampPoolToMaximum(pool);
}

/// Settles a pending maxDelta once the target pool exists. A no-op when settled, zero, missing
/// or broken - the tick retries every frame, so a late pool picks the shift up on arrival.
void SettleEffectMaximum(GameplayAttributesComponent& attributes, StatusEffect& effect) noexcept {
    if (effect.maxDelta == 0.0F || effect.maxApplied) {
        return;
    }
    GameplayAttribute* pool = FindGameplayAttribute(attributes, effect.attributeId);
    if (pool == nullptr || !std::isfinite(pool->maximum)) {
        return;
    }
    ShiftPoolMaximum(*pool, effect.maxDelta);
    effect.maxApplied = true;
}

/// Unwinds a settled maxDelta (remove, expiry, retarget). True when the restored ceiling zeroed
/// a pool that still held something - the only unwind the tick reports as Depleted.
bool ReverseEffectMaximum(GameplayAttributesComponent& attributes, StatusEffect& effect) noexcept {
    if (!effect.maxApplied || effect.maxDelta == 0.0F) {
        return false;
    }
    effect.maxApplied = false;
    GameplayAttribute* pool = FindGameplayAttribute(attributes, effect.attributeId);
    if (pool == nullptr) {
        return false;
    }
    const float before = pool->current;
    ShiftPoolMaximum(*pool, -effect.maxDelta);
    return before > 0.0F && pool->current <= 0.0F;
}

} // namespace

bool IsStatusEffectsComponentValid(const StatusEffectsComponent& value) noexcept {
    if (value.effects.size() > kMaximumStatusEffects) {
        return false;
    }
    for (const StatusEffect& effect : value.effects) {
        if (!IsUsable(effect)) {
            return false;
        }
    }
    for (std::size_t first = 0U; first < value.effects.size(); ++first) {
        for (std::size_t second = first + 1U; second < value.effects.size(); ++second) {
            if (value.effects[first].effectId == value.effects[second].effectId) {
                return false;
            }
        }
    }
    return true;
}

bool ApplyStatusEffect(GameplayAttributesComponent& attributes, StatusEffectsComponent& effects,
                           std::string effectId, std::string attributeId, const float magnitudePerSecond,
                           const float durationSeconds, const StatusEffectStacking stacking,
                           const std::uint32_t maxStacks, const float maxDelta) {
    if (!IsValidId(effectId) || !IsValidTarget(attributeId) || !std::isfinite(magnitudePerSecond) ||
        !std::isfinite(durationSeconds) || durationSeconds <= 0.0F ||
        static_cast<std::uint8_t>(stacking) > static_cast<std::uint8_t>(StatusEffectStacking::Stack) ||
        maxStacks == 0U || !std::isfinite(maxDelta) ||
        (maxDelta != 0.0F && attributeId == kReservedHealthAttributeId)) {
        return false;
    }
    if (maxDelta != 0.0F) {
        GameplayAttribute* pool = FindGameplayAttribute(attributes, attributeId);
        if (pool != nullptr && !std::isfinite(pool->maximum)) {
            return false;
        }
    }
    for (StatusEffect& held : effects.effects) {
        if (held.effectId != effectId) {
            continue;
        }
        static_cast<void>(ReverseEffectMaximum(attributes, held));
        held.attributeId = std::move(attributeId);
        held.magnitudePerSecond = magnitudePerSecond;
        held.remainingSeconds = durationSeconds;
        held.stacking = stacking;
        held.maxStacks = maxStacks;
        held.maxDelta = maxDelta;
        held.maxApplied = false;
        held.stacks = stacking == StatusEffectStacking::Stack
                          ? (held.stacks < maxStacks ? held.stacks + 1U : maxStacks)
                          : 1U;
        SettleEffectMaximum(attributes, held);
        return true;
    }
    if (effects.effects.size() >= kMaximumStatusEffects) {
        return false;
    }
    StatusEffect effect;
    effect.effectId = std::move(effectId);
    effect.attributeId = std::move(attributeId);
    effect.magnitudePerSecond = magnitudePerSecond;
    effect.remainingSeconds = durationSeconds;
    effect.stacking = stacking;
    effect.maxStacks = maxStacks;
    effect.maxDelta = maxDelta;
    SettleEffectMaximum(attributes, effect);
    effects.effects.push_back(std::move(effect));
    return true;
}

bool RemoveStatusEffect(GameplayAttributesComponent& attributes, StatusEffectsComponent& effects,
                            const std::string_view effectId) {
    for (auto held = effects.effects.begin(); held != effects.effects.end(); ++held) {
        if (held->effectId != effectId) {
            continue;
        }
        static_cast<void>(ReverseEffectMaximum(attributes, *held));
        effects.effects.erase(held);
        return true;
    }
    return false;
}

std::vector<GameplayAttributeTickResult> TickGameplayAttributes(GameplayAttributesComponent& attributes,
                                                                      StatusEffectsComponent* status,
                                                                      const float deltaSeconds) {
    std::vector<GameplayAttributeTickResult> results;
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return results;
    }
    if (status != nullptr) {
        for (StatusEffect& effect : status->effects) {
            SettleEffectMaximum(attributes, effect);
            GameplayAttributeTickResult result;
            result.effectId = effect.effectId;
            result.attributeId = effect.attributeId;
            if (effect.attributeId == kReservedHealthAttributeId) {
                result.amount = effect.magnitudePerSecond * static_cast<float>(effect.stacks) * deltaSeconds;
            } else if (GameplayAttribute* pool = FindGameplayAttribute(attributes, effect.attributeId);
                       pool != nullptr) {
                const float drift =
                    effect.magnitudePerSecond * static_cast<float>(effect.stacks) * deltaSeconds;
                if (drift >= 0.0F) {
                    const GameplayAttributeDamageResult damage = DamageGameplayAttribute(*pool, drift);
                    result.amount = damage.amount;
                    result.remaining = damage.remaining;
                    result.applied = damage.applied;
                    result.depleted = damage.depleted;
                } else {
                    const GameplayAttributeDamageResult heal = HealGameplayAttribute(*pool, -drift);
                    result.amount = -heal.amount;
                    result.remaining = heal.remaining;
                    result.applied = heal.applied;
                }
            }
            effect.remainingSeconds -= deltaSeconds;
            result.expired = effect.remainingSeconds <= 0.0F;
            if (result.expired && ReverseEffectMaximum(attributes, effect)) {
                result.depleted = true;
            }
            results.push_back(std::move(result));
        }
        std::erase_if(status->effects,
                      [](const StatusEffect& effect) { return effect.remainingSeconds <= 0.0F; });
    }
    for (GameplayAttribute& pool : attributes.attributes) {
        if (pool.regenPerSecond == 0.0F) {
            continue;
        }
        const float drift = pool.regenPerSecond * deltaSeconds;
        GameplayAttributeTickResult result;
        result.attributeId = pool.id;
        if (drift >= 0.0F) {
            const GameplayAttributeDamageResult heal = HealGameplayAttribute(pool, drift);
            result.amount = -heal.amount;
            result.remaining = heal.remaining;
            result.applied = heal.applied;
        } else {
            const GameplayAttributeDamageResult damage = DamageGameplayAttribute(pool, -drift);
            result.amount = damage.amount;
            result.remaining = damage.remaining;
            result.applied = damage.applied;
            result.depleted = damage.depleted;
        }
        results.push_back(std::move(result));
    }
    return results;
}

} // namespace UVE::Scene
