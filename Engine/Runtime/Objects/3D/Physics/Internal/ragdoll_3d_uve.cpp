// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/ragdoll_3d_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/physics/detail/collider_world_aabb_cache_uve.h"

namespace UVE::Scene {
namespace {

constexpr float kPiUVE = 3.14159265358979323846F;
constexpr float kDirectionEpsilonUVE = 1.0e-6F;
constexpr float kMinimumBoneLengthUVE = 0.02F;
constexpr std::size_t kMaximumRagdollEntitiesPerUpdateUVE = 64U;

/// The humanoid table. Ordered most-specific-first, because matching is a substring test and
/// `upperarm` must win over `arm` - and there is no `arm` row at all, so a rig whose upper arm is
/// simply "arm_l" falls through to the spine row rather than silently becoming a 6 kg limb.
///
/// Masses sum to about 75.5 kg on a rig with three spine bones: pelvis 12, spine 3x6, head 5, neck
/// 1.5, clavicles 2x1.5, upper arms 2x2.5, forearms 2x1.5, hands 2x0.5, thighs 2x9, shins 2x3.5,
/// feet 2x1. The absolute number matters far less than the ratios - a solver is sensitive to the
/// ratio between adjacent bodies, not to whether a character weighs 70 or 80 kg.
///
/// Damping rises toward the extremities, which is Unreal's own advice for ragdoll stability: a hand
/// that keeps spinning after the arm has stopped is the classic "floppy" tell.
constexpr std::array<HumanoidRagdollProfileEntryUVE, 18U> kHumanoidProfileUVE{{
    {"lowerarm", 1.5F, 0.045F, 0.10F, 0.05F, 1.0F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"lower_arm", 1.5F, 0.045F, 0.10F, 0.05F, 1.0F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"forearm", 1.5F, 0.045F, 0.10F, 0.05F, 1.0F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"upperarm", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"upper_arm", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"clavicle", 1.5F, 0.05F, 0.08F, 0.05F, 0.6F, 20.0F, 20.0F, 30.0F, false, -30.0F, 30.0F},
    {"shoulder", 1.5F, 0.05F, 0.08F, 0.05F, 0.6F, 20.0F, 20.0F, 30.0F, false, -30.0F, 30.0F},
    {"hand", 0.5F, 0.035F, 0.04F, 0.10F, 1.5F, 60.0F, 60.0F, 40.0F, false, -40.0F, 40.0F},
    {"thigh", 9.0F, 0.08F, 0.16F, 0.05F, 0.5F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"upperleg", 9.0F, 0.08F, 0.16F, 0.05F, 0.5F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"upleg", 9.0F, 0.08F, 0.16F, 0.05F, 0.5F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"calf", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"shin", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"lowerleg", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"foot", 1.0F, 0.04F, 0.06F, 0.10F, 1.5F, 5.0F, 5.0F, 40.0F, true, -40.0F, 40.0F},
    {"neck", 1.5F, 0.05F, 0.04F, 0.05F, 0.8F, 30.0F, 30.0F, 40.0F, false, -40.0F, 40.0F},
    {"head", 5.0F, 0.09F, 0.05F, 0.05F, 0.8F, 30.0F, 30.0F, 30.0F, false, -30.0F, 30.0F},
    {"spine", 6.0F, 0.10F, 0.08F, 0.05F, 0.4F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
}};

/// The root row is separate because `pelvis` and `hips` are equally specific and both should win
/// over `spine` on a rig that names its root "hip".
constexpr std::array<HumanoidRagdollProfileEntryUVE, 2U> kHumanoidRootProfileUVE{{
    {"pelvis", 12.0F, 0.12F, 0.08F, 0.05F, 0.4F, 45.0F, 45.0F, 35.0F, false, -35.0F, 35.0F},
    {"hips", 12.0F, 0.12F, 0.08F, 0.05F, 0.4F, 45.0F, 45.0F, 35.0F, false, -35.0F, 35.0F},
}};

[[nodiscard]] std::string ToLowerAsciiUVE(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const char character : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

[[nodiscard]] const HumanoidRagdollProfileEntryUVE* FindProfileEntryUVE(const std::string_view boneName) noexcept {
    const std::string lowered = ToLowerAsciiUVE(boneName);
    for (const HumanoidRagdollProfileEntryUVE& entry : kHumanoidRootProfileUVE) {
        if (lowered.find(entry.pattern) != std::string::npos) return &entry;
    }
    for (const HumanoidRagdollProfileEntryUVE& entry : kHumanoidProfileUVE) {
        if (lowered.find(entry.pattern) != std::string::npos) return &entry;
    }
    return nullptr;
}

/// Shortest-arc rotation taking the capsule's local +Y onto `axis`, which is what aligns a limb
/// capsule with the bone it stands for.
[[nodiscard]] Math::QuaternionUVE MakeCapsuleRotationFromAxisUVE(const Math::Vector3UVE& axis) noexcept {
    const float lengthSquared = Math::LengthSquaredUVE(axis);
    if (!std::isfinite(lengthSquared) || !(lengthSquared > kDirectionEpsilonUVE)) return Math::QuaternionUVE{};
    const Math::Vector3UVE normalised = axis * (1.0F / std::sqrt(lengthSquared));
    const Math::Vector3UVE from{0.0F, 1.0F, 0.0F};
    const float cosine = Math::DotUVE(from, normalised);
    if (cosine >= 1.0F - 1.0e-6F) return Math::QuaternionUVE{};

    Math::Vector3UVE rotationAxis = Math::CrossUVE(from, normalised);
    const float sine = std::sqrt(std::max(0.0F, 1.0F - cosine * cosine));
    const float rotationLengthSquared = Math::LengthSquaredUVE(rotationAxis);
    if (!(rotationLengthSquared > kDirectionEpsilonUVE)) {
        rotationAxis = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    } else {
        rotationAxis = rotationAxis * (1.0F / std::sqrt(rotationLengthSquared));
    }
    Math::QuaternionUVE rotation;
    if (!Math::TryMakeAxisAngleUVE(rotationAxis, std::atan2(sine, cosine), rotation)) {
        return Math::QuaternionUVE{};
    }
    return rotation;
}

/// Depth of every bone in the hierarchy, capped so a malformed skeleton (a cycle, or a parent index
/// pointing forward) cannot hang the walk.
[[nodiscard]] std::uint32_t BoneDepthUVE(const std::vector<SkeletonBoneUVE>& bones,
                                         const std::uint32_t boneIndex) noexcept {
    std::uint32_t depth = 0U;
    std::int32_t current = static_cast<std::int32_t>(boneIndex);
    constexpr std::uint32_t kMaximumDepthUVE = static_cast<std::uint32_t>(kMaximumSkeletonBonesUVE) + 1U;
    while (current >= 0 && depth < kMaximumDepthUVE) {
        const std::size_t index = static_cast<std::size_t>(current);
        if (index >= bones.size()) break;
        current = bones[index].parentIndex;
        ++depth;
    }
    return depth;
}

/// Grows `pose` until it covers every bone, seeding new entries from the rest pose. The ragdoll has
/// to write into a pose that exists; leaving it short would mean the write-back silently addressed
/// nothing for the bones past the end.
void EnsureSkeletonPoseUVE(Skeleton3DComponentUVE& skeleton) {
    const std::size_t boneCount = skeleton.bones.size();
    if (skeleton.pose.size() >= boneCount) return;
    const std::size_t filled = skeleton.pose.size();
    skeleton.pose.resize(boneCount);
    for (std::size_t index = filled; index < boneCount; ++index) {
        skeleton.pose[index] = SkeletonBonePoseUVE{skeleton.bones[index].localPosition,
                                                   skeleton.bones[index].localRotation,
                                                   skeleton.bones[index].localScale};
    }
}

[[nodiscard]] bool ResolveSkeletonWorldFrameUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                                ObjectWorldFrameUVE& outFrame) {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
        return false;
    }
    const WorldTransformComponentUVE& world = entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
    outFrame.position = world.worldPosition;
    outFrame.rotation = world.worldRotation;
    outFrame.scale = world.worldScale;
    if (!IsFinite3DObjectVectorUVE(outFrame.position) || !Math::IsFiniteUVE(outFrame.rotation) ||
        !IsFinite3DObjectVectorUVE(outFrame.scale)) {
        return false;
    }
    return true;
}

/// Finds the nearest ancestor of `boneIndex` that is present in `simulated`, i.e. the bone this one
/// should be jointed to. This is what makes a rig with un-simulated helper bones still articulate:
/// the child joints to its closest simulated ancestor rather than to a body that does not exist.
[[nodiscard]] std::uint32_t FindSimulatedAncestorUVE(const std::vector<SkeletonBoneUVE>& bones,
                                                     const std::vector<bool>& simulated,
                                                     const std::uint32_t boneIndex) noexcept {
    std::int32_t current = bones.empty() ? -1 : bones[boneIndex].parentIndex;
    constexpr std::uint32_t kMaximumDepthUVE = static_cast<std::uint32_t>(kMaximumSkeletonBonesUVE) + 1U;
    std::uint32_t hops = 0U;
    while (current >= 0 && hops < kMaximumDepthUVE) {
        const std::size_t index = static_cast<std::size_t>(current);
        if (index >= bones.size()) return kInvalidSkeletonBoneIndexUVE;
        if (simulated[index]) return static_cast<std::uint32_t>(index);
        current = bones[index].parentIndex;
        ++hops;
    }
    return kInvalidSkeletonBoneIndexUVE;
}

/// The first child of `boneIndex` in storage order. Storage order is the skeleton's own, so the
/// answer is deterministic for a given asset rather than dependent on how the hierarchy happened to
/// be traversed.
/// Closest point to `point` on the capsule axis of `body`, in world space. The axis is the body's
/// local +Y, which is the convention `RagdollBodyUVE` documents.
[[nodiscard]] Math::Vector3UVE ClosestAxisPointUVE(const Physics::RagdollBodyUVE& body,
                                                  const Math::Vector3UVE& point) noexcept {
    const Math::Vector3UVE axis = Math::RotateVectorUVE(body.rotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const float halfHeight = std::max(0.0F, body.halfHeight);
    const float projection = Math::DotUVE(point - body.position, axis);
    const float clamped = std::clamp(projection, -halfHeight, halfHeight);
    return body.position + axis * clamped;
}

[[nodiscard]] std::uint32_t FindFirstChildUVE(const std::vector<SkeletonBoneUVE>& bones,
                                              const std::uint32_t boneIndex) noexcept {
    for (std::size_t index = 0U; index < bones.size(); ++index) {
        if (bones[index].parentIndex == static_cast<std::int32_t>(boneIndex)) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return kInvalidSkeletonBoneIndexUVE;
}

} // namespace

std::span<const HumanoidRagdollProfileEntryUVE> GetHumanoidRagdollProfileUVE() noexcept {
    return std::span<const HumanoidRagdollProfileEntryUVE>{kHumanoidProfileUVE};
}

bool IsRagdoll3DComponentValidUVE(const Ragdoll3DComponentUVE& value) noexcept {
    if (value.skeleton == kInvalidEntityUVE) return false;
    if (!IsFinite3DObjectVectorUVE(value.gravity) || !IsFinite3DObjectVectorUVE(value.initialVelocity)) {
        return false;
    }
    for (const RagdollBoneSetupUVE& bone : value.bones) {
        if (bone.boneName.empty()) return false;
        if (!std::isfinite(bone.mass) || bone.mass <= 0.0F) return false;
        if (!std::isfinite(bone.radius) || bone.radius <= 0.0F) return false;
        if (!std::isfinite(bone.halfHeight) || bone.halfHeight < 0.0F) return false;
        if (!std::isfinite(bone.linearDamping) || bone.linearDamping < 0.0F) return false;
        if (!std::isfinite(bone.angularDamping) || bone.angularDamping < 0.0F) return false;
        if (!std::isfinite(bone.friction) || bone.friction < 0.0F || bone.friction > 1.0F) return false;
        if (!std::isfinite(bone.swing1LimitDegrees) || bone.swing1LimitDegrees < 0.0F ||
            bone.swing1LimitDegrees > 180.0F) {
            return false;
        }
        if (!std::isfinite(bone.swing2LimitDegrees) || bone.swing2LimitDegrees < 0.0F ||
            bone.swing2LimitDegrees > 180.0F) {
            return false;
        }
        if (!std::isfinite(bone.twistLimitDegrees) || bone.twistLimitDegrees < 0.0F ||
            bone.twistLimitDegrees > 180.0F) {
            return false;
        }
        if (bone.twistAsymmetric) {
            if (!std::isfinite(bone.twistLowerLimitDegrees) || !std::isfinite(bone.twistUpperLimitDegrees) ||
                bone.twistLowerLimitDegrees < -180.0F || bone.twistUpperLimitDegrees > 180.0F ||
                bone.twistLowerLimitDegrees > bone.twistUpperLimitDegrees) {
                return false;
            }
        }
    }
    return true;
}

bool IsRagdoll3DObjectDefinitionValidUVE(const Ragdoll3DObjectDefinitionUVE& value) noexcept {
    return IsRagdoll3DComponentValidUVE(value.ragdoll);
}

void ApplyRagdoll3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const Ragdoll3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, Ragdoll3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<Ragdoll3DComponentUVE>(entity, value.ragdoll);
}

std::vector<RagdollBoneSetupUVE> BuildHumanoidRagdollBonesUVE(const std::vector<SkeletonBoneUVE>& skeletonBones) {
    std::vector<RagdollBoneSetupUVE> setups;
    for (std::size_t boneIndex = 0U; boneIndex < skeletonBones.size(); ++boneIndex) {
        const HumanoidRagdollProfileEntryUVE* entry = FindProfileEntryUVE(skeletonBones[boneIndex].name);
        if (entry == nullptr) continue;
        RagdollBoneSetupUVE setup;
        setup.boneName = skeletonBones[boneIndex].name;
        setup.mass = entry->mass;
        setup.radius = entry->radius;
        setup.halfHeight = entry->halfHeight;
        setup.linearDamping = entry->linearDamping;
        setup.angularDamping = entry->angularDamping;
        setup.swing1LimitDegrees = entry->swing1LimitDegrees;
        setup.swing2LimitDegrees = entry->swing2LimitDegrees;
        setup.twistLimitDegrees = entry->twistLimitDegrees;
        if (entry->hinge) {
            // Unreal's "Swing1/Swing2 Locked, Twist Limited" is exactly a knee, an elbow or an ankle.
            setup.swing1Motion = Physics::RagdollMotionUVE::Locked;
            setup.swing2Motion = Physics::RagdollMotionUVE::Locked;
            setup.twistMotion = Physics::RagdollMotionUVE::Limited;
            setup.twistAsymmetric = true;
            setup.twistLowerLimitDegrees = entry->twistLowerLimitDegrees;
            setup.twistUpperLimitDegrees = entry->twistUpperLimitDegrees;
        }
        setups.push_back(std::move(setup));
    }
    return setups;
}

bool InitializeRagdoll3DUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Ragdoll3DComponentUVE>(entity)) {
        return false;
    }
    Ragdoll3DComponentUVE& component = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(entity);
    if (component.skeleton == kInvalidEntityUVE || !entityManager.IsAliveUVE(component.skeleton) ||
        !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(component.skeleton)) {
        return false;
    }
    Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(component.skeleton);
    if (skeleton.bones.empty()) return false;

