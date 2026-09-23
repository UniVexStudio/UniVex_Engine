// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/physics_object_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsPhysicsObjectComponentValidUVE(const PhysicsObjectComponentUVE& component) noexcept {
    return component.disableMode <= PhysicsObjectDisableModeUVE::KeepActive &&
           std::isfinite(component.collisionPriority) && component.collisionPriority >= 0.0F;
}

} // namespace UVE::Scene
