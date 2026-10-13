// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"

#include <string>

namespace UVE::Gameplay {

/// Queued whenever a gameplay attribute loses value: who, which pool, how much, what is left.
struct AttributeDamagedEventUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    std::string attribute;
    float amount = 0.0F;
    float remaining = 0.0F;

    [[nodiscard]] bool operator==(const AttributeDamagedEventUVE&) const = default;
};

/// Queued when a gameplay attribute hits zero.
struct AttributeDepletedEventUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    std::string attribute;

    [[nodiscard]] bool operator==(const AttributeDepletedEventUVE&) const = default;
};

} // namespace UVE::Gameplay