    ObjectWorldFrameUVE skeletonFrame;
    if (!ResolveSkeletonWorldFrameUVE(entityManager, component.skeleton, skeletonFrame)) return false;

    if (component.bones.empty()) {
        component.bones = BuildHumanoidRagdollBonesUVE(skeleton.bones);
        if (component.bones.empty()) return false;
    }

    // Resolve every authored bone to an index, dropping the ones the rig does not have. A physics
    // asset authored against a different character should simulate the parts it recognises instead
    // of refusing to run - a partly-right ragdoll beats none, and the count is in the bindings.
    std::vector<RagdollBoneSetupUVE> resolved;
    std::vector<std::uint32_t> resolvedBoneIndices;
    resolved.reserve(component.bones.size());
    resolvedBoneIndices.reserve(component.bones.size());
    for (const RagdollBoneSetupUVE& setup : component.bones) {
        std::uint32_t boneIndex = kInvalidSkeletonBoneIndexUVE;
        if (!TryFindSkeletonBoneIndexUVE(skeleton, setup.boneName, boneIndex)) continue;
        if (boneIndex >= skeleton.bones.size()) continue;
        resolved.push_back(setup);
        resolvedBoneIndices.push_back(boneIndex);
    }
    if (resolved.empty()) return false;

    std::vector<bool> simulated(skeleton.bones.size(), false);
    for (const std::uint32_t boneIndex : resolvedBoneIndices) simulated[boneIndex] = true;

