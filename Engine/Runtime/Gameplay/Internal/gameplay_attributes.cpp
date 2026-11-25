// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gameplay_attributes.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsValidId(const std::string_view id) noexcept {
    return !id.empty() && id.size() <= kMaximumGameplayAttributeIdBytes &&
           id != kReservedHealthAttributeId;
}

[[nodiscard]] bool IsUsable(const GameplayAttribute& attribute) noexcept {
    return IsValidId(attribute.id) && std::isfinite(attribute.current) &&
           std::isfinite(attribute.maximum) && std::isfinite(attribute.regenPerSecond) &&
           attribute.maximum >= 0.0F && attribute.current >= 0.0F && attribute.current <= attribute.maximum;
}

} // namespace

bool IsGameplayAttributesComponentValid(const GameplayAttributesComponent& value) noexcept {
    if (value.attributes.size() > kMaximumGameplayAttributes) {
        return false;
    }
    for (const GameplayAttribute& attribute : value.attributes) {
        if (!IsUsable(attribute)) {
            return false;
        }
    }
    for (std::size_t first = 0U; first < value.attributes.size(); ++first) {
        for (std::size_t second = first + 1U; second < value.attributes.size(); ++second) {
            if (value.attributes[first].id == value.attributes[second].id) {
                return false;
            }
        }
    }
    return true;
}

GameplayAttribute* FindGameplayAttribute(GameplayAttributesComponent& attributes,
                                               const std::string_view id) noexcept {
    for (GameplayAttribute& attribute : attributes.attributes) {
        if (attribute.id == id) {
            return &attribute;
        }
    }
    return nullptr;
}

const GameplayAttribute* FindGameplayAttribute(const GameplayAttributesComponent& attributes,
                                                     const std::string_view id) noexcept {
    for (const GameplayAttribute& attribute : attributes.attributes) {
        if (attribute.id == id) {
            return &attribute;
        }
    }
    return nullptr;
}

bool AddGameplayAttribute(GameplayAttributesComponent& attributes, std::string id, const float maximum,
                             const float initial, const float regenPerSecond) {
    if (!IsValidId(id) || !std::isfinite(maximum) || maximum < 0.0F || !std::isfinite(initial) ||
        !std::isfinite(regenPerSecond) || attributes.attributes.size() >= kMaximumGameplayAttributes ||
        FindGameplayAttribute(attributes, id) != nullptr) {
        return false;
    }
    GameplayAttribute attribute;
    attribute.id = std::move(id);
    attribute.maximum = maximum;
    attribute.current = std::clamp(initial, 0.0F, maximum);
    attribute.regenPerSecond = regenPerSecond;
    attributes.attributes.push_back(std::move(attribute));
    return true;
}

bool RemoveGameplayAttribute(GameplayAttributesComponent& attributes, const std::string_view id) {
    const auto removed = std::erase_if(attributes.attributes,
                                       [id](const GameplayAttribute& attribute) { return attribute.id == id; });
    return removed > 0;
}

GameplayAttributeDamageResult DamageGameplayAttribute(GameplayAttribute& attribute,
                                                            const float amount) noexcept {
    if (!std::isfinite(amount) || amount <= 0.0F) {
        return {};
    }
    const float applied = std::min(amount, attribute.current);
    attribute.current -= applied;
    return {applied, attribute.current, true, attribute.current <= 0.0F};
}

GameplayAttributeDamageResult HealGameplayAttribute(GameplayAttribute& attribute,
                                                          const float amount) noexcept {
    if (!std::isfinite(amount) || amount <= 0.0F) {
        return {};
    }
    const float applied = std::min(amount, attribute.maximum - attribute.current);
    attribute.current += applied;
    return {applied, attribute.current, true, false};
}

bool SetGameplayAttributeMaximum(GameplayAttribute& attribute, const float maximum) noexcept {
    if (!std::isfinite(maximum) || maximum < 0.0F) {
        return false;
    }
    attribute.maximum = maximum;
    attribute.current = std::min(attribute.current, maximum);
    return true;
}

} // namespace UVE::Scene
