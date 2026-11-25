// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "entity.h"

#include <string>

namespace UVE::Gameplay {

/// Queued whenever a gameplay attribute loses value: who, which pool, how much, what is left.
struct AttributeDamagedEvent final {
    Scene::Entity entity = Scene::kInvalidEntity;
    std::string attribute;
    float amount = 0.0F;
    float remaining = 0.0F;

    [[nodiscard]] bool operator==(const AttributeDamagedEvent&) const = default;
};

/// Queued when a gameplay attribute hits zero.
struct AttributeDepletedEvent final {
    Scene::Entity entity = Scene::kInvalidEntity;
    std::string attribute;

    [[nodiscard]] bool operator==(const AttributeDepletedEvent&) const = default;
};

} // namespace UVE::Gameplay