    // Bind frames: captured BEFORE anything is written back, because they are the reference every
    // limit is measured against.
    std::vector<ObjectWorldFrameUVE> bindFrames(skeleton.bones.size());
    std::vector<bool> bindResolved(skeleton.bones.size(), false);
    for (const std::uint32_t boneIndex : resolvedBoneIndices) {
        bindResolved[boneIndex] =
            TryResolveSkeletonBoneWorldFrameUVE(skeleton, boneIndex, skeletonFrame, bindFrames[boneIndex]);
    }

    // Build bodies root-first so joint anchors can always be expressed against an existing body.
    std::vector<std::size_t> order(resolved.size());
    for (std::size_t index = 0U; index < order.size(); ++index) order[index] = index;
    std::sort(order.begin(), order.end(), [&resolvedBoneIndices, &skeleton](const std::size_t lhs,
                                                                           const std::size_t rhs) {
        const std::uint32_t lhsDepth = BoneDepthUVE(skeleton.bones, resolvedBoneIndices[lhs]);
        const std::uint32_t rhsDepth = BoneDepthUVE(skeleton.bones, resolvedBoneIndices[rhs]);
        if (lhsDepth != rhsDepth) return lhsDepth < rhsDepth;
        return resolvedBoneIndices[lhs] < resolvedBoneIndices[rhs];
    });

