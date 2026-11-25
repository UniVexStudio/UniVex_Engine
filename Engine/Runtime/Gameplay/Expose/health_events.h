// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "entity.h"

#include <string>

namespace UVE::Gameplay {

struct HealthDamagedEvent final {
    Scene::Entity entity = Scene::kInvalidEntity;
    Scene::Entity hitbox = Scene::kInvalidEntity;
    Scene::Entity hurtbox = Scene::kInvalidEntity;
    float amount = 0.0F;
    float remaining = 0.0F;

    [[nodiscard]] bool operator==(const HealthDamagedEvent&) const noexcept = default;
};

struct HealthDepletedEvent final {
    Scene::Entity entity = Scene::kInvalidEntity;

    [[nodiscard]] bool operator==(const HealthDepletedEvent&) const noexcept = default;
};

struct HealthHealedEvent final {
    Scene::Entity entity = Scene::kInvalidEntity;
    std::string effectId;
    float amount = 0.0F;
    float remaining = 0.0F;

    [[nodiscard]] bool operator==(const HealthHealedEvent&) const noexcept = default;
};

} // namespace UVE::Gameplay
