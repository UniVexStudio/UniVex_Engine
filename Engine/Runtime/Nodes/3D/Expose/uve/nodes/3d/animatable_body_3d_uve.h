// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct AnimatableBody3DNodeComponentUVE final {
    Math::Vector3UVE targetVelocity{};
    float interpolation = 1.0F;
    bool active = true;
};

[[nodiscard]] bool IsAnimatableBody3DNodeComponentValidUVE(const AnimatableBody3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
