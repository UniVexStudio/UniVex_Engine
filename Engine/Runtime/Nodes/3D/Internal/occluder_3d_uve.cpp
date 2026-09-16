// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/occluder_3d_uve.h"

namespace UVE::Scene {

bool IsOccluder3DNodeComponentValidUVE(const Occluder3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F && value.mode == Occluder3DNodeModeUVE::ConservativeBox;
}

} // namespace UVE::Scene
