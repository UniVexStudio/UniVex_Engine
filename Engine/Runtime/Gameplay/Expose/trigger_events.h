// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "entity.h"

namespace UVE::Gameplay {

struct TriggerFiredEvent final {
    Scene::Entity trigger = Scene::kInvalidEntity;
    Scene::Entity interactor = Scene::kInvalidEntity;
    std::uint32_t firedCount = 0U;

    [[nodiscard]] bool operator==(const TriggerFiredEvent&) const noexcept = default;
};

} // namespace UVE::Gameplay
