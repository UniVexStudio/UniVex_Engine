// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/animatable_body_3d_uve.h"

namespace UVE::Scene {

bool IsAnimatableBody3DNodeComponentValidUVE(const AnimatableBody3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.targetVelocity) && std::isfinite(value.interpolation) &&
           value.interpolation >= 0.0F && value.interpolation <= 1.0F;
}

} // namespace UVE::Scene
