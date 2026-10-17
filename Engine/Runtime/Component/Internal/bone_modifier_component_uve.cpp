// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/bone_modifier_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsBoneModifierComponentValidUVE(const BoneModifierComponentUVE& component) noexcept {
    return std::isfinite(component.influence) &&
           component.influence >= kMinimumBoneModifierInfluenceUVE &&
           component.influence <= kMaximumBoneModifierInfluenceUVE;
}

float ClampBoneModifierInfluenceUVE(const float influence) noexcept {
    if (!std::isfinite(influence)) {
        return kMinimumBoneModifierInfluenceUVE;
    }
    return std::clamp(influence, kMinimumBoneModifierInfluenceUVE, kMaximumBoneModifierInfluenceUVE);
}

BoneModifierComponentUVE SanitizeBoneModifierComponentUVE(
    const BoneModifierComponentUVE& component) noexcept {
    return BoneModifierComponentUVE{
        .active = component.active,
        .influence = ClampBoneModifierInfluenceUVE(component.influence),
    };
}

bool IsBoneModifierEffectiveUVE(const BoneModifierComponentUVE& component) noexcept {
    return component.active && std::isfinite(component.influence) && component.influence > 0.0F;
}

float ResolveEffectiveBoneModifierWeightUVE(
    const BoneModifierComponentUVE* const component) noexcept {
    if (component == nullptr || !component->active) {
        return 0.0F;
    }
    return ClampBoneModifierInfluenceUVE(component->influence);
}

Math::Vector3UVE BlendBoneModifierPositionUVE(const BoneModifierComponentUVE& component,
                                              const Math::Vector3UVE& basePosition,
                                              const Math::Vector3UVE& targetPosition) noexcept {
    const float weight = ResolveEffectiveBoneModifierWeightUVE(&component);
    if (weight <= 0.0F) {
        return basePosition;
    }
    if (weight >= 1.0F) {
        return targetPosition;
    }
    return basePosition + (targetPosition - basePosition) * weight;
}

Math::QuaternionUVE BlendBoneModifierRotationUVE(const BoneModifierComponentUVE& component,
                                                 const Math::QuaternionUVE& baseRotation,
                                                 const Math::QuaternionUVE& targetRotation) noexcept {
    const float weight = ResolveEffectiveBoneModifierWeightUVE(&component);
    if (weight <= 0.0F) {
        return baseRotation;
    }
    if (weight >= 1.0F) {
        return targetRotation;
    }
    Math::QuaternionUVE blended = baseRotation;
    return Math::TrySlerpUVE(baseRotation, targetRotation, weight, blended) ? blended
                                                                            : baseRotation;
}

} // namespace UVE::Scene
