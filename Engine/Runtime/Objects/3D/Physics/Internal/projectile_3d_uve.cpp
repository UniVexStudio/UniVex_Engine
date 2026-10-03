// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/projectile_3d_uve.h"

namespace UVE::Scene {

bool IsProjectile3DObjectComponentValidUVE(const Projectile3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.velocity) && IsFinite3DObjectVectorUVE(value.acceleration) &&
           std::isfinite(value.radius) && value.radius > 0.0F && std::isfinite(value.maxLifetime) &&
           value.maxLifetime > 0.0F && std::isfinite(value.remainingLifetime) && value.remainingLifetime >= 0.0F &&
           value.remainingLifetime <= value.maxLifetime;
}

} // namespace UVE::Scene
