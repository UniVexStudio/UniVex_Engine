// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/interaction_area_3d_uve.h"

namespace UVE::Scene {

bool IsInteractionArea3DNodeComponentValidUVE(const InteractionArea3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F && value.collisionLayer != 0U &&
           value.maximumCandidates > 0U && value.maximumCandidates <= 4096U &&
           IsBounded3DNodeStringUVE(value.interactionTag, false);
}

} // namespace UVE::Scene
