// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/status_effects_uve.h"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsValidIdUVE(const std::string_view id) noexcept {
    return !id.empty() && id.size() <= kMaximumStatusEffectIdBytesUVE;
}

[[nodiscard]] bool IsValidTargetUVE(const std::string_view attributeId) noexcept {
    return !attributeId.empty() && attributeId.size() <= kMaximumGameplayAttributeIdBytesUVE;
}

[[nodiscard]] bool IsUsableUVE(const StatusEffectUVE& effect) noexcept {
    return IsValidIdUVE(effect.effectId) && IsValidTargetUVE(effect.attributeId) &&
           std::isfinite(effect.magnitudePerSecond) && std::isfinite(effect.remainingSeconds) &&
           effect.remainingSeconds >= 0.0F &&
           static_cast<std::uint8_t>(effect.stacking) <=
               static_cast<std::uint8_t>(StatusEffectStackingUVE::Stack) &&
           effect.stacks >= 1U && effect.maxStacks >= 1U && effect.stacks <= effect.maxStacks &&
           std::isfinite(effect.maxDelta);
}

void ClampPoolToMaximumUVE(GameplayAttributeUVE& pool) noexcept {
    if (!std::isfinite(pool.maximum) || pool.maximum < 0.0F) {
        pool.maximum = 0.0F;
    }
    if (!std::isfinite(pool.current) || pool.current < 0.0F) {
        pool.current = 0.0F;
    } else if (pool.current > pool.maximum) {
        pool.current = pool.maximum;
    }
}

void ShiftPoolMaximumUVE(GameplayAttributeUVE& pool, const float delta) noexcept {
    const double shifted = static_cast<double>(pool.maximum) + static_cast<double>(delta);
    if (!std::isfinite(shifted)) {
        pool.maximum = delta > 0.0F ? std::numeric_limits<float>::max() : 0.0F;
    } else {
        pool.maximum = static_cast<float>(shifted);
    }
    ClampPoolToMaximumUVE(pool);
}

/// Settles a pending maxDelta once the target pool exists. A no-op when settled, zero, missing
/// or broken - the tick retries every frame, so a late pool picks the shift up on arrival.
void SettleEffectMaximumUVE(GameplayAttributesComponentUVE& attributes, StatusEffectUVE& effect) noexcept {
    if (effect.maxDelta == 0.0F || effect.maxApplied) {
        return;
    }
    GameplayAttributeUVE* pool = FindGameplayAttributeUVE(attributes, effect.attributeId);
    if (pool == nullptr || !std::isfinite(pool->maximum)) {
        return;
    }
    ShiftPoolMaximumUVE(*pool, effect.maxDelta);
    effect.maxApplied = true;
}

/// Unwinds a settled maxDelta (remove, expiry, retarget). True when the restored ceiling zeroed
/// a pool that still held something - the only unwind the tick reports as Depleted.
bool ReverseEffectMaximumUVE(GameplayAttributesComponentUVE& attributes, StatusEffectUVE& effect) noexcept {
    if (!effect.maxApplied || effect.maxDelta == 0.0F) {
        return false;
    }
    effect.maxApplied = false;
    GameplayAttributeUVE* pool = FindGameplayAttributeUVE(attributes, effect.attributeId);
    if (pool == nullptr) {
        return false;
    }
    const float before = pool->current;
    ShiftPoolMaximumUVE(*pool, -effect.maxDelta);
    return before > 0.0F && pool->current <= 0.0F;
}

} // namespace