    std::vector<Physics::RagdollBodyUVE> bodies;
    std::vector<Physics::RagdollJointUVE> joints;
    std::vector<RagdollBoneBindingUVE> bindings;
    bodies.reserve(order.size());
    bindings.reserve(order.size());
    std::vector<std::size_t> bodyOfBone(skeleton.bones.size(), kInvalidSkeletonBoneIndexUVE);

    const Math::Vector3UVE characterRight = Math::RotateVectorUVE(skeletonFrame.rotation,
                                                                 Math::Vector3UVE{1.0F, 0.0F, 0.0F});

    for (const std::size_t sourceIndex : order) {
        const RagdollBoneSetupUVE& setup = resolved[sourceIndex];
        const std::uint32_t boneIndex = resolvedBoneIndices[sourceIndex];
        if (!bindResolved[boneIndex]) continue;
        const ObjectWorldFrameUVE& boneFrame = bindFrames[boneIndex];

        // The capsule spans the bone: from this bone's origin toward its first child. A leaf has no
        // child, so it uses its own local axis and the authored half-height.
        const std::uint32_t childIndex = FindFirstChildUVE(skeleton.bones, boneIndex);
        Math::Vector3UVE direction{0.0F, 1.0F, 0.0F};
        float length = 2.0F * (std::max(0.0F, setup.halfHeight) + setup.radius);
        if (childIndex != kInvalidSkeletonBoneIndexUVE && childIndex < bindFrames.size() &&
            TryResolveSkeletonBoneWorldFrameUVE(skeleton, childIndex, skeletonFrame, bindFrames[childIndex])) {
            bindResolved[childIndex] = true;
            const Math::Vector3UVE delta = bindFrames[childIndex].position - boneFrame.position;
            const float distance = std::sqrt(Math::LengthSquaredUVE(delta));
            if (std::isfinite(distance) && distance > kMinimumBoneLengthUVE) {
                direction = delta * (1.0F / distance);
                length = distance;
            }
        }
        if (!(Math::LengthSquaredUVE(direction) > kDirectionEpsilonUVE)) direction = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        direction = direction * (1.0F / std::sqrt(Math::LengthSquaredUVE(direction)));

        Physics::RagdollBodyUVE body;
        body.mass = setup.mass;
        body.radius = setup.radius;
        // Half the cylinder part: the capsule's total length is 2*(halfHeight + radius), and it has
        // to match the bone or the limb will be visibly too long or too short.
        body.halfHeight = std::max(0.005F, length * 0.5F - setup.radius);
        body.linearDamping = setup.linearDamping;
        body.angularDamping = setup.angularDamping;
        body.friction = setup.friction;
        body.restitution = 0.0F;
        body.position = boneFrame.position + direction * (length * 0.5F);
        body.rotation = MakeCapsuleRotationFromAxisUVE(direction);
        body.linearVelocity = component.initialVelocity;
        body.angularVelocity = Math::Vector3UVE{};

        RagdollBoneBindingUVE binding;
        binding.bodyIndex = static_cast<std::uint32_t>(bodies.size());
        binding.boneIndex = boneIndex;
        binding.depth = BoneDepthUVE(skeleton.bones, boneIndex);
        Math::QuaternionUVE inverseBody;
        if (Math::TryInverseUVE(body.rotation, inverseBody)) {
            binding.boneOffset = Math::MultiplyUVE(inverseBody, boneFrame.rotation);
        }
        bodyOfBone[boneIndex] = bodies.size();
        bodies.push_back(body);
        bindings.push_back(binding);
    }
    if (bodies.empty()) return false;

