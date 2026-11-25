// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "bone_modifier_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {

bool IsBoneModifierComponentValid(const BoneModifierComponent& component) noexcept {
    return std::isfinite(component.influence) &&
           component.influence >= kMinimumBoneModifierInfluence &&
           component.influence <= kMaximumBoneModifierInfluence;
}

float ClampBoneModifierInfluence(const float influence) noexcept {
    if (!std::isfinite(influence)) {
        return kMinimumBoneModifierInfluence;
    }
    return std::clamp(influence, kMinimumBoneModifierInfluence, kMaximumBoneModifierInfluence);
}

BoneModifierComponent SanitizeBoneModifierComponent(
    const BoneModifierComponent& component) noexcept {
    return BoneModifierComponent{
        .active = component.active,
        .influence = ClampBoneModifierInfluence(component.influence),
    };
}

bool IsBoneModifierEffective(const BoneModifierComponent& component) noexcept {
    return component.active && std::isfinite(component.influence) && component.influence > 0.0F;
}

float ResolveEffectiveBoneModifierWeight(
    const BoneModifierComponent* const component) noexcept {
    if (component == nullptr || !component->active) {
        return 0.0F;
    }
    return ClampBoneModifierInfluence(component->influence);
}

Math::Vector3 BlendBoneModifierPosition(const BoneModifierComponent& component,
                                              const Math::Vector3& basePosition,
                                              const Math::Vector3& targetPosition) noexcept {
    const float weight = ResolveEffectiveBoneModifierWeight(&component);
    if (weight <= 0.0F) {
        return basePosition;
    }
    if (weight >= 1.0F) {
        return targetPosition;
    }
    return basePosition + (targetPosition - basePosition) * weight;
}

Math::Quaternion BlendBoneModifierRotation(const BoneModifierComponent& component,
                                                 const Math::Quaternion& baseRotation,
                                                 const Math::Quaternion& targetRotation) noexcept {
    const float weight = ResolveEffectiveBoneModifierWeight(&component);
    if (weight <= 0.0F) {
        return baseRotation;
    }
    if (weight >= 1.0F) {
        return targetRotation;
    }
    Math::Quaternion blended = baseRotation;
    return Math::TrySlerp(baseRotation, targetRotation, weight, blended) ? blended
                                                                            : baseRotation;
}

} // namespace UVE::Scene