bool IsStatusEffectsComponentValidUVE(const StatusEffectsComponentUVE& value) noexcept {
    if (value.effects.size() > kMaximumStatusEffectsUVE) {
        return false;
    }
    for (const StatusEffectUVE& effect : value.effects) {
        if (!IsUsableUVE(effect)) {
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

bool ApplyStatusEffectUVE(GameplayAttributesComponentUVE& attributes, StatusEffectsComponentUVE& effects,
                           std::string effectId, std::string attributeId, const float magnitudePerSecond,
                           const float durationSeconds, const StatusEffectStackingUVE stacking,
                           const std::uint32_t maxStacks, const float maxDelta) {
    if (!IsValidIdUVE(effectId) || !IsValidTargetUVE(attributeId) || !std::isfinite(magnitudePerSecond) ||
        !std::isfinite(durationSeconds) || durationSeconds <= 0.0F ||
        static_cast<std::uint8_t>(stacking) > static_cast<std::uint8_t>(StatusEffectStackingUVE::Stack) ||
        maxStacks == 0U || !std::isfinite(maxDelta) ||
        (maxDelta != 0.0F && attributeId == kReservedHealthAttributeIdUVE)) {
        return false;
    }
    if (maxDelta != 0.0F) {
        GameplayAttributeUVE* pool = FindGameplayAttributeUVE(attributes, attributeId);
        if (pool != nullptr && !std::isfinite(pool->maximum)) {
            return false;
        }
    }
    for (StatusEffectUVE& held : effects.effects) {
        if (held.effectId != effectId) {
            continue;
        }
        static_cast<void>(ReverseEffectMaximumUVE(attributes, held));
        held.attributeId = std::move(attributeId);
        held.magnitudePerSecond = magnitudePerSecond;
        held.remainingSeconds = durationSeconds;
        held.stacking = stacking;
        held.maxStacks = maxStacks;
        held.maxDelta = maxDelta;
        held.maxApplied = false;
        held.stacks = stacking == StatusEffectStackingUVE::Stack
                          ? (held.stacks < maxStacks ? held.stacks + 1U : maxStacks)
                          : 1U;
        SettleEffectMaximumUVE(attributes, held);
        return true;
    }
    if (effects.effects.size() >= kMaximumStatusEffectsUVE) {
        return false;
    }
    StatusEffectUVE effect;
    effect.effectId = std::move(effectId);
    effect.attributeId = std::move(attributeId);
    effect.magnitudePerSecond = magnitudePerSecond;
    effect.remainingSeconds = durationSeconds;
    effect.stacking = stacking;
    effect.maxStacks = maxStacks;
    effect.maxDelta = maxDelta;
    SettleEffectMaximumUVE(attributes, effect);
    effects.effects.push_back(std::move(effect));
    return true;
}

bool RemoveStatusEffectUVE(GameplayAttributesComponentUVE& attributes, StatusEffectsComponentUVE& effects,
                            const std::string_view effectId) {
    for (auto held = effects.effects.begin(); held != effects.effects.end(); ++held) {
        if (held->effectId != effectId) {
            continue;
        }
        static_cast<void>(ReverseEffectMaximumUVE(attributes, *held));
        effects.effects.erase(held);
        return true;
    }
    return false;
}

std::vector<GameplayAttributeTickResultUVE> TickGameplayAttributesUVE(GameplayAttributesComponentUVE& attributes,
                                                                      StatusEffectsComponentUVE* status,
                                                                      const float deltaSeconds) {
    std::vector<GameplayAttributeTickResultUVE> results;
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return results;
    }
    if (status != nullptr) {
        for (StatusEffectUVE& effect : status->effects) {
            SettleEffectMaximumUVE(attributes, effect);
            GameplayAttributeTickResultUVE result;
            result.effectId = effect.effectId;
            result.attributeId = effect.attributeId;
            if (effect.attributeId == kReservedHealthAttributeIdUVE) {
                result.amount = effect.magnitudePerSecond * static_cast<float>(effect.stacks) * deltaSeconds;
            } else if (GameplayAttributeUVE* pool = FindGameplayAttributeUVE(attributes, effect.attributeId);
                       pool != nullptr) {
                const float drift =
                    effect.magnitudePerSecond * static_cast<float>(effect.stacks) * deltaSeconds;
                if (drift >= 0.0F) {
                    const GameplayAttributeDamageResultUVE damage = DamageGameplayAttributeUVE(*pool, drift);
                    result.amount = damage.amount;
                    result.remaining = damage.remaining;
                    result.applied = damage.applied;
                    result.depleted = damage.depleted;
                } else {
                    const GameplayAttributeDamageResultUVE heal = HealGameplayAttributeUVE(*pool, -drift);
                    result.amount = -heal.amount;
                    result.remaining = heal.remaining;
                    result.applied = heal.applied;
                }
            }
            effect.remainingSeconds -= deltaSeconds;
            result.expired = effect.remainingSeconds <= 0.0F;
            if (result.expired && ReverseEffectMaximumUVE(attributes, effect)) {
                result.depleted = true;
            }
            results.push_back(std::move(result));
        }
        std::erase_if(status->effects,
                      [](const StatusEffectUVE& effect) { return effect.remainingSeconds <= 0.0F; });
    }
    for (GameplayAttributeUVE& pool : attributes.attributes) {
        if (pool.regenPerSecond == 0.0F) {
            continue;
        }
        const float drift = pool.regenPerSecond * deltaSeconds;
        GameplayAttributeTickResultUVE result;
        result.attributeId = pool.id;
        if (drift >= 0.0F) {
            const GameplayAttributeDamageResultUVE heal = HealGameplayAttributeUVE(pool, drift);
            result.amount = -heal.amount;
            result.remaining = heal.remaining;
            result.applied = heal.applied;
        } else {
            const GameplayAttributeDamageResultUVE damage = DamageGameplayAttributeUVE(pool, -drift);
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
