// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct SpringArm3DNodeComponentUVE final {

    float armLength = 4.0F;
    float margin = 0.1F;
    float smoothing = 8.0F;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    float currentLength = 4.0F;
    bool enabled = true;
};

[[nodiscard]] bool IsSpringArm3DNodeComponentValidUVE(const SpringArm3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
