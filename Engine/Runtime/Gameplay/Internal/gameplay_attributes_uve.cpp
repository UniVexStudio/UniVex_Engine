// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/gameplay_attributes_uve.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsValidIdUVE(const std::string_view id) noexcept {
    return !id.empty() && id.size() <= kMaximumGameplayAttributeIdBytesUVE &&
           id != kReservedHealthAttributeIdUVE;
}

[[nodiscard]] bool IsUsableUVE(const GameplayAttributeUVE& attribute) noexcept {
    return IsValidIdUVE(attribute.id) && std::isfinite(attribute.current) &&
           std::isfinite(attribute.maximum) && std::isfinite(attribute.regenPerSecond) &&
           attribute.maximum >= 0.0F && attribute.current >= 0.0F && attribute.current <= attribute.maximum;
}

} // namespace

bool IsGameplayAttributesComponentValidUVE(const GameplayAttributesComponentUVE& value) noexcept {
    if (value.attributes.size() > kMaximumGameplayAttributesUVE) {
        return false;
    }
    for (const GameplayAttributeUVE& attribute : value.attributes) {
        if (!IsUsableUVE(attribute)) {
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

GameplayAttributeUVE* FindGameplayAttributeUVE(GameplayAttributesComponentUVE& attributes,
                                               const std::string_view id) noexcept {
    for (GameplayAttributeUVE& attribute : attributes.attributes) {
        if (attribute.id == id) {
            return &attribute;
        }
    }
    return nullptr;
}

const GameplayAttributeUVE* FindGameplayAttributeUVE(const GameplayAttributesComponentUVE& attributes,
                                                     const std::string_view id) noexcept {
    for (const GameplayAttributeUVE& attribute : attributes.attributes) {
        if (attribute.id == id) {
            return &attribute;
        }
    }
    return nullptr;
}

bool AddGameplayAttributeUVE(GameplayAttributesComponentUVE& attributes, std::string id, const float maximum,
                             const float initial, const float regenPerSecond) {
    if (!IsValidIdUVE(id) || !std::isfinite(maximum) || maximum < 0.0F || !std::isfinite(initial) ||
        !std::isfinite(regenPerSecond) || attributes.attributes.size() >= kMaximumGameplayAttributesUVE ||
        FindGameplayAttributeUVE(attributes, id) != nullptr) {
        return false;
    }
    GameplayAttributeUVE attribute;
    attribute.id = std::move(id);
    attribute.maximum = maximum;
    attribute.current = std::clamp(initial, 0.0F, maximum);
    attribute.regenPerSecond = regenPerSecond;
    attributes.attributes.push_back(std::move(attribute));
    return true;
}

bool RemoveGameplayAttributeUVE(GameplayAttributesComponentUVE& attributes, const std::string_view id) {
    const auto removed = std::erase_if(attributes.attributes,
                                       [id](const GameplayAttributeUVE& attribute) { return attribute.id == id; });
    return removed > 0;
}

GameplayAttributeDamageResultUVE DamageGameplayAttributeUVE(GameplayAttributeUVE& attribute,
                                                            const float amount) noexcept {
    if (!std::isfinite(amount) || amount <= 0.0F) {
        return {};
    }
    const float applied = std::min(amount, attribute.current);
    attribute.current -= applied;
    return {applied, attribute.current, true, attribute.current <= 0.0F};
}

GameplayAttributeDamageResultUVE HealGameplayAttributeUVE(GameplayAttributeUVE& attribute,
                                                          const float amount) noexcept {
    if (!std::isfinite(amount) || amount <= 0.0F) {
        return {};
    }
    const float applied = std::min(amount, attribute.maximum - attribute.current);
    attribute.current += applied;
    return {applied, attribute.current, true, false};
}

bool SetGameplayAttributeMaximumUVE(GameplayAttributeUVE& attribute, const float maximum) noexcept {
    if (!std::isfinite(maximum) || maximum < 0.0F) {
        return false;
    }
    attribute.maximum = maximum;
    attribute.current = std::min(attribute.current, maximum);
    return true;
}

} // namespace UVE::Scene
