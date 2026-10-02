// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/hurtbox_3d_uve.h"

namespace UVE::Scene {

bool IsHurtbox3DObjectComponentValidUVE(const Hurtbox3DComponentUVE& value) noexcept {
    const Hitbox3DComponentUVE equivalent{value.halfExtents, value.collisionLayer, value.collisionMask,
                                               value.damageChannel, value.enabled};
    return IsHitbox3DObjectComponentValidUVE(equivalent);
}

} // namespace UVE::Scene
