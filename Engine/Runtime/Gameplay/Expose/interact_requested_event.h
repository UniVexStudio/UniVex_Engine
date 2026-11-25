// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "entity.h"

namespace UVE::Gameplay {

struct InteractRequestedEvent final {
    Scene::Entity interactor = Scene::kInvalidEntity;
    Scene::Entity area = Scene::kInvalidEntity;

    [[nodiscard]] bool operator==(const InteractRequestedEvent&) const noexcept = default;
};

} // namespace UVE::Gameplay
