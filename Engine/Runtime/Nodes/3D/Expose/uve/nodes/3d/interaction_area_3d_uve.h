// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct InteractionArea3DNodeComponentUVE final {
    Math::Vector3UVE halfExtents{1.0F, 1.0F, 1.0F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string interactionTag = "interactable";
    std::uint32_t maximumCandidates = 16U;
    bool enabled = true;
};

[[nodiscard]] bool IsInteractionArea3DNodeComponentValidUVE(const InteractionArea3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
