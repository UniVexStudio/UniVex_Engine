// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "entity.h"

#include <string>

namespace UVE::Ai {

/// Queued when a brain's selection changes: which brain, which action, at what score.
struct AiActionSelected final {
    Scene::Entity brain = Scene::kInvalidEntity;
    std::string actionId;
    float score = 0.0F;

    [[nodiscard]] bool operator==(const AiActionSelected&) const = default;
};

} // namespace UVE::Ai