    // Joints, in the same root-first order, so a parent body always exists before its child needs it.
    for (RagdollBoneBindingUVE& binding : bindings) {
        const std::uint32_t ancestorBone = FindSimulatedAncestorUVE(skeleton.bones, simulated, binding.boneIndex);
        binding.parentBoneIndex = ancestorBone;
        if (ancestorBone == kInvalidSkeletonBoneIndexUVE) continue;
        if (bodyOfBone[ancestorBone] == kInvalidSkeletonBoneIndexUVE) continue;

        const std::size_t parentBodyIndex = bodyOfBone[ancestorBone];
        const std::size_t childBodyIndex = binding.bodyIndex;
        const Math::Vector3UVE jointWorldPoint = bindFrames[binding.boneIndex].position;

        // The joint origin is the child bone's head, expressed in both bodies' local space.
        const Physics::RagdollBodyUVE& parentBody = bodies[parentBodyIndex];
        const Physics::RagdollBodyUVE& childBody = bodies[childBodyIndex];
        Math::QuaternionUVE inverseParent;
        Math::QuaternionUVE inverseChild;
        if (!Math::TryInverseUVE(parentBody.rotation, inverseParent) ||
            !Math::TryInverseUVE(childBody.rotation, inverseChild)) {
            continue;
        }

        const RagdollBoneSetupUVE* setup = nullptr;
        for (const RagdollBoneSetupUVE& candidate : resolved) {
            if (candidate.boneName == skeleton.bones[binding.boneIndex].name) {
                setup = &candidate;
                break;
            }
        }
        if (setup == nullptr) continue;

        Physics::RagdollJointUVE joint;
        joint.firstBody = static_cast<std::uint32_t>(parentBodyIndex);
        joint.secondBody = static_cast<std::uint32_t>(childBodyIndex);
        joint.firstAnchor = Math::RotateVectorUVE(inverseParent, jointWorldPoint - parentBody.position);
        joint.secondAnchor = Math::RotateVectorUVE(inverseChild, jointWorldPoint - childBody.position);
        joint.swing1Motion = setup->swing1Motion;
        joint.swing2Motion = setup->swing2Motion;
        joint.twistMotion = setup->twistMotion;
        joint.swing1LimitDegrees = setup->swing1LimitDegrees;
        joint.swing2LimitDegrees = setup->swing2LimitDegrees;
        joint.twistLimitDegrees = setup->twistLimitDegrees;
        joint.twistAsymmetric = setup->twistAsymmetric;
        joint.twistLowerLimitDegrees = setup->twistLowerLimitDegrees;
        joint.twistUpperLimitDegrees = setup->twistUpperLimitDegrees;

        // The joint frame's X axis. A hinge bends about the character's lateral axis (a knee does
        // not twist around the shin); a cone twists along the bone itself.
        Math::Vector3UVE jointAxisLocal = setup->jointAxisLocal;
        if (!(Math::LengthSquaredUVE(jointAxisLocal) > kDirectionEpsilonUVE)) {
            const Math::Vector3UVE worldAxis =
                setup->twistMotion == Physics::RagdollMotionUVE::Limited &&
                        setup->swing1Motion == Physics::RagdollMotionUVE::Locked
                    ? characterRight
                    : (childBody.position - parentBody.position);
            jointAxisLocal = Math::RotateVectorUVE(inverseParent, worldAxis);
        }
        joint.firstFrame = Physics::MakeJointFrameFromTwistAxisUVE(jointAxisLocal);
        joint.secondFrame = Physics::ComputeJointSecondFrameUVE(parentBody.rotation, childBody.rotation,
                                                                joint.firstFrame);
        joints.push_back(joint);
    }

