// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "two_bone_ik_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "scalar.h"
#include "abstract_animation_objects_3d.h"

namespace UVE::Scene {

namespace {

/// Lengths and directions closer than this are treated as absent. A bone of a millimetre and a
/// target on top of its own root are both degenerate chains: the circles the solve intersects stop
/// being circles, and the branch that divides by them would hand the pose an infinity.
inline constexpr float kDegenerateLength = 1.0e-6F;

/// The rotation that turns `from` into `to` along the shortest arc, leaving the twist about that arc
/// alone. Built from the half-way quaternion rather than from an axis and an angle, because
/// acos() loses most of its precision exactly where a bone usually is (a nearly straight limb).
[[nodiscard]] bool TryFromToRotation(const Math::Vector3& from, const Math::Vector3& to,
                                        Math::Quaternion& out) noexcept {
    const float dot = std::clamp(Math::Dot(from, to), -1.0F, 1.0F);
    if (dot >= 1.0F - kDegenerateLength) {
        out = Math::Quaternion{};
        return true;
    }
    if (dot <= -1.0F + kDegenerateLength) {
        // Opposite directions: the arc is a half turn about ANY perpendicular axis, so one is picked
        // deterministically - the smallest component of `from` crossed with that world axis - because
        // an arbitrary choice here would make a straightened-back limb flip between frames.
        const Math::Vector3 axis = std::abs(from.x) <= std::abs(from.y) && std::abs(from.x) <= std::abs(from.z)
                                          ? Math::Cross(from, Math::Vector3{1.0F, 0.0F, 0.0F})
                                      : std::abs(from.y) <= std::abs(from.z)
                                          ? Math::Cross(from, Math::Vector3{0.0F, 1.0F, 0.0F})
                                          : Math::Cross(from, Math::Vector3{0.0F, 0.0F, 1.0F});
        const float length = Math::Length(axis);
        if (!(length > kDegenerateLength)) {
            return false;
        }
        return Math::TryMakeAxisAngle(axis * (1.0F / length), Math::kPi, out);
    }
    const Math::Vector3 cross = Math::Cross(from, to);
    return Math::TryNormalize(Math::Quaternion{cross.x, cross.y, cross.z, 1.0F + dot}, out);
}

/// The part of `direction` that stands across `axis`, or zero when it has none. This is how a pole
/// is read: only its component perpendicular to the root-to-target line can say which way to bend.
[[nodiscard]] Math::Vector3 PerpendicularComponent(const Math::Vector3& direction,
                                                         const Math::Vector3& axis) noexcept {
    return direction - axis * Math::Dot(direction, axis);
}

} // namespace

bool IsTwoBoneIK3DObjectComponentValid(const TwoBoneIK3DComponent& value) noexcept {
    return IsBounded3DObjectString(value.rootBoneName) && IsBounded3DObjectString(value.middleBoneName) &&
           IsBounded3DObjectString(value.endBoneName) && IsFinite3DObjectVector(value.targetPosition) &&
           IsFinite3DObjectVector(value.poleDirection);
}

std::optional<TwoBoneIKSolution> SolveTwoBoneIK(const TwoBoneIKChain& chain,
                                                      const Math::Vector3& target,
                                                      const Math::Vector3& poleDirection) noexcept {
    if (!IsFinite3DObjectVector(chain.root) || !IsFinite3DObjectVector(chain.middle) ||
        !IsFinite3DObjectVector(chain.end) || !IsFinite3DObjectVector(target) ||
        !IsFinite3DObjectVector(poleDirection) || !IsFinite3DObjectQuaternion(chain.parentRotation) ||
        !IsFinite3DObjectQuaternion(chain.rootRotation) || !IsFinite3DObjectQuaternion(chain.middleRotation)) {
        return std::nullopt;
    }

    const Math::Vector3 rootToMiddle = chain.middle - chain.root;
    const Math::Vector3 middleToEnd = chain.end - chain.middle;
    const float upperLength = Math::Length(rootToMiddle);
    const float lowerLength = Math::Length(middleToEnd);
    if (!(upperLength > kDegenerateLength) || !(lowerLength > kDegenerateLength)) {
        return std::nullopt;
    }

    const Math::Vector3 rootToTarget = target - chain.root;
    const float targetDistance = Math::Length(rootToTarget);
    if (!(targetDistance > kDegenerateLength)) {
        return std::nullopt;
    }
    const Math::Vector3 aim = rootToTarget * (1.0F / targetDistance);

    // Where the end can go: the chain reaches at most its own length out, and folds at most to the
    // difference of its two bones. Clamping to the reachable band is what turns an impossible target
    // into the pose an animator would key for it - straight and reaching, or folded shut - instead of
    // a limb stretched past its own joints.
    const float reach = upperLength + lowerLength;
    const float fold = std::abs(upperLength - lowerLength);
    const float spannedDistance = std::clamp(targetDistance, fold, reach);
    const Math::Vector3 placedEnd = chain.root + aim * spannedDistance;
    const bool reached = targetDistance <= reach + kDegenerateLength &&
                         targetDistance >= fold - kDegenerateLength;

    // The bend plane. An authored pole wins; with none, the plane the pose already put the joint in
    // is kept, and when the limb comes in perfectly straight (no plane to keep) the aim's own
    // perpendicular is chosen - all three are deterministic, which is what stops a chain from
    // flickering between the mirror-image poses that both reach the same target.
    Math::Vector3 bend = PerpendicularComponent(poleDirection, aim);
    if (!(Math::Length(bend) > kDegenerateLength)) {
        bend = PerpendicularComponent(rootToMiddle, aim);
        if (!(Math::Length(bend) > kDegenerateLength)) {
            bend = PerpendicularComponent(Math::Cross(aim, Math::Vector3{0.0F, 0.0F, 1.0F}), aim);
            if (!(Math::Length(bend) > kDegenerateLength)) {
                bend = PerpendicularComponent(Math::Cross(aim, Math::Vector3{0.0F, 1.0F, 0.0F}), aim);
            }
        }
    }
    if (!(Math::Length(bend) > kDegenerateLength)) {
        return std::nullopt;
    }
    bend = Math::Normalize(bend);

    // The two circles meet here: the joint sits at the angle the law of cosines gives, measured off
    // the root-to-target line and swung toward the pole.
    const float along = (upperLength * upperLength + spannedDistance * spannedDistance - lowerLength * lowerLength) /
                        (2.0F * upperLength * spannedDistance);
    const float across = std::sqrt(std::max(0.0F, 1.0F - along * along));
    const Math::Vector3 placedMiddle = chain.root + aim * (upperLength * along) + bend * (upperLength * across);

    const Math::Vector3 upperDirection = Math::Normalize(placedMiddle - chain.root);
    const Math::Vector3 lowerDirection = Math::Normalize(placedEnd - placedMiddle);
    Math::Quaternion upperSwing{};
    if (!TryFromToRotation(Math::Normalize(rootToMiddle), upperDirection, upperSwing)) {
        return std::nullopt;
    }
    // The middle bone inherits the swing the root took and then takes its own on top of that; solving
    // its direction from the unturned pose would double-count the parent's rotation.
    const Math::Vector3 inheritedLowerDirection =
        Math::Normalize(Math::RotateVector(upperSwing, middleToEnd));
    Math::Quaternion lowerSwing{};
    if (!TryFromToRotation(inheritedLowerDirection, lowerDirection, lowerSwing)) {
        return std::nullopt;
    }

    const Math::Quaternion rootWorld = Math::Multiply(upperSwing, chain.rootRotation);
    const Math::Quaternion middleWorld =
        Math::Multiply(lowerSwing, Math::Multiply(upperSwing, chain.middleRotation));
    Math::Quaternion inverseParent{};
    Math::Quaternion inverseRoot{};
    if (!Math::TryInverse(chain.parentRotation, inverseParent) || !Math::TryInverse(rootWorld, inverseRoot)) {
        return std::nullopt;
    }

    TwoBoneIKSolution solution{};
    if (!Math::TryNormalize(Math::Multiply(inverseParent, rootWorld), solution.rootLocalRotation) ||
        !Math::TryNormalize(Math::Multiply(inverseRoot, middleWorld), solution.middleLocalRotation)) {
        return std::nullopt;
    }
    solution.endPosition = placedEnd;
    solution.endToTargetDistanceMetres = Math::Length(target - placedEnd);
    solution.reached = reached;
    return solution;
}

bool TryBlendTwoBoneIKRotation(const Math::Quaternion& posed, const Math::Quaternion& solved,
                                  const float influence, Math::Quaternion& out) noexcept {
    if (!IsFinite3DObjectQuaternion(posed) || !IsFinite3DObjectQuaternion(solved)) {
        return false;
    }
    if (influence <= 0.0F) {
        out = posed;
        return true;
    }
    if (influence >= 1.0F) {
        return Math::TryNormalize(solved, out);
    }
    return Math::TrySlerp(posed, solved, influence, out);
}

void ApplyTwoBoneIK3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                         const TwoBoneIK3DObjectDefinition& value) {
    static_cast<void>(value);
    ApplyBoneModifier3DBase(entityManager, entity, TwoBoneIK3DObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<TwoBoneIK3DComponent>(entity)) {
        entityManager.AddComponent<TwoBoneIK3DComponent>(entity, TwoBoneIK3DComponent{});
    }
}

} // namespace UVE::Scene
