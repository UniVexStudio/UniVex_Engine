// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"

#include <string>

namespace UVE::Gameplay {

struct HealthDamagedEventUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    Scene::EntityUVE hitbox = Scene::kInvalidEntityUVE;
    Scene::EntityUVE hurtbox = Scene::kInvalidEntityUVE;
    float amount = 0.0F;
    float remaining = 0.0F;

    [[nodiscard]] bool operator==(const HealthDamagedEventUVE&) const noexcept = default;
};

struct HealthDepletedEventUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;

    [[nodiscard]] bool operator==(const HealthDepletedEventUVE&) const noexcept = default;
};

struct HealthHealedEventUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    std::string effectId;
    float amount = 0.0F;
    float remaining = 0.0F;

    [[nodiscard]] bool operator==(const HealthHealedEventUVE&) const noexcept = default;
};

} // namespace UVE::Gameplay
