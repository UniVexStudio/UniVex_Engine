// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"

#include <string>

namespace UVE::Gameplay {

/// Queued whenever a playing cinematic's playhead passes an event key: which shot, which key.
struct CinematicEventFiredUVE final {
    Scene::EntityUVE cinematic = Scene::kInvalidEntityUVE;
    std::string eventId;
    double timeSeconds = 0.0;

    [[nodiscard]] bool operator==(const CinematicEventFiredUVE&) const = default;
};

} // namespace UVE::Gameplay
