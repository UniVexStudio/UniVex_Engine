// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "entity.h"

namespace UVE::Gameplay {

struct InteractRequestedEventUVE final {
    Scene::EntityUVE interactor = Scene::kInvalidEntityUVE;
    Scene::EntityUVE area = Scene::kInvalidEntityUVE;

    [[nodiscard]] bool operator==(const InteractRequestedEventUVE&) const noexcept = default;
};

} // namespace UVE::Gameplay
