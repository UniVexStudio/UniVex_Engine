// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/projectile_3d_uve.h"

namespace UVE::Scene {

bool IsProjectile3DNodeComponentValidUVE(const Projectile3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.velocity) && IsFinite3DNodeVectorUVE(value.acceleration) &&
           std::isfinite(value.radius) && value.radius > 0.0F && std::isfinite(value.maxLifetime) &&
           value.maxLifetime > 0.0F && std::isfinite(value.remainingLifetime) && value.remainingLifetime >= 0.0F &&
           value.remainingLifetime <= value.maxLifetime;
}

} // namespace UVE::Scene
