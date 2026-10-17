// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/physics_object_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsPhysicsObjectDisableModeValidUVE(const PhysicsObjectDisableModeUVE mode) noexcept {
    switch (mode) {
        case PhysicsObjectDisableModeUVE::Remove:
        case PhysicsObjectDisableModeUVE::MakeStatic:
        case PhysicsObjectDisableModeUVE::KeepActive:
            return true;
    }
    return false;
}

bool IsPhysicsObjectComponentValidUVE(const PhysicsObjectComponentUVE& component) noexcept {
    return IsPhysicsObjectDisableModeValidUVE(component.disableMode) &&
           std::isfinite(component.collisionPriority) && component.collisionPriority >= 0.0F;
}

PhysicsObjectComponentUVE SanitizePhysicsObjectComponentUVE(
    const PhysicsObjectComponentUVE& component) noexcept {
    const PhysicsObjectDisableModeUVE safeMode =
        IsPhysicsObjectDisableModeValidUVE(component.disableMode)
            ? component.disableMode
            : PhysicsObjectDisableModeUVE::Remove;
    const float safePriority =
        (std::isfinite(component.collisionPriority) && component.collisionPriority >= 0.0F)
            ? component.collisionPriority
            : kDefaultPhysicsCollisionPriorityUVE;
    return PhysicsObjectComponentUVE{
        .disableMode = safeMode,
        .collisionPriority = safePriority,
        .inputRayPickable = component.inputRayPickable,
        .inputCaptureOnDrag = component.inputCaptureOnDrag,
    };
}

bool ShouldKeepPhysicsBodyInWorldWhenDisabledUVE(
    const PhysicsObjectComponentUVE& component) noexcept {
    return component.disableMode == PhysicsObjectDisableModeUVE::MakeStatic ||
           component.disableMode == PhysicsObjectDisableModeUVE::KeepActive;
}

bool ShouldSimulatePhysicsBodyWhenDisabledUVE(
    const PhysicsObjectComponentUVE& component) noexcept {
    return component.disableMode == PhysicsObjectDisableModeUVE::KeepActive;
}

bool CanPickPhysicsObjectWithRayUVE(const PhysicsObjectComponentUVE& component,
                                    const bool entityEnabled) noexcept {
    if (!component.inputRayPickable) {
        return false;
    }
    return entityEnabled || ShouldKeepPhysicsBodyInWorldWhenDisabledUVE(component);
}

float ResolvePairDepenetrationShareUVE(const PhysicsObjectComponentUVE& self,
                                       const PhysicsObjectComponentUVE& other) noexcept {
    const float selfPriority =
        (std::isfinite(self.collisionPriority) && self.collisionPriority >= 0.0F)
            ? self.collisionPriority
            : 0.0F;
    const float otherPriority =
        (std::isfinite(other.collisionPriority) && other.collisionPriority >= 0.0F)
            ? other.collisionPriority
            : 0.0F;
    const float total = selfPriority + otherPriority;
    if (total <= 1e-6F) {
        return 0.5F;
    }
    // Higher priority yields less -> absorbs `otherPriority / (selfPriority + otherPriority)`.
    return std::clamp(otherPriority / total, 0.0F, 1.0F);
}

} // namespace UVE::Scene
