// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/component/entity_uve.h"

namespace UVE::Gameplay {

struct TriggerFiredEventUVE final {
    Scene::EntityUVE trigger = Scene::kInvalidEntityUVE;
    Scene::EntityUVE interactor = Scene::kInvalidEntityUVE;
    std::uint32_t firedCount = 0U;

    [[nodiscard]] bool operator==(const TriggerFiredEventUVE&) const noexcept = default;
};

} // namespace UVE::Gameplay
