// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/rigid_3d_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsRigid3DComponentValidUVE(const Rigid3DComponentUVE& rigidBody) noexcept {
    return std::isfinite(rigidBody.mass) && rigidBody.mass >= 0.0F && std::isfinite(rigidBody.velocity.x) &&
           std::isfinite(rigidBody.velocity.y) && std::isfinite(rigidBody.velocity.z) &&
           std::isfinite(rigidBody.force.x) && std::isfinite(rigidBody.force.y) &&
           std::isfinite(rigidBody.force.z) && std::isfinite(rigidBody.angularVelocity.x) && std::isfinite(rigidBody.angularVelocity.y) &&
           std::isfinite(rigidBody.angularVelocity.z) && std::isfinite(rigidBody.torque.x) &&
           std::isfinite(rigidBody.torque.y) && std::isfinite(rigidBody.torque.z) &&
           std::isfinite(rigidBody.inverseInertia.x) && rigidBody.inverseInertia.x >= 0.0F &&
           std::isfinite(rigidBody.inverseInertia.y) && rigidBody.inverseInertia.y >= 0.0F &&
           std::isfinite(rigidBody.inverseInertia.z) && rigidBody.inverseInertia.z >= 0.0F &&
           std::isfinite(rigidBody.drag) && rigidBody.drag >= 0.0F && std::isfinite(rigidBody.gravityScale) &&
           rigidBody.gravityScale >= 0.0F;
}

Rigid3DComponentUVE SanitizeRigid3DComponentUVE(const Rigid3DComponentUVE& rigidBody) noexcept {
    Rigid3DComponentUVE out = rigidBody;
    out.mass = (std::isfinite(out.mass) && out.mass >= 0.0F) ? out.mass : 1.0F;
    if (!Math::IsFiniteUVE(out.velocity)) {
        out.velocity = Math::Vector3UVE{};
    }
    if (!Math::IsFiniteUVE(out.force)) {
        out.force = Math::Vector3UVE{};
    }
    if (!Math::IsFiniteUVE(out.angularVelocity)) {
        out.angularVelocity = Math::Vector3UVE{};
    }
    if (!Math::IsFiniteUVE(out.torque)) {
        out.torque = Math::Vector3UVE{};
    }
    out.inverseInertia = Math::Vector3UVE{
        (std::isfinite(out.inverseInertia.x) && out.inverseInertia.x >= 0.0F)
            ? out.inverseInertia.x
            : 0.0F,
        (std::isfinite(out.inverseInertia.y) && out.inverseInertia.y >= 0.0F)
            ? out.inverseInertia.y
            : 0.0F,
        (std::isfinite(out.inverseInertia.z) && out.inverseInertia.z >= 0.0F)
            ? out.inverseInertia.z
            : 0.0F,
    };
    out.drag = (std::isfinite(out.drag) && out.drag >= 0.0F) ? out.drag : 0.0F;
    out.gravityScale =
        (std::isfinite(out.gravityScale) && out.gravityScale >= 0.0F) ? out.gravityScale : 1.0F;
    return out;
}

float ResolveRigid3DInverseMassUVE(const Rigid3DComponentUVE& rigidBody) noexcept {
    if (rigidBody.isKinematic || !std::isfinite(rigidBody.mass) || rigidBody.mass <= 0.0F) {
        return 0.0F;
    }
    return 1.0F / rigidBody.mass;
}

void ApplyRigid3DLinearImpulseUVE(Rigid3DComponentUVE& rigidBody,
                                  const Math::Vector3UVE& impulseNs) noexcept {
    const float invMass = ResolveRigid3DInverseMassUVE(rigidBody);
    if (invMass <= 0.0F || !Math::IsFiniteUVE(impulseNs)) {
        return;
    }
    rigidBody.velocity = rigidBody.velocity + impulseNs * invMass;
}

void ClearRigid3DForcesAndTorquesUVE(Rigid3DComponentUVE& rigidBody) noexcept {
    rigidBody.force = Math::Vector3UVE{};
    rigidBody.torque = Math::Vector3UVE{};
}

} // namespace UVE::Scene