    if (!component.solver.ResetUVE(bodies)) return false;
    for (const Physics::RagdollJointUVE& joint : joints) {
        if (!component.solver.AddJointUVE(joint)) return false;
    }

    EnsureSkeletonPoseUVE(skeleton);
    component.bindings = std::move(bindings);
    component.pendingSeconds = 0.0F;
    component.lastReport = Physics::RagdollStepReportUVE{};
    component.initialized = true;
    return true;
}

bool ApplyRagdollImpactUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                           const Math::Vector3UVE& worldPoint, const Math::Vector3UVE& impulse,
                           const float searchRadius) {
    if (!std::isfinite(searchRadius) || searchRadius < 0.0F) return false;
    if (!IsFinite3DObjectVectorUVE(worldPoint) || !IsFinite3DObjectVectorUVE(impulse)) return false;
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Ragdoll3DComponentUVE>(entity) ||
        entityManager.HasComponentUVE<DisabledComponentUVE>(entity)) {
        return false;
    }
    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(entity);
    if (!ragdoll.enabled) return false;
    // An impact on a rig that has never stepped is also the moment it starts simulating, and it must
    // start from the pose it was drawn in or the corpse teleports to its bind pose as it dies.
    if (!ragdoll.initialized && !InitializeRagdoll3DUVE(entityManager, entity)) return false;

    const std::size_t bodyCount = ragdoll.solver.GetBodyCountUVE();
    if (bodyCount == 0U) return false;

    // Nearest CAPSULE, not nearest centre: a hit on the head of a long torso capsule is a hit on the
    // head, and the distance that matters is the one to the surface the hit landed on.
    std::size_t nearestBody = bodyCount;
    float nearestDistance = searchRadius;
    Math::Vector3UVE nearestPoint{};
    for (std::size_t index = 0U; index < bodyCount; ++index) {
        const Physics::RagdollBodyUVE& body = ragdoll.solver.GetBodyUVE(index);
        const Math::Vector3UVE axisPoint = ClosestAxisPointUVE(body, worldPoint);
        const float distance = std::sqrt(Math::LengthSquaredUVE(worldPoint - axisPoint)) - body.radius;
        if (!std::isfinite(distance)) continue;
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestBody = index;
            nearestPoint = axisPoint;
        }
    }
    if (nearestBody == bodyCount) return false;

    ragdoll.solver.ApplyImpulseUVE(static_cast<std::uint32_t>(nearestBody), impulse, nearestPoint);
    return true;
}

