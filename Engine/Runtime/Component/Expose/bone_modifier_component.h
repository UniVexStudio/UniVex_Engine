// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "quaternion.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr float kMinimumBoneModifierInfluence = 0.0F;
inline constexpr float kMaximumBoneModifierInfluence = 1.0F;

/// The shared state of BoneModifier3D, the abstract base of every object that adjusts a skeleton's
/// pose after animation has posed it - look-at, IK, spring bones. No object is a BoneModifier3D on
/// its own; its kinds carry this component and add their own.
struct BoneModifierComponent final {
    /// Off skips the modifier entirely, as if it were not there.
    bool active = true;
    /// How much of the modifier's result is blended over the incoming pose: 0 leaves the pose
    /// untouched, 1 applies it fully.
    float influence = 1.0F;

    [[nodiscard]] bool operator==(const BoneModifierComponent&) const = default;
};

/// Influence must be finite and within [0, 1].
[[nodiscard]] bool IsBoneModifierComponentValid(const BoneModifierComponent& component) noexcept;

/// Clamps `influence` into `[0, 1]`, returning `0.0F` when non-finite.
[[nodiscard]] float ClampBoneModifierInfluence(float influence) noexcept;

/// Returns a copy of `component` with `influence` clamped into `[0, 1]`.
[[nodiscard]] BoneModifierComponent SanitizeBoneModifierComponent(
    const BoneModifierComponent& component) noexcept;

/// True when `component` is active and has a finite, strictly positive influence.
[[nodiscard]] bool IsBoneModifierEffective(const BoneModifierComponent& component) noexcept;

/// Resolves the effective blend weight in `[0, 1]` (`0` when null or inactive).
[[nodiscard]] float ResolveEffectiveBoneModifierWeight(
    const BoneModifierComponent* component) noexcept;

/// Linearly blends `basePosition` toward `targetPosition` by `component`'s effective weight.
[[nodiscard]] Math::Vector3 BlendBoneModifierPosition(
    const BoneModifierComponent& component, const Math::Vector3& basePosition,
    const Math::Vector3& targetPosition) noexcept;

/// Slerps `baseRotation` toward `targetRotation` by `component`'s effective weight.
[[nodiscard]] Math::Quaternion BlendBoneModifierRotation(
    const BoneModifierComponent& component, const Math::Quaternion& baseRotation,
    const Math::Quaternion& targetRotation) noexcept;

} // namespace UVE::Scene
