// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/two_bone_ik_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_animation_objects_3d_uve.h"

namespace UVE::Scene {

namespace {

/// Lengths and directions closer than this are treated as absent. A bone of a millimetre and a
/// target on top of its own root are both degenerate chains: the circles the solve intersects stop
/// being circles, and the branch that divides by them would hand the pose an infinity.
inline constexpr float kDegenerateLengthUVE = 1.0e-6F;

/// The rotation that turns `from` into `to` along the shortest arc, leaving the twist about that arc
/// alone. Built from the half-way quaternion rather than from an axis and an angle, because
/// acos() loses most of its precision exactly where a bone usually is (a nearly straight limb).
[[nodiscard]] bool TryFromToRotationUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                                        Math::QuaternionUVE& out) noexcept {
    const float dot = std::clamp(Math::DotUVE(from, to), -1.0F, 1.0F);
    if (dot >= 1.0F - kDegenerateLengthUVE) {
        out = Math::QuaternionUVE{};
        return true;
    }
    if (dot <= -1.0F + kDegenerateLengthUVE) {
        // Opposite directions: the arc is a half turn about ANY perpendicular axis, so one is picked
        // deterministically - the smallest component of `from` crossed with that world axis - because
        // an arbitrary choice here would make a straightened-back limb flip between frames.
        const Math::Vector3UVE axis = std::abs(from.x) <= std::abs(from.y) && std::abs(from.x) <= std::abs(from.z)
                                          ? Math::CrossUVE(from, Math::Vector3UVE{1.0F, 0.0F, 0.0F})
                                      : std::abs(from.y) <= std::abs(from.z)
                                          ? Math::CrossUVE(from, Math::Vector3UVE{0.0F, 1.0F, 0.0F})
                                          : Math::CrossUVE(from, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
        const float length = Math::LengthUVE(axis);
        if (!(length > kDegenerateLengthUVE)) {
            return false;
        }
        return Math::TryMakeAxisAngleUVE(axis * (1.0F / length), 3.14159265F, out);
    }
    const Math::Vector3UVE cross = Math::CrossUVE(from, to);
    return Math::TryNormalizeUVE(Math::QuaternionUVE{cross.x, cross.y, cross.z, 1.0F + dot}, out);
}

/// The part of `direction` that stands across `axis`, or zero when it has none. This is how a pole
/// is read: only its component perpendicular to the root-to-target line can say which way to bend.
[[nodiscard]] Math::Vector3UVE PerpendicularComponentUVE(const Math::Vector3UVE& direction,
                                                         const Math::Vector3UVE& axis) noexcept {
    return direction - axis * Math::DotUVE(direction, axis);
}

} // namespace

bool IsTwoBoneIK3DObjectComponentValidUVE(const TwoBoneIK3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.rootBoneName) && IsBounded3DObjectStringUVE(value.middleBoneName) &&
           IsBounded3DObjectStringUVE(value.endBoneName) && IsFinite3DObjectVectorUVE(value.targetPosition) &&
           IsFinite3DObjectVectorUVE(value.poleDirection);
}

