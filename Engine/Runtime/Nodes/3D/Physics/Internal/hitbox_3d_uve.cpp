// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/hitbox_3d_uve.h"

namespace UVE::Scene {

bool IsHitbox3DNodeComponentValidUVE(const Hitbox3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F && value.collisionLayer != 0U &&
           IsBounded3DNodeStringUVE(value.damageChannel, false);
}

} // namespace UVE::Scene