std::size_t UpdateRagdoll3DObjectsUVE(IEntityManagerUVE& entityManager, const float deltaTimeSeconds) {
    if (!std::isfinite(deltaTimeSeconds) || !(deltaTimeSeconds > 0.0F)) return 0U;

    // One pass to find the work, so the collider snapshot below is built once for the whole scene
    // rather than once per corpse.
    std::vector<EntityUVE> ragdollEntities;
    entityManager.ForEachUVE<Ragdoll3DComponentUVE>([&ragdollEntities](const EntityUVE entity,
                                                                      Ragdoll3DComponentUVE& ragdoll) {
        if (ragdoll.enabled && ragdollEntities.size() < kMaximumRagdollEntitiesPerUpdateUVE) {
            ragdollEntities.push_back(entity);
        }
    });
    if (ragdollEntities.empty()) return 0U;

    const std::vector<Physics::Detail::ColliderWorldAabbUVE> colliders =
        Physics::Detail::BuildColliderWorldAabbCacheUVE(entityManager);

    std::size_t advancedCount = 0U;
    for (const EntityUVE entity : ragdollEntities) {
        if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Ragdoll3DComponentUVE>(entity) ||
            entityManager.HasComponentUVE<DisabledComponentUVE>(entity)) {
            continue;
        }
        Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(entity);
        if (!ragdoll.enabled) continue;
        if (!ragdoll.initialized && !InitializeRagdoll3DUVE(entityManager, entity)) continue;
        if (!entityManager.IsAliveUVE(ragdoll.skeleton) ||
            !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton)) {
            ragdoll.initialized = false;
            continue;
        }
        Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton);
        ObjectWorldFrameUVE skeletonFrame;
        if (!ResolveSkeletonWorldFrameUVE(entityManager, ragdoll.skeleton, skeletonFrame)) continue;

        std::array<Physics::RagdollContactUVE, Physics::kMaximumRagdollContactsUVE> contacts{};
        Physics::RagdollCollisionFilterUVE filter = ragdoll.filter;
        if (filter.ignoreEntity == kInvalidEntityUVE) filter.ignoreEntity = ragdoll.skeleton;
        const Physics::RagdollContactStatsUVE stats = Physics::BuildRagdollContactsUVE(
            ragdoll.solver, colliders, filter, std::span<Physics::RagdollContactUVE>{contacts});

        // A body that has come to rest is woken by anything touching it - which is what makes a
        // corpse react when something lands on it, rather than staying frozen.
        if (stats.generatedContactCount > 0U && ragdoll.solver.IsSleepingUVE()) ragdoll.solver.WakeUVE();

        ragdoll.pendingSeconds += deltaTimeSeconds;
        std::size_t substeps = 0U;
        const float stepSeconds = Physics::kRagdollFixedStepSecondsUVE;
        while (ragdoll.pendingSeconds >= stepSeconds && substeps < Physics::kMaximumRagdollSubstepsUVE) {
            ragdoll.lastReport = ragdoll.solver.StepUVE(ragdoll.gravity, stepSeconds, contacts, ragdoll.stepSettings);
            ragdoll.pendingSeconds -= stepSeconds;
            ++substeps;
        }
        if (substeps > 0U) ++advancedCount;
        // A hitch is not replayed as a burst: the accumulator is clamped instead, so the simulation
        // runs slow for one frame rather than paying an unbounded cost in an already-late one.
        ragdoll.pendingSeconds = std::min(ragdoll.pendingSeconds,
                                          stepSeconds * static_cast<float>(Physics::kMaximumRagdollSubstepsUVE));

        // ---- Write the simulation back into the skeleton's runtime pose, root first ----
        std::vector<RagdollBoneBindingUVE> ordered = ragdoll.bindings;
        std::sort(ordered.begin(), ordered.end(),
                  [](const RagdollBoneBindingUVE& lhs, const RagdollBoneBindingUVE& rhs) {
                      if (lhs.depth != rhs.depth) return lhs.depth < rhs.depth;
                      return lhs.boneIndex < rhs.boneIndex;
                  });
        for (const RagdollBoneBindingUVE& binding : ordered) {
            if (binding.boneIndex >= skeleton.bones.size()) continue;
            if (binding.bodyIndex >= ragdoll.solver.GetBodyCountUVE()) continue;
            const Physics::RagdollBodyUVE& body = ragdoll.solver.GetBodyUVE(binding.bodyIndex);
            if (!Math::IsFiniteUVE(body.rotation)) continue;

            const Math::QuaternionUVE boneWorldRotation = Math::MultiplyUVE(body.rotation, binding.boneOffset);
            Math::QuaternionUVE parentWorldRotation = skeletonFrame.rotation;
            if (binding.parentBoneIndex != kInvalidSkeletonBoneIndexUVE) {
                ObjectWorldFrameUVE parentFrame;
                if (TryResolveSkeletonBoneWorldFrameUVE(skeleton, binding.parentBoneIndex, skeletonFrame,
                                                        parentFrame)) {
                    parentWorldRotation = parentFrame.rotation;
                }
            }
            Math::QuaternionUVE inverseParent;
            if (!Math::TryInverseUVE(parentWorldRotation, inverseParent)) continue;
            Math::QuaternionUVE localRotation;
            if (!Math::TryNormalizeUVE(Math::MultiplyUVE(inverseParent, boneWorldRotation), localRotation)) {
                continue;
            }
            // Rotation only. The bone's length and position still come from the skeleton, so the
            // mesh cannot stretch no matter what the solver does.
            skeleton.pose[binding.boneIndex].rotation = localRotation;
        }
    }
    return advancedCount;
}

} // namespace UVE::Scene
