// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "physics_object_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsPhysicsObjectDisableModeValid(const PhysicsObjectDisableMode mode) noexcept {
    switch (mode) {
        case PhysicsObjectDisableMode::Remove:
        case PhysicsObjectDisableMode::MakeStatic:
        case PhysicsObjectDisableMode::KeepActive:
            return true;
    }
    return false;
}

bool IsPhysicsObjectComponentValid(const PhysicsObjectComponent& component) noexcept {
    return IsPhysicsObjectDisableModeValid(component.disableMode) &&
           std::isfinite(component.collisionPriority) && component.collisionPriority >= 0.0F;
}

PhysicsObjectComponent SanitizePhysicsObjectComponent(
    const PhysicsObjectComponent& component) noexcept {
    const PhysicsObjectDisableMode safeMode =
        IsPhysicsObjectDisableModeValid(component.disableMode)
            ? component.disableMode
            : PhysicsObjectDisableMode::Remove;
    const float safePriority =
        (std::isfinite(component.collisionPriority) && component.collisionPriority >= 0.0F)
            ? component.collisionPriority
            : kDefaultPhysicsCollisionPriority;
    return PhysicsObjectComponent{
        .disableMode = safeMode,
        .collisionPriority = safePriority,
        .inputRayPickable = component.inputRayPickable,
        .inputCaptureOnDrag = component.inputCaptureOnDrag,
    };
}

bool ShouldKeepPhysicsBodyInWorldWhenDisabled(
    const PhysicsObjectComponent& component) noexcept {
    return component.disableMode == PhysicsObjectDisableMode::MakeStatic ||
           component.disableMode == PhysicsObjectDisableMode::KeepActive;
}

bool ShouldSimulatePhysicsBodyWhenDisabled(
    const PhysicsObjectComponent& component) noexcept {
    return component.disableMode == PhysicsObjectDisableMode::KeepActive;
}

bool CanPickPhysicsObjectWithRay(const PhysicsObjectComponent& component,
                                    const bool entityEnabled) noexcept {
    if (!component.inputRayPickable) {
        return false;
    }
    return entityEnabled || ShouldKeepPhysicsBodyInWorldWhenDisabled(component);
}

float ResolvePairDepenetrationShare(const PhysicsObjectComponent& self,
                                       const PhysicsObjectComponent& other) noexcept {
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
