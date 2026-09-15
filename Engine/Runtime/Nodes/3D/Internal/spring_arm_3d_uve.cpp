// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/spring_arm_3d_uve.h"

namespace UVE::Scene {

bool IsSpringArm3DNodeComponentValidUVE(const SpringArm3DNodeComponentUVE& value) noexcept {
    return std::isfinite(value.armLength) && value.armLength > 0.0F && std::isfinite(value.margin) &&
           value.margin >= 0.0F && std::isfinite(value.smoothing) && value.smoothing >= 0.0F &&
           std::isfinite(value.currentLength) && value.currentLength >= 0.0F && value.currentLength <= value.armLength;
}

} // namespace UVE::Scene