std::optional<TwoBoneIKSolutionUVE> SolveTwoBoneIKUVE(const TwoBoneIKChainUVE& chain,
                                                      const Math::Vector3UVE& target,
                                                      const Math::Vector3UVE& poleDirection) noexcept {
    if (!IsFinite3DObjectVectorUVE(chain.root) || !IsFinite3DObjectVectorUVE(chain.middle) ||
        !IsFinite3DObjectVectorUVE(chain.end) || !IsFinite3DObjectVectorUVE(target) ||
        !IsFinite3DObjectVectorUVE(poleDirection) || !IsFinite3DObjectQuaternionUVE(chain.parentRotation) ||
        !IsFinite3DObjectQuaternionUVE(chain.rootRotation) || !IsFinite3DObjectQuaternionUVE(chain.middleRotation)) {
        return std::nullopt;
    }

    const Math::Vector3UVE rootToMiddle = chain.middle - chain.root;
    const Math::Vector3UVE middleToEnd = chain.end - chain.middle;
    const float upperLength = Math::LengthUVE(rootToMiddle);
    const float lowerLength = Math::LengthUVE(middleToEnd);
    if (!(upperLength > kDegenerateLengthUVE) || !(lowerLength > kDegenerateLengthUVE)) {
        return std::nullopt;
    }

    const Math::Vector3UVE rootToTarget = target - chain.root;
    const float targetDistance = Math::LengthUVE(rootToTarget);
    if (!(targetDistance > kDegenerateLengthUVE)) {
        return std::nullopt;
    }
    const Math::Vector3UVE aim = rootToTarget * (1.0F / targetDistance);

    // Where the end can go: the chain reaches at most its own length out, and folds at most to the
    // difference of its two bones. Clamping to the reachable band is what turns an impossible target
    // into the pose an animator would key for it - straight and reaching, or folded shut - instead of
    // a limb stretched past its own joints.
    const float reach = upperLength + lowerLength;
    const float fold = std::abs(upperLength - lowerLength);
    const float spannedDistance = std::clamp(targetDistance, fold, reach);
    const Math::Vector3UVE placedEnd = chain.root + aim * spannedDistance;
    const bool reached = targetDistance <= reach + kDegenerateLengthUVE &&
                         targetDistance >= fold - kDegenerateLengthUVE;

    // The bend plane. An authored pole wins; with none, the plane the pose already put the joint in
    // is kept, and when the limb comes in perfectly straight (no plane to keep) the aim's own
    // perpendicular is chosen - all three are deterministic, which is what stops a chain from
    // flickering between the mirror-image poses that both reach the same target.
    Math::Vector3UVE bend = PerpendicularComponentUVE(poleDirection, aim);
    if (!(Math::LengthUVE(bend) > kDegenerateLengthUVE)) {
        bend = PerpendicularComponentUVE(rootToMiddle, aim);
        if (!(Math::LengthUVE(bend) > kDegenerateLengthUVE)) {
            bend = PerpendicularComponentUVE(Math::CrossUVE(aim, Math::Vector3UVE{0.0F, 0.0F, 1.0F}), aim);
            if (!(Math::LengthUVE(bend) > kDegenerateLengthUVE)) {
                bend = PerpendicularComponentUVE(Math::CrossUVE(aim, Math::Vector3UVE{0.0F, 1.0F, 0.0F}), aim);
            }
        }
    }
    if (!(Math::LengthUVE(bend) > kDegenerateLengthUVE)) {
        return std::nullopt;
    }
    bend = Math::NormalizeUVE(bend);

    // The two circles meet here: the joint sits at the angle the law of cosines gives, measured off
    // the root-to-target line and swung toward the pole.
    const float along = (upperLength * upperLength + spannedDistance * spannedDistance - lowerLength * lowerLength) /
                        (2.0F * upperLength * spannedDistance);
    const float across = std::sqrt(std::max(0.0F, 1.0F - along * along));
    const Math::Vector3UVE placedMiddle = chain.root + aim * (upperLength * along) + bend * (upperLength * across);

    const Math::Vector3UVE upperDirection = Math::NormalizeUVE(placedMiddle - chain.root);
    const Math::Vector3UVE lowerDirection = Math::NormalizeUVE(placedEnd - placedMiddle);
    Math::QuaternionUVE upperSwing{};
    if (!TryFromToRotationUVE(Math::NormalizeUVE(rootToMiddle), upperDirection, upperSwing)) {
        return std::nullopt;
    }
    // The middle bone inherits the swing the root took and then takes its own on top of that; solving
    // its direction from the unturned pose would double-count the parent's rotation.
    const Math::Vector3UVE inheritedLowerDirection =
        Math::NormalizeUVE(Math::RotateVectorUVE(upperSwing, middleToEnd));
    Math::QuaternionUVE lowerSwing{};
    if (!TryFromToRotationUVE(inheritedLowerDirection, lowerDirection, lowerSwing)) {
        return std::nullopt;
    }

    const Math::QuaternionUVE rootWorld = Math::MultiplyUVE(upperSwing, chain.rootRotation);
    const Math::QuaternionUVE middleWorld =
        Math::MultiplyUVE(lowerSwing, Math::MultiplyUVE(upperSwing, chain.middleRotation));
    Math::QuaternionUVE inverseParent{};
    Math::QuaternionUVE inverseRoot{};
    if (!Math::TryInverseUVE(chain.parentRotation, inverseParent) || !Math::TryInverseUVE(rootWorld, inverseRoot)) {
        return std::nullopt;
    }

    TwoBoneIKSolutionUVE solution{};
    if (!Math::TryNormalizeUVE(Math::MultiplyUVE(inverseParent, rootWorld), solution.rootLocalRotation) ||
        !Math::TryNormalizeUVE(Math::MultiplyUVE(inverseRoot, middleWorld), solution.middleLocalRotation)) {
        return std::nullopt;
    }
    solution.endPosition = placedEnd;
    solution.endToTargetDistanceMetres = Math::LengthUVE(target - placedEnd);
    solution.reached = reached;
    return solution;
}

bool TryBlendTwoBoneIKRotationUVE(const Math::QuaternionUVE& posed, const Math::QuaternionUVE& solved,
                                  const float influence, Math::QuaternionUVE& out) noexcept {
    if (!IsFinite3DObjectQuaternionUVE(posed) || !IsFinite3DObjectQuaternionUVE(solved)) {
        return false;
    }
    if (influence <= 0.0F) {
        out = posed;
        return true;
    }
    if (influence >= 1.0F) {
        return Math::TryNormalizeUVE(solved, out);
    }
    return Math::TrySlerpUVE(posed, solved, influence, out);
}

void ApplyTwoBoneIK3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const TwoBoneIK3DObjectDefinitionUVE& value) {
    static_cast<void>(value);
    ApplyBoneModifier3DBaseUVE(entityManager, entity, TwoBoneIK3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<TwoBoneIK3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<TwoBoneIK3DComponentUVE>(entity, TwoBoneIK3DComponentUVE{});
    }
}

} // namespace UVE::Scene
