// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/hitbox_3d_uve.h"

namespace UVE::Scene {

bool IsHitbox3DObjectComponentValidUVE(const Hitbox3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F && value.collisionLayer != 0U &&
           IsBounded3DObjectStringUVE(value.damageChannel, false);
}

} // namespace UVE::Scene
