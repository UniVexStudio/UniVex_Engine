// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"

#include <string>

namespace UVE::Ai {

/// Queued when a brain's selection changes: which brain, which action, at what score.
struct AiActionSelectedUVE final {
    Scene::EntityUVE brain = Scene::kInvalidEntityUVE;
    std::string actionId;
    float score = 0.0F;

    [[nodiscard]] bool operator==(const AiActionSelectedUVE&) const = default;
};

} // namespace UVE::Ai
