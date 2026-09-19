// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cmath>
#include <cstdint>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// The authored, parent-relative (local) position/rotation/scale of a scene-graph entity —
/// exactly what a Node3D "requires" per the master spec (Position, Rotation, Scale). Attach via
/// SceneGraphUVE::AttachTransformUVE() rather than directly, so the paired
/// WorldTransformComponentUVE/HierarchyComponentUVE are never forgotten. Setting this directly
/// via IEntityManagerUVE::GetComponentUVE() does NOT mark the entity dirty — use
/// SceneGraphUVE::SetLocalTransformUVE() to change it so world-transform propagation stays
/// correct.
/// How a transform's rotation is authored, and therefore which representation is the source of
/// truth for it.
enum class RotationEditModeUVE : std::uint8_t {
    /// Euler angles are authored; `localRotation` is derived from them. The default, because it is
    /// what almost everyone types into an Inspector.
    Euler = 0,
    /// The quaternion is authored directly - by a gizmo, by physics, by a script. Euler is then a
    /// read-only view of it.
    Quaternion,
};

struct TransformComponentUVE final {
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    Math::Vector3UVE localScale{1.0F, 1.0F, 1.0F};

    /// The authored Euler angles, in radians, when `rotationEditMode` is Euler.
    ///
    /// WHY THIS IS STORED RATHER THAN DERIVED. The obvious design keeps only the quaternion and
    /// re-extracts Euler angles whenever the Inspector draws. That is what most engines do, and it
    /// loses authored data three separate ways - all three measured on this engine's own maths
    /// before this field existed:
    ///
    ///   typed (90, 45, 30) deg   read back as (60, 90, 45)   - gimbal lock
    ///   typed 370 deg            read back as 10             - the extra turn is gone
    ///   1000 redraws, no edits   drifted by up to 54 deg     - round-trip error accumulating
    ///
    /// The third is the worst: the rotation changes while the author is only LOOKING at it. None
    /// of it is a precision bug to be tuned away - a quaternion genuinely cannot represent "370
    /// degrees" or "which of the infinitely many angle triples at this pole did you mean". The
    /// information has to be kept, so it is kept.
    ///
    /// Ignored entirely when `rotationEditMode` is Quaternion, where the quaternion is the truth
    /// and Euler is only ever a derived view.
    Math::Vector3UVE localEulerRadians{};

    /// Which axis order `localEulerRadians` is applied in. XYZ is the default and matches every
    /// rotation authored before this field existed.
    ///
    /// Six orders exist because every Euler convention has a pose where a degree of freedom
    /// collapses, but the pose DIFFERS per order - an author whose rig lives at XYZ's singularity
    /// can pick one whose singularity is somewhere they never go.
    Math::EulerOrderUVE eulerOrder = Math::EulerOrderUVE::XYZ;

    /// Which representation is authoritative. Switching to Quaternion is what a gizmo drag or a
    /// physics write does: it says "the quaternion is now the truth, stop replaying stale angles".
    RotationEditModeUVE rotationEditMode = RotationEditModeUVE::Euler;
};

/// Rebuilds `localRotation` from the authored Euler angles, when those are authoritative.
///
/// A no-op in Quaternion mode - there, the quaternion IS the authored value and rebuilding it
/// from a derived Euler view would be the very round-trip this design exists to avoid.
///
/// Returns false, leaving the transform untouched, when the angles or the order are unusable. A
/// caller that ignores the result keeps the previous rotation, which is the safe outcome: a bad
/// edit should not be able to teleport an object.
[[nodiscard]] bool TrySyncRotationFromEulerUVE(TransformComponentUVE& transform) noexcept;

/// The Euler angles to SHOW for this transform.
///
/// In Euler mode this returns exactly what was authored - no extraction, so nothing drifts and
/// nothing is normalised away. In Quaternion mode it extracts from the quaternion, which is
/// honest: there are no authored angles to show, and the caller is looking at a derived view.
///
/// Returns false only when a Quaternion-mode rotation cannot be decomposed at all.
[[nodiscard]] bool TryGetDisplayEulerUVE(const TransformComponentUVE& transform,
                                         Math::Vector3UVE& outRadians) noexcept;

/// Validates authored local transforms before scene persistence or graph propagation. Position and
/// scale must be finite; rotation must be finite and already normalized so composition cannot
/// silently introduce non-rotational scale or non-finite world state.
[[nodiscard]] bool IsTransformComponentValidUVE(const TransformComponentUVE& transform) noexcept;

} // namespace UVE::Scene
