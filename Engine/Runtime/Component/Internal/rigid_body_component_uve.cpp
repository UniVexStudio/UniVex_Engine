// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/rigid_body_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsRigidBodyComponentValidUVE(const RigidBodyComponentUVE& rigidBody) noexcept {
    return std::isfinite(rigidBody.mass) && rigidBody.mass >= 0.0F && std::isfinite(rigidBody.velocity.x) &&
           std::isfinite(rigidBody.velocity.y) && std::isfinite(rigidBody.velocity.z) &&
           std::isfinite(rigidBody.angularVelocity.x) && std::isfinite(rigidBody.angularVelocity.y) &&
           std::isfinite(rigidBody.angularVelocity.z) && std::isfinite(rigidBody.torque.x) &&
           std::isfinite(rigidBody.torque.y) && std::isfinite(rigidBody.torque.z) &&
           std::isfinite(rigidBody.inverseInertia.x) && rigidBody.inverseInertia.x >= 0.0F &&
           std::isfinite(rigidBody.inverseInertia.y) && rigidBody.inverseInertia.y >= 0.0F &&
           std::isfinite(rigidBody.inverseInertia.z) && rigidBody.inverseInertia.z >= 0.0F &&
           std::isfinite(rigidBody.drag) && rigidBody.drag >= 0.0F && std::isfinite(rigidBody.gravityScale) &&
           rigidBody.gravityScale >= 0.0F;
}

} // namespace UVE::Scene
