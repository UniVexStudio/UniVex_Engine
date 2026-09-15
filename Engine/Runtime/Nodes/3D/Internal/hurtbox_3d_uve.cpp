// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/hurtbox_3d_uve.h"

namespace UVE::Scene {

bool IsHurtbox3DNodeComponentValidUVE(const Hurtbox3DNodeComponentUVE& value) noexcept {
    const Hitbox3DNodeComponentUVE equivalent{value.halfExtents, value.collisionLayer, value.collisionMask,
                                               value.damageChannel, value.enabled};
    return IsHitbox3DNodeComponentValidUVE(equivalent);
}

} // namespace UVE::Scene
