// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumBoneModifierInfluenceUVE = 0.0F;
inline constexpr float kMaximumBoneModifierInfluenceUVE = 1.0F;

/// The shared state of BoneModifier3D, the abstract base of every object that adjusts a skeleton's
/// pose after animation has posed it - look-at, IK, spring bones. No object is a BoneModifier3D on
/// its own; its kinds carry this component and add their own.
struct BoneModifierComponentUVE final {
    /// Off skips the modifier entirely, as if it were not there.
    bool active = true;
    /// How much of the modifier's result is blended over the incoming pose: 0 leaves the pose
    /// untouched, 1 applies it fully.
    float influence = 1.0F;

    [[nodiscard]] bool operator==(const BoneModifierComponentUVE&) const = default;
};

/// Influence must be finite and within [0, 1].
[[nodiscard]] bool IsBoneModifierComponentValidUVE(const BoneModifierComponentUVE& component) noexcept;

/// Clamps `influence` into `[0, 1]`, returning `0.0F` when non-finite.
[[nodiscard]] float ClampBoneModifierInfluenceUVE(float influence) noexcept;

/// Returns a copy of `component` with `influence` clamped into `[0, 1]`.
[[nodiscard]] BoneModifierComponentUVE SanitizeBoneModifierComponentUVE(
    const BoneModifierComponentUVE& component) noexcept;

/// True when `component` is active and has a finite, strictly positive influence.
[[nodiscard]] bool IsBoneModifierEffectiveUVE(const BoneModifierComponentUVE& component) noexcept;

/// Resolves the effective blend weight in `[0, 1]` (`0` when null or inactive).
[[nodiscard]] float ResolveEffectiveBoneModifierWeightUVE(
    const BoneModifierComponentUVE* component) noexcept;

/// Linearly blends `basePosition` toward `targetPosition` by `component`'s effective weight.
[[nodiscard]] Math::Vector3UVE BlendBoneModifierPositionUVE(
    const BoneModifierComponentUVE& component, const Math::Vector3UVE& basePosition,
    const Math::Vector3UVE& targetPosition) noexcept;

/// Slerps `baseRotation` toward `targetRotation` by `component`'s effective weight.
[[nodiscard]] Math::QuaternionUVE BlendBoneModifierRotationUVE(
    const BoneModifierComponentUVE& component, const Math::QuaternionUVE& baseRotation,
    const Math::QuaternionUVE& targetRotation) noexcept;

} // namespace UVE::Scene
