// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ragdoll_3d.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "disabled_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"
#include "skeleton_3d.h"
#include "collider_world_aabb_cache.h"

namespace UVE::Scene {
namespace {

constexpr float kDirectionEpsilonUVE = 1.0e-6F;
constexpr float kMinimumBoneLengthUVE = 0.02F;
constexpr std::size_t kMaximumRagdollEntitiesPerUpdateUVE = 64U;
constexpr float kMaxInheritedLinearSpeedUVE = 25.0F;
constexpr float kMaxInheritedAngularSpeedUVE = 30.0F;
constexpr float kMaxDynamicPushbackImpulseUVE = 120.0F;

/// Expanded industry-standard humanoid profile table (UE5 Mannequin, Mixamo, VRM / Unity Humanoid,
/// Blender, and generic DCC rigs). Ordered most-specific-first; all patterns strictly lowercase.
constexpr std::array<HumanoidRagdollProfileEntryUVE, 38U> kHumanoidProfileUVE{{
    {"lowerarm", 1.5F, 0.045F, 0.10F, 0.05F, 1.0F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"lower_arm", 1.5F, 0.045F, 0.10F, 0.05F, 1.0F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"forearm", 1.5F, 0.045F, 0.10F, 0.05F, 1.0F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"arm_lower", 1.5F, 0.045F, 0.10F, 0.05F, 1.0F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"elbow", 1.5F, 0.045F, 0.10F, 0.05F, 1.0F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"upperarm", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"upper_arm", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"arm_upper", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"leftarm", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"rightarm", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"arm_l", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"arm_r", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"arm.l", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"arm.r", 2.5F, 0.055F, 0.12F, 0.05F, 0.8F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"clavicle", 1.5F, 0.05F, 0.08F, 0.05F, 0.6F, 20.0F, 20.0F, 30.0F, false, -30.0F, 30.0F},
    {"shoulder", 1.5F, 0.05F, 0.08F, 0.05F, 0.6F, 20.0F, 20.0F, 30.0F, false, -30.0F, 30.0F},
    {"hand", 0.5F, 0.035F, 0.04F, 0.10F, 1.5F, 60.0F, 60.0F, 40.0F, false, -40.0F, 40.0F},
    {"thigh", 9.0F, 0.08F, 0.16F, 0.05F, 0.5F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"upperleg", 9.0F, 0.08F, 0.16F, 0.05F, 0.5F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"upper_leg", 9.0F, 0.08F, 0.16F, 0.05F, 0.5F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"upleg", 9.0F, 0.08F, 0.16F, 0.05F, 0.5F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"up_leg", 9.0F, 0.08F, 0.16F, 0.05F, 0.5F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"leg_upper", 9.0F, 0.08F, 0.16F, 0.05F, 0.5F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"calf", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"shin", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"lowerleg", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"lower_leg", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"leg_lower", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"leftleg", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"rightleg", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"leg_l", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"leg_r", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"leg.l", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"leg.r", 3.5F, 0.06F, 0.16F, 0.05F, 0.7F, 5.0F, 5.0F, 150.0F, true, -150.0F, 5.0F},
    {"foot", 1.0F, 0.04F, 0.06F, 0.10F, 1.5F, 5.0F, 5.0F, 40.0F, true, -40.0F, 40.0F},
    {"neck", 1.5F, 0.05F, 0.04F, 0.05F, 0.8F, 30.0F, 30.0F, 40.0F, false, -40.0F, 40.0F},
    {"head", 5.0F, 0.09F, 0.05F, 0.05F, 0.8F, 30.0F, 30.0F, 30.0F, false, -30.0F, 30.0F},
    {"spine", 6.0F, 0.10F, 0.08F, 0.05F, 0.4F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
}};

/// Separate torso aliases checked after `kHumanoidProfileUVE` so VRM/Unity `Chest`/`UpperChest`
/// rigs auto-build cleanly alongside standard spine chains.
constexpr std::array<HumanoidRagdollProfileEntryUVE, 3U> kHumanoidTorsoFallbackProfileUVE{{
    {"upperchest", 6.0F, 0.11F, 0.09F, 0.05F, 0.4F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
    {"chest", 6.0F, 0.11F, 0.09F, 0.05F, 0.4F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
    {"torso", 6.0F, 0.11F, 0.09F, 0.05F, 0.4F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
}};

/// The root row is separate because `pelvis` and `hips` win over `spine`.
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
    for (const HumanoidRagdollProfileEntryUVE& entry : kHumanoidTorsoFallbackProfileUVE) {
        if (lowered.find(entry.pattern) != std::string::npos) return &entry;
    }
    return nullptr;
}

[[nodiscard]] bool IsMidlineBoneNameUVE(const std::string_view boneName) {
    const std::string lowered = ToLowerAsciiUVE(boneName);
    return lowered.find("spine") != std::string::npos || lowered.find("chest") != std::string::npos ||
           lowered.find("torso") != std::string::npos || lowered.find("neck") != std::string::npos ||
           lowered.find("head") != std::string::npos;
}

[[nodiscard]] bool IsLeftBoneNameUVE(const std::string_view boneName) {
    const std::string lowered = ToLowerAsciiUVE(boneName);
    return lowered.find("left") != std::string::npos || lowered.ends_with("_l") || lowered.ends_with(".l");
}

[[nodiscard]] bool IsRightBoneNameUVE(const std::string_view boneName) {
    const std::string lowered = ToLowerAsciiUVE(boneName);
    return lowered.find("right") != std::string::npos || lowered.ends_with("_r") || lowered.ends_with(".r");
}

[[nodiscard]] Math::Vector3UVE SafeNormalizeUVE(const Math::Vector3UVE& v,
                                                const Math::Vector3UVE& fallback) noexcept {
    const float lenSq = Math::LengthSquaredUVE(v);
    if (!std::isfinite(lenSq) || !(lenSq > kDirectionEpsilonUVE)) return fallback;
    return v * (1.0F / std::sqrt(lenSq));
}

[[nodiscard]] Math::Vector3UVE ClampVectorLengthUVE(const Math::Vector3UVE& v, const float maxLength) noexcept {
    if (!Math::IsFiniteUVE(v) || !(maxLength > 0.0F)) return Math::Vector3UVE{};
    const float lenSq = Math::LengthSquaredUVE(v);
    if (lenSq <= maxLength * maxLength) return v;
    return v * (maxLength / std::sqrt(lenSq));
}

[[nodiscard]] Math::Vector3UVE LerpVectorUVE(const Math::Vector3UVE& a, const Math::Vector3UVE& b,
                                             const float t) noexcept {
    const float alpha = std::clamp(t, 0.0F, 1.0F);
    return a + (b - a) * alpha;
}

[[nodiscard]] Math::QuaternionUVE SlerpQuaternionUVE(const Math::QuaternionUVE& a, const Math::QuaternionUVE& b,
                                                     const float t) noexcept {
    const float alpha = std::clamp(t, 0.0F, 1.0F);
    if (alpha <= 0.0F) return a;
    if (alpha >= 1.0F) return b;
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Math::QuaternionUVE target = b;
    if (dot < 0.0F) {
        dot = -dot;
        target = Math::QuaternionUVE{-b.x, -b.y, -b.z, -b.w};
    }
    Math::QuaternionUVE out{};
    if (dot > 0.9995F) {
        out = Math::QuaternionUVE{
            a.x + (target.x - a.x) * alpha,
            a.y + (target.y - a.y) * alpha,
            a.z + (target.z - a.z) * alpha,
            a.w + (target.w - a.w) * alpha,
        };
    } else {
        const float theta = std::acos(std::clamp(dot, -1.0F, 1.0F));
        const float sinTheta = std::sin(theta);
        const float wA = std::sin((1.0F - alpha) * theta) / sinTheta;
        const float wB = std::sin(alpha * theta) / sinTheta;
        out = Math::QuaternionUVE{
            a.x * wA + target.x * wB,
            a.y * wA + target.y * wB,
            a.z * wA + target.z * wB,
            a.w * wA + target.w * wB,
        };
    }
    Math::QuaternionUVE normalized;
    return Math::TryNormalizeUVE(out, normalized) ? normalized : b;
}

[[nodiscard]] Math::Vector3UVE ComputeAngularVelocityBetweenUVE(const Math::QuaternionUVE& from,
                                                                const Math::QuaternionUVE& to,
                                                                const float dt) noexcept {
    if (!(dt > 1.0e-5F)) return Math::Vector3UVE{};
    Math::QuaternionUVE delta = Math::MultiplyUVE(to, Math::ConjugateUVE(from));
    if (!Math::IsFiniteUVE(delta)) return Math::Vector3UVE{};
    if (delta.w < 0.0F) {
        delta = Math::QuaternionUVE{-delta.x, -delta.y, -delta.z, -delta.w};
    }
    const Math::Vector3UVE imag{delta.x, delta.y, delta.z};
    const float sinHalf = std::sqrt(Math::LengthSquaredUVE(imag));
    Math::Vector3UVE rotVec{};
    if (sinHalf > 1.0e-6F) {
        const float angle = 2.0F * std::atan2(sinHalf, std::clamp(delta.w, -1.0F, 1.0F));
        rotVec = imag * (angle / sinHalf);
    } else {
        rotVec = imag * 2.0F;
    }
    return ClampVectorLengthUVE(rotVec * (1.0F / dt), kMaxInheritedAngularSpeedUVE);
}

/// Shortest-arc rotation taking the capsule's local +Y onto `axis`.
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

[[nodiscard]] bool IsBoneDescendantOrSelfUVE(const std::vector<SkeletonBoneUVE>& bones,
                                             const std::uint32_t boneIndex,
                                             const std::uint32_t ancestorBoneIndex) noexcept {
    if (ancestorBoneIndex == kInvalidSkeletonBoneIndexUVE) return true;
    std::int32_t current = static_cast<std::int32_t>(boneIndex);
    constexpr std::uint32_t kMaximumDepthUVE = static_cast<std::uint32_t>(kMaximumSkeletonBonesUVE) + 1U;
    std::uint32_t hops = 0U;
    while (current >= 0 && hops < kMaximumDepthUVE) {
        const std::uint32_t idx = static_cast<std::uint32_t>(current);
        if (idx == ancestorBoneIndex) return true;
        if (idx >= bones.size()) break;
        current = bones[idx].parentIndex;
        ++hops;
    }
    return false;
}

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

[[nodiscard]] Math::Vector3UVE ClosestAxisPointUVE(const Physics::RagdollBodyUVE& body,
                                                   const Math::Vector3UVE& point) noexcept {
    const Math::Vector3UVE axis = Math::RotateVectorUVE(body.rotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const float halfHeight = std::max(0.0F, body.halfHeight);
    const float projection = Math::DotUVE(point - body.position, axis);
    const float clamped = std::clamp(projection, -halfHeight, halfHeight);
    return body.position + axis * clamped;
}

/// Picks the anatomically primary child of `boneIndex` for multi-child bones (e.g. Pelvis chooses
/// Spine over LeftThigh/RightThigh; Chest chooses Neck/Head over Clavicle/UpperArm).
[[nodiscard]] std::uint32_t FindPrimaryChildUVE(const std::vector<SkeletonBoneUVE>& bones,
                                                const std::uint32_t boneIndex) noexcept {
    std::uint32_t firstChild = kInvalidSkeletonBoneIndexUVE;
    std::uint32_t midlineChild = kInvalidSkeletonBoneIndexUVE;
    for (std::size_t index = 0U; index < bones.size(); ++index) {
        if (bones[index].parentIndex != static_cast<std::int32_t>(boneIndex)) continue;
        const std::uint32_t childIdx = static_cast<std::uint32_t>(index);
        if (firstChild == kInvalidSkeletonBoneIndexUVE) firstChild = childIdx;
        if (midlineChild == kInvalidSkeletonBoneIndexUVE && IsMidlineBoneNameUVE(bones[index].name)) {
            midlineChild = childIdx;
        }
    }
    return midlineChild != kInvalidSkeletonBoneIndexUVE ? midlineChild : firstChild;
}

/// Detects the character's forward direction in world space from left/right limb symmetry so
/// elbow & knee hinge axes bend anatomically on both +Z-facing (glTF/Mixamo/VRM) and -Z-facing rigs.
[[nodiscard]] Math::Vector3UVE DetectCharacterForwardWorldUVE(const Skeleton3DComponentUVE& skeleton,
                                                              const ObjectWorldFrameUVE& skeletonFrame,
                                                              const std::vector<ObjectWorldFrameUVE>& bindFrames,
                                                              const std::vector<bool>& bindResolved) noexcept {
    const Math::Vector3UVE skelRight =
        SafeNormalizeUVE(Math::RotateVectorUVE(skeletonFrame.rotation, Math::Vector3UVE{1.0F, 0.0F, 0.0F}),
                         Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    const Math::Vector3UVE skelUp =
        SafeNormalizeUVE(Math::RotateVectorUVE(skeletonFrame.rotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F}),
                         Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const Math::Vector3UVE defaultForward =
        SafeNormalizeUVE(Math::RotateVectorUVE(skeletonFrame.rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F}),
                         Math::Vector3UVE{0.0F, 0.0F, 1.0F});

    Math::Vector3UVE leftSum{};
    Math::Vector3UVE rightSum{};
    std::size_t leftCount = 0U;
    std::size_t rightCount = 0U;
    for (std::size_t i = 0U; i < skeleton.bones.size(); ++i) {
        if (!bindResolved[i]) continue;
        if (IsLeftBoneNameUVE(skeleton.bones[i].name)) {
            leftSum += bindFrames[i].position - skeletonFrame.position;
            ++leftCount;
        } else if (IsRightBoneNameUVE(skeleton.bones[i].name)) {
            rightSum += bindFrames[i].position - skeletonFrame.position;
            ++rightCount;
        }
    }
    if (leftCount > 0U && rightCount > 0U) {
        const Math::Vector3UVE leftAvg = leftSum * (1.0F / static_cast<float>(leftCount));
        const Math::Vector3UVE rightAvg = rightSum * (1.0F / static_cast<float>(rightCount));
        const Math::Vector3UVE rightToLeft = leftAvg - rightAvg;
        const Math::Vector3UVE forwardCand = Math::CrossUVE(skelUp, rightToLeft);
        if (Math::LengthSquaredUVE(forwardCand) > kDirectionEpsilonUVE) {
            return SafeNormalizeUVE(forwardCand, defaultForward);
        }
    } else if (leftCount > 0U) {
        const Math::Vector3UVE leftAvg = leftSum * (1.0F / static_cast<float>(leftCount));
        const float side = Math::DotUVE(leftAvg, skelRight);
        if (std::fabs(side) > 0.02F) {
            return side > 0.0F ? defaultForward : (defaultForward * -1.0F);
        }
    }
    return defaultForward;
}

void ResolveEffectiveStepSettingsUVE(const Ragdoll3DComponentUVE& ragdoll,
                                     Physics::RagdollStepSettingsUVE& outStepSettings,
                                     std::uint32_t& outMaxSubsteps, bool& outDynamicPushback) noexcept {
    outStepSettings = ragdoll.stepSettings;
    outMaxSubsteps = std::clamp<std::uint32_t>(ragdoll.maxSubstepsPerFrame, 1U, Physics::kMaximumRagdollSubstepsUVE);
    outDynamicPushback = ragdoll.enableDynamicPushback;

    switch (ragdoll.qualityPreset) {
    case RagdollQualityPresetUVE::DesktopAAA:
        outStepSettings.enableSelfCollision = ragdoll.enableSelfCollision;
        outStepSettings.stabilizeInertia = true;
        break;
    case RagdollQualityPresetUVE::MobileBalanced:
        outStepSettings.velocityIterations = std::min<std::size_t>(outStepSettings.velocityIterations, 6U);
        outStepSettings.positionIterations = std::min<std::size_t>(outStepSettings.positionIterations, 3U);
        outStepSettings.enableSelfCollision = ragdoll.enableSelfCollision;
        outStepSettings.stabilizeInertia = true;
        outMaxSubsteps = std::min<std::uint32_t>(outMaxSubsteps, 4U);
        break;
    case RagdollQualityPresetUVE::MobileFast:
        outStepSettings.velocityIterations = std::min<std::size_t>(outStepSettings.velocityIterations, 4U);
        outStepSettings.positionIterations = std::min<std::size_t>(outStepSettings.positionIterations, 2U);
        outStepSettings.enableSelfCollision = false;
        outStepSettings.stabilizeInertia = true;
        outMaxSubsteps = std::min<std::uint32_t>(outMaxSubsteps, 2U);
        outDynamicPushback = false;
        break;
    case RagdollQualityPresetUVE::Custom:
        outStepSettings.enableSelfCollision = ragdoll.enableSelfCollision;
        break;
    }
}

void SyncBodiesFromAnimatedPoseUVE(Ragdoll3DComponentUVE& ragdoll, const Skeleton3DComponentUVE& skeleton,
                                   const ObjectWorldFrameUVE& skeletonFrame, const float deltaTimeSeconds,
                                   const bool kinematicOnlyBodies) {
    for (RagdollBoneBindingUVE& binding : ragdoll.bindings) {
        if (binding.bodyIndex >= ragdoll.solver.GetBodyCountUVE()) continue;
        if (binding.boneIndex >= skeleton.bones.size()) continue;
        Physics::RagdollBodyUVE& body = ragdoll.solver.GetBodyUVE(binding.bodyIndex);
        if (kinematicOnlyBodies && !body.kinematic) continue;

        ObjectWorldFrameUVE boneFrame;
        if (!TryResolveSkeletonBoneWorldFrameUVE(skeleton, binding.boneIndex, skeletonFrame, boneFrame)) continue;

        Math::QuaternionUVE inverseOffset;
        if (!Math::TryInverseUVE(binding.boneOffset, inverseOffset)) continue;
        Math::QuaternionUVE bodyRot;
        if (!Math::TryNormalizeUVE(Math::MultiplyUVE(boneFrame.rotation, inverseOffset), bodyRot)) continue;
        const Math::Vector3UVE bodyPos =
            boneFrame.position - Math::RotateVectorUVE(bodyRot, binding.rootAnchorLocal);
        if (!Math::IsFiniteUVE(bodyPos)) continue;

        if (binding.hasPreviousWorldPose && deltaTimeSeconds > 1.0e-5F) {
            body.linearVelocity =
                ClampVectorLengthUVE((bodyPos - binding.prevWorldPosition) * (1.0F / deltaTimeSeconds),
                                     kMaxInheritedLinearSpeedUVE);
            body.angularVelocity =
                ComputeAngularVelocityBetweenUVE(binding.prevWorldRotation, bodyRot, deltaTimeSeconds);
        } else {
            body.linearVelocity = Math::Vector3UVE{};
            body.angularVelocity = Math::Vector3UVE{};
        }
        body.position = bodyPos;
        body.rotation = bodyRot;
        binding.prevWorldPosition = bodyPos;
        binding.prevWorldRotation = bodyRot;
        binding.hasPreviousWorldPose = true;
    }
}

void UpdatePartialAndMotorTargetsUVE(Ragdoll3DComponentUVE& ragdoll, const Skeleton3DComponentUVE& skeleton,
                                     const ObjectWorldFrameUVE& skeletonFrame, const float deltaTimeSeconds,
                                     bool& outAnimatedTargetChanged) {
    outAnimatedTargetChanged = false;
    std::uint32_t partialRootBone = kInvalidSkeletonBoneIndexUVE;
    if (!ragdoll.partialRootBoneName.empty()) {
        static_cast<void>(TryFindSkeletonBoneIndexUVE(skeleton, ragdoll.partialRootBoneName, partialRootBone));
    }

    const std::size_t bodyCount = ragdoll.solver.GetBodyCountUVE();
    std::array<Math::Vector3UVE, Physics::kMaximumRagdollBodiesUVE> targetBodyPos{};
    std::array<Math::QuaternionUVE, Physics::kMaximumRagdollBodiesUVE> targetBodyRot{};
    std::array<bool, Physics::kMaximumRagdollBodiesUVE> hasTarget{};

    for (RagdollBoneBindingUVE& binding : ragdoll.bindings) {
        if (binding.bodyIndex >= bodyCount || binding.boneIndex >= skeleton.bones.size()) continue;
        Physics::RagdollBodyUVE& body = ragdoll.solver.GetBodyUVE(binding.bodyIndex);

        const bool setupSim =
            binding.setupIndex < ragdoll.bones.size() ? ragdoll.bones[binding.setupIndex].simulate : true;
        const bool inSubtree = IsBoneDescendantOrSelfUVE(skeleton.bones, binding.boneIndex, partialRootBone);
        body.kinematic = !(setupSim && inSubtree);

        ObjectWorldFrameUVE boneFrame;
        if (TryResolveSkeletonBoneWorldFrameUVE(skeleton, binding.boneIndex, skeletonFrame, boneFrame)) {
            Math::QuaternionUVE invOffset;
            if (Math::TryInverseUVE(binding.boneOffset, invOffset)) {
                Math::QuaternionUVE desiredRot;
                if (Math::TryNormalizeUVE(Math::MultiplyUVE(boneFrame.rotation, invOffset), desiredRot)) {
                    const Math::Vector3UVE desiredPos =
                        boneFrame.position - Math::RotateVectorUVE(desiredRot, binding.rootAnchorLocal);
                    if (Math::IsFiniteUVE(desiredPos)) {
                        targetBodyPos[binding.bodyIndex] = desiredPos;
                        targetBodyRot[binding.bodyIndex] = desiredRot;
                        hasTarget[binding.bodyIndex] = true;
                        if (binding.hasPreviousWorldPose) {
                            const float posDeltaSq = Math::LengthSquaredUVE(desiredPos - binding.prevWorldPosition);
                            const float rotDot =
                                std::fabs(desiredRot.x * binding.prevWorldRotation.x +
                                          desiredRot.y * binding.prevWorldRotation.y +
                                          desiredRot.z * binding.prevWorldRotation.z +
                                          desiredRot.w * binding.prevWorldRotation.w);
                            if (posDeltaSq > 1.0e-6F || rotDot < 0.9999F) {
                                outAnimatedTargetChanged = true;
                            }
                        }
                    }
                }
            }
        }

        // Configure optional root world-space drive in Active mode.
        if (ragdoll.mode == RagdollModeUVE::Active && binding.isRootBody && !body.kinematic &&
            hasTarget[binding.bodyIndex]) {
            body.worldDrive.enabled = (ragdoll.rootDriveMaxLinearForce > 0.0F &&
                                       (ragdoll.rootDriveLinearStiffness > 0.0F || ragdoll.rootDriveLinearDamping > 0.0F)) ||
                                      (ragdoll.rootDriveMaxAngularTorque > 0.0F &&
                                       (ragdoll.rootDriveAngularStiffness > 0.0F || ragdoll.rootDriveAngularDamping > 0.0F));
            body.worldDrive.targetPosition = targetBodyPos[binding.bodyIndex];
            body.worldDrive.targetRotation = targetBodyRot[binding.bodyIndex];
            body.worldDrive.linearStiffness = ragdoll.rootDriveLinearStiffness;
            body.worldDrive.linearDamping = ragdoll.rootDriveLinearDamping;
            body.worldDrive.maxLinearForce = ragdoll.rootDriveMaxLinearForce;
            body.worldDrive.angularStiffness = ragdoll.rootDriveAngularStiffness;
            body.worldDrive.angularDamping = ragdoll.rootDriveAngularDamping;
            body.worldDrive.maxAngularTorque = ragdoll.rootDriveMaxAngularTorque;
            if (binding.hasPreviousWorldPose && deltaTimeSeconds > 1.0e-5F) {
                body.worldDrive.targetLinearVelocity = ClampVectorLengthUVE(
                    (targetBodyPos[binding.bodyIndex] - binding.prevWorldPosition) * (1.0F / deltaTimeSeconds),
                    kMaxInheritedLinearSpeedUVE);
                body.worldDrive.targetAngularVelocity = ComputeAngularVelocityBetweenUVE(
                    binding.prevWorldRotation, targetBodyRot[binding.bodyIndex], deltaTimeSeconds);
            } else {
                body.worldDrive.targetLinearVelocity = Math::Vector3UVE{};
                body.worldDrive.targetAngularVelocity = Math::Vector3UVE{};
            }
        } else {
            body.worldDrive.enabled = false;
        }
    }

    // Move kinematic anchor bodies directly to their animated transforms (used in partial ragdoll!).
    SyncBodiesFromAnimatedPoseUVE(ragdoll, skeleton, skeletonFrame, deltaTimeSeconds, /*kinematicOnlyBodies=*/true);

    // Update joint PD motors for Active mode (and disable them in Passive Simulating mode).
    const std::size_t jointCount = ragdoll.solver.GetJointCountUVE();
    for (std::size_t jointIndex = 0U; jointIndex < jointCount; ++jointIndex) {
        Physics::RagdollJointUVE& joint = ragdoll.solver.GetJointUVE(jointIndex);
        if (ragdoll.mode != RagdollModeUVE::Active) {
            joint.motor.enabled = false;
            continue;
        }
        if (joint.firstBody >= bodyCount || joint.secondBody >= bodyCount ||
            !hasTarget[joint.firstBody] || !hasTarget[joint.secondBody]) {
            joint.motor.enabled = false;
            continue;
        }
        const Math::QuaternionUVE animFirstFrame = Math::MultiplyUVE(targetBodyRot[joint.firstBody], joint.firstFrame);
        const Math::QuaternionUVE animSecondFrame =
            Math::MultiplyUVE(targetBodyRot[joint.secondBody], joint.secondFrame);
        Math::QuaternionUVE invAnimFirst;
        if (!Math::TryInverseUVE(animFirstFrame, invAnimFirst)) {
            joint.motor.enabled = false;
            continue;
        }
        Math::QuaternionUVE targetRel;
        if (!Math::TryNormalizeUVE(Math::MultiplyUVE(invAnimFirst, animSecondFrame), targetRel)) {
            targetRel = Math::QuaternionUVE{};
        }

        float strengthScale = 1.0F;
        for (const RagdollBoneBindingUVE& binding : ragdoll.bindings) {
            if (binding.bodyIndex == joint.secondBody && binding.setupIndex < ragdoll.bones.size()) {
                strengthScale = std::clamp(ragdoll.bones[binding.setupIndex].motorStrengthScale, 0.0F, 1.0F);
                break;
            }
        }

        joint.motor.enabled = true;
        joint.motor.targetRelativeRotation = targetRel;
        joint.motor.targetAngularVelocity = Math::Vector3UVE{};
        joint.motor.angularStiffness = ragdoll.motorStiffness;
        joint.motor.angularDamping = ragdoll.motorDamping;
        joint.motor.maxTorque = ragdoll.maxMotorTorque;
        joint.motor.strengthScale = strengthScale;
    }

    // Track target pose history in Active mode as well.
    if (ragdoll.mode == RagdollModeUVE::Active) {
        for (RagdollBoneBindingUVE& binding : ragdoll.bindings) {
            if (binding.bodyIndex < bodyCount && hasTarget[binding.bodyIndex]) {
                binding.prevWorldPosition = targetBodyPos[binding.bodyIndex];
                binding.prevWorldRotation = targetBodyRot[binding.bodyIndex];
                binding.hasPreviousWorldPose = true;
            }
        }
    }
}

void WriteRagdollPoseToSkeletonUVE(IEntityManagerUVE& entityManager, Ragdoll3DComponentUVE& ragdoll,
                                   Skeleton3DComponentUVE& skeleton, const ObjectWorldFrameUVE& skeletonFrame) {
    EnsureSkeletonPoseUVE(skeleton);
    const float globalBlend = std::clamp(ragdoll.physicsBlendWeight, 0.0F, 1.0F);
    if (!(globalBlend > 0.0F)) return;

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
        if (body.kinematic) continue;
        if (!Math::IsFiniteUVE(body.position) || !Math::IsFiniteUVE(body.rotation)) continue;

        const float boneBlend =
            binding.setupIndex < ragdoll.bones.size()
                ? std::clamp(ragdoll.bones[binding.setupIndex].blendWeight, 0.0F, 1.0F)
                : 1.0F;
        const float weight = globalBlend * boneBlend;
        if (!(weight > 0.0F)) continue;

        // Resolve the IMMEDIATE skeleton parent frame (not just simulated ancestor!) so both root
        // position and child rotations match the skeleton hierarchy cleanly.
        ObjectWorldFrameUVE parentFrame = skeletonFrame;
        const std::int32_t skeletonParent = skeleton.bones[binding.boneIndex].parentIndex;
        if (skeletonParent >= 0 && static_cast<std::size_t>(skeletonParent) < skeleton.bones.size()) {
            ObjectWorldFrameUVE resolvedParent;
            if (TryResolveSkeletonBoneWorldFrameUVE(skeleton, static_cast<std::uint32_t>(skeletonParent),
                                                    skeletonFrame, resolvedParent)) {
                parentFrame = resolvedParent;
            }
        }

        Math::QuaternionUVE inverseParent;
        if (!Math::TryInverseUVE(parentFrame.rotation, inverseParent)) continue;

        const Math::QuaternionUVE boneWorldRotation = Math::MultiplyUVE(body.rotation, binding.boneOffset);
        Math::QuaternionUVE localRotation;
        if (!Math::TryNormalizeUVE(Math::MultiplyUVE(inverseParent, boneWorldRotation), localRotation)) {
            continue;
        }

        // BUG #1 FIX (Floating Corpse): Root/pelvis writes BOTH world position AND rotation back!
        // Non-root limbs remain rotation-only so bone lengths can never stretch.
        if (binding.isRootBody) {
            const Math::Vector3UVE boneWorldPosition =
                body.position + Math::RotateVectorUVE(body.rotation, binding.rootAnchorLocal);
            if (Math::IsFiniteUVE(boneWorldPosition)) {
                if (ragdoll.driveEntityTransform &&
                    entityManager.HasComponentUVE<TransformComponentUVE>(ragdoll.skeleton)) {
                    TransformComponentUVE& transform =
                        entityManager.GetComponentUVE<TransformComponentUVE>(ragdoll.skeleton);
                    const Math::Vector3UVE targetEntityPos =
                        LerpVectorUVE(transform.localPosition, boneWorldPosition - skeleton.bones[binding.boneIndex].localPosition, weight);
                    if (Math::IsFiniteUVE(targetEntityPos)) {
                        transform.localPosition = targetEntityPos;
                        if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(ragdoll.skeleton)) {
                            entityManager.GetComponentUVE<WorldTransformComponentUVE>(ragdoll.skeleton).dirty = true;
                        }
                    }
                } else {
                    const Math::Vector3UVE relWorld = boneWorldPosition - parentFrame.position;
                    Math::Vector3UVE localPos = Math::RotateVectorUVE(inverseParent, relWorld);
                    if (std::fabs(parentFrame.scale.x) > kDirectionEpsilonUVE) localPos.x /= parentFrame.scale.x;
                    if (std::fabs(parentFrame.scale.y) > kDirectionEpsilonUVE) localPos.y /= parentFrame.scale.y;
                    if (std::fabs(parentFrame.scale.z) > kDirectionEpsilonUVE) localPos.z /= parentFrame.scale.z;
                    if (Math::IsFiniteUVE(localPos)) {
                        skeleton.pose[binding.boneIndex].position =
                            LerpVectorUVE(skeleton.pose[binding.boneIndex].position, localPos, weight);
                    }
                }
            }
        }

        skeleton.pose[binding.boneIndex].rotation =
            SlerpQuaternionUVE(skeleton.pose[binding.boneIndex].rotation, localRotation, weight);
    }
}

void ApplyDynamicRigid3DPushbackUVE(IEntityManagerUVE& entityManager,
                                    const std::span<const Physics::Detail::ColliderWorldAabbUVE> colliders,
                                    std::span<Physics::RagdollColliderMotionUVE> colliderMotions,
                                    const std::span<const Physics::RagdollContactFeedbackUVE> feedbackList) {
    for (const Physics::RagdollContactFeedbackUVE& fb : feedbackList) {
        if (fb.colliderIndex >= colliders.size()) continue;
        const EntityUVE target = colliders[fb.colliderIndex].entity;
        if (!entityManager.IsAliveUVE(target) || !entityManager.HasComponentUVE<Rigid3DComponentUVE>(target)) continue;
        Rigid3DComponentUVE& rigid = entityManager.GetComponentUVE<Rigid3DComponentUVE>(target);
        if (rigid.isKinematic) continue;
        const float invMass = ResolveRigid3DInverseMassUVE(rigid);
        if (!(invMass > 0.0F)) continue;

        const Math::Vector3UVE pushImpulse = ClampVectorLengthUVE(fb.appliedImpulse * -1.0F, kMaxDynamicPushbackImpulseUVE);
        ApplyRigid3DLinearImpulseUVE(rigid, pushImpulse);

        // Optional angular impulse when the dynamic body has non-zero inverse inertia.
        if (Math::LengthSquaredUVE(rigid.inverseInertia) > 0.0F &&
            entityManager.HasComponentUVE<WorldTransformComponentUVE>(target)) {
            const WorldTransformComponentUVE& world = entityManager.GetComponentUVE<WorldTransformComponentUVE>(target);
            const Math::Vector3UVE worldTorqueImpulse = Math::CrossUVE(fb.worldPoint - world.worldPosition, pushImpulse);
            const Math::Vector3UVE localTorque =
                Math::RotateVectorUVE(Math::ConjugateUVE(world.worldRotation), worldTorqueImpulse);
            const Math::Vector3UVE deltaOmega{
                localTorque.x * rigid.inverseInertia.x,
                localTorque.y * rigid.inverseInertia.y,
                localTorque.z * rigid.inverseInertia.z,
            };
            if (Math::IsFiniteUVE(deltaOmega)) {
                rigid.angularVelocity += deltaOmega;
            }
        }

        if (fb.colliderIndex < colliderMotions.size()) {
            colliderMotions[fb.colliderIndex].linearVelocity = rigid.velocity;
            colliderMotions[fb.colliderIndex].angularVelocity = rigid.angularVelocity;
        }
    }
}

} // namespace

std::span<const HumanoidRagdollProfileEntryUVE> GetHumanoidRagdollProfileUVE() noexcept {
    return std::span<const HumanoidRagdollProfileEntryUVE>{kHumanoidProfileUVE};
}

bool IsRagdoll3DComponentAuthoredValidUVE(const Ragdoll3DComponentUVE& value) noexcept {
    if (!IsFinite3DObjectVectorUVE(value.gravity) || !IsFinite3DObjectVectorUVE(value.initialVelocity)) {
        return false;
    }
    if (!std::isfinite(value.physicsBlendWeight) || value.physicsBlendWeight < 0.0F ||
        value.physicsBlendWeight > 1.0F) {
        return false;
    }
    if (!std::isfinite(value.motorStiffness) || value.motorStiffness < 0.0F ||
        !std::isfinite(value.motorDamping) || value.motorDamping < 0.0F ||
        !std::isfinite(value.maxMotorTorque) || value.maxMotorTorque < 0.0F) {
        return false;
    }
    if (!std::isfinite(value.rootDriveLinearStiffness) || value.rootDriveLinearStiffness < 0.0F ||
        !std::isfinite(value.rootDriveLinearDamping) || value.rootDriveLinearDamping < 0.0F ||
        !std::isfinite(value.rootDriveMaxLinearForce) || value.rootDriveMaxLinearForce < 0.0F ||
        !std::isfinite(value.rootDriveAngularStiffness) || value.rootDriveAngularStiffness < 0.0F ||
        !std::isfinite(value.rootDriveAngularDamping) || value.rootDriveAngularDamping < 0.0F ||
        !std::isfinite(value.rootDriveMaxAngularTorque) || value.rootDriveMaxAngularTorque < 0.0F) {
        return false;
    }
    if (value.maxSubstepsPerFrame == 0U || value.maxSubstepsPerFrame > Physics::kMaximumRagdollSubstepsUVE) {
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
        if (!std::isfinite(bone.motorStrengthScale) || bone.motorStrengthScale < 0.0F ||
            bone.motorStrengthScale > 1.0F) {
            return false;
        }
        if (!std::isfinite(bone.blendWeight) || bone.blendWeight < 0.0F || bone.blendWeight > 1.0F) {
            return false;
        }
    }
    return true;
}

bool IsRagdoll3DComponentValidUVE(const Ragdoll3DComponentUVE& value) noexcept {
    if (value.skeleton == kInvalidEntityUVE) return false;
    return IsRagdoll3DComponentAuthoredValidUVE(value);
}

bool IsRagdoll3DObjectDefinitionValidUVE(const Ragdoll3DObjectDefinitionUVE& value) noexcept {
    return IsRagdoll3DComponentAuthoredValidUVE(value.ragdoll);
}

void ApplyRagdoll3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const Ragdoll3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, Ragdoll3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<Ragdoll3DComponentUVE>(entity, value.ragdoll);
}

void ApplyRagdollQualityPresetUVE(Ragdoll3DComponentUVE& ragdoll, const RagdollQualityPresetUVE preset) noexcept {
    ragdoll.qualityPreset = preset;
    switch (preset) {
    case RagdollQualityPresetUVE::DesktopAAA:
        ragdoll.stepSettings.velocityIterations = Physics::kRagdollDefaultVelocityIterationsUVE;
        ragdoll.stepSettings.positionIterations = Physics::kRagdollDefaultPositionIterationsUVE;
        ragdoll.stepSettings.stabilizeInertia = true;
        ragdoll.enableSelfCollision = true;
        ragdoll.enableDynamicPushback = true;
        ragdoll.maxSubstepsPerFrame = Physics::kMaximumRagdollSubstepsUVE;
        break;
    case RagdollQualityPresetUVE::MobileBalanced:
        ragdoll.stepSettings.velocityIterations = 6U;
        ragdoll.stepSettings.positionIterations = 3U;
        ragdoll.stepSettings.stabilizeInertia = true;
        ragdoll.enableSelfCollision = true;
        ragdoll.enableDynamicPushback = true;
        ragdoll.maxSubstepsPerFrame = 4U;
        break;
    case RagdollQualityPresetUVE::MobileFast:
        ragdoll.stepSettings.velocityIterations = 4U;
        ragdoll.stepSettings.positionIterations = 2U;
        ragdoll.stepSettings.stabilizeInertia = true;
        ragdoll.enableSelfCollision = false;
        ragdoll.enableDynamicPushback = false;
        ragdoll.maxSubstepsPerFrame = 2U;
        break;
    case RagdollQualityPresetUVE::Custom:
        break;
    }
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

    std::vector<std::size_t> resolvedSetupIndices;
    std::vector<std::uint32_t> resolvedBoneIndices;
    resolvedSetupIndices.reserve(component.bones.size());
    resolvedBoneIndices.reserve(component.bones.size());
    for (std::size_t setupIndex = 0U; setupIndex < component.bones.size(); ++setupIndex) {
        std::uint32_t boneIndex = kInvalidSkeletonBoneIndexUVE;
        if (!TryFindSkeletonBoneIndexUVE(skeleton, component.bones[setupIndex].boneName, boneIndex)) continue;
        if (boneIndex >= skeleton.bones.size()) continue;
        resolvedSetupIndices.push_back(setupIndex);
        resolvedBoneIndices.push_back(boneIndex);
    }
    if (resolvedSetupIndices.empty()) return false;

    std::vector<bool> simulated(skeleton.bones.size(), false);
    for (const std::uint32_t boneIndex : resolvedBoneIndices) simulated[boneIndex] = true;

    std::vector<ObjectWorldFrameUVE> bindFrames(skeleton.bones.size());
    std::vector<bool> bindResolved(skeleton.bones.size(), false);
    for (std::size_t boneIndex = 0U; boneIndex < skeleton.bones.size(); ++boneIndex) {
        bindResolved[boneIndex] = TryResolveSkeletonBoneWorldFrameUVE(
            skeleton, static_cast<std::uint32_t>(boneIndex), skeletonFrame, bindFrames[boneIndex]);
    }

    const Math::Vector3UVE characterForward =
        DetectCharacterForwardWorldUVE(skeleton, skeletonFrame, bindFrames, bindResolved);
    const Math::Vector3UVE characterRight =
        SafeNormalizeUVE(Math::RotateVectorUVE(skeletonFrame.rotation, Math::Vector3UVE{1.0F, 0.0F, 0.0F}),
                         Math::Vector3UVE{1.0F, 0.0F, 0.0F});

    // Build bodies root-first so joint anchors are always expressed against an existing parent body.
    std::vector<std::size_t> order(resolvedSetupIndices.size());
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

    for (const std::size_t listIndex : order) {
        const std::size_t setupIndex = resolvedSetupIndices[listIndex];
        const RagdollBoneSetupUVE& setup = component.bones[setupIndex];
        const std::uint32_t boneIndex = resolvedBoneIndices[listIndex];
        if (!bindResolved[boneIndex]) continue;
        const ObjectWorldFrameUVE& boneFrame = bindFrames[boneIndex];

        // BUG #5 FIX: Use midline-aware primary child selection for Pelvis/Chest multi-child bones!
        const std::uint32_t childIndex = FindPrimaryChildUVE(skeleton.bones, boneIndex);
        Math::Vector3UVE direction{0.0F, 1.0F, 0.0F};
        float length = 2.0F * (std::max(0.0F, setup.halfHeight) + setup.radius);
        if (childIndex != kInvalidSkeletonBoneIndexUVE && childIndex < bindFrames.size() && bindResolved[childIndex]) {
            const Math::Vector3UVE delta = bindFrames[childIndex].position - boneFrame.position;
            const float distance = std::sqrt(Math::LengthSquaredUVE(delta));
            if (std::isfinite(distance) && distance > kMinimumBoneLengthUVE) {
                direction = delta * (1.0F / distance);
                length = distance;
            }
        }
        direction = SafeNormalizeUVE(direction, Math::Vector3UVE{0.0F, 1.0F, 0.0F});

        Physics::RagdollBodyUVE body;
        body.mass = setup.mass;
        body.radius = setup.radius;
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
        binding.setupIndex = setupIndex;
        binding.depth = BoneDepthUVE(skeleton.bones, boneIndex);
        Math::QuaternionUVE inverseBody;
        if (Math::TryInverseUVE(body.rotation, inverseBody)) {
            binding.boneOffset = Math::MultiplyUVE(inverseBody, boneFrame.rotation);
            binding.rootAnchorLocal = Math::RotateVectorUVE(inverseBody, boneFrame.position - body.position);
        }
        binding.prevWorldPosition = body.position;
        binding.prevWorldRotation = body.rotation;
        binding.hasPreviousWorldPose = true;

        bodyOfBone[boneIndex] = bodies.size();
        bodies.push_back(body);
        bindings.push_back(binding);
    }
    if (bodies.empty()) return false;

    for (RagdollBoneBindingUVE& binding : bindings) {
        const RagdollBoneSetupUVE& setup = component.bones[binding.setupIndex];
        std::uint32_t ancestorBone = kInvalidSkeletonBoneIndexUVE;
        if (!setup.parentBoneName.empty()) {
            std::uint32_t explicitParent = kInvalidSkeletonBoneIndexUVE;
            if (TryFindSkeletonBoneIndexUVE(skeleton, setup.parentBoneName, explicitParent) &&
                explicitParent < bodyOfBone.size() && bodyOfBone[explicitParent] != kInvalidSkeletonBoneIndexUVE &&
                explicitParent != binding.boneIndex) {
                ancestorBone = explicitParent;
            }
        }
        if (ancestorBone == kInvalidSkeletonBoneIndexUVE) {
            ancestorBone = FindSimulatedAncestorUVE(skeleton.bones, simulated, binding.boneIndex);
        }
        binding.parentBoneIndex = ancestorBone;
        if (ancestorBone == kInvalidSkeletonBoneIndexUVE || bodyOfBone[ancestorBone] == kInvalidSkeletonBoneIndexUVE) {
            binding.isRootBody = true;
            binding.parentBodyIndex = Physics::kInvalidRagdollBodyUVE;
            continue;
        }

        const std::size_t parentBodyIndex = bodyOfBone[ancestorBone];
        const std::size_t childBodyIndex = binding.bodyIndex;
        binding.parentBodyIndex = static_cast<std::uint32_t>(parentBodyIndex);
        binding.isRootBody = false;

        const Math::Vector3UVE jointWorldPoint = bindFrames[binding.boneIndex].position;
        const Physics::RagdollBodyUVE& parentBody = bodies[parentBodyIndex];
        const Physics::RagdollBodyUVE& childBody = bodies[childBodyIndex];
        Math::QuaternionUVE inverseParent;
        Math::QuaternionUVE inverseChild;
        if (!Math::TryInverseUVE(parentBody.rotation, inverseParent) ||
            !Math::TryInverseUVE(childBody.rotation, inverseChild)) {
            continue;
        }

        Physics::RagdollJointUVE joint;
        joint.firstBody = static_cast<std::uint32_t>(parentBodyIndex);
        joint.secondBody = static_cast<std::uint32_t>(childBodyIndex);
        joint.firstAnchor = Math::RotateVectorUVE(inverseParent, jointWorldPoint - parentBody.position);
        joint.secondAnchor = Math::RotateVectorUVE(inverseChild, jointWorldPoint - childBody.position);
        joint.swing1Motion = setup.swing1Motion;
        joint.swing2Motion = setup.swing2Motion;
        joint.twistMotion = setup.twistMotion;
        joint.swing1LimitDegrees = setup.swing1LimitDegrees;
        joint.swing2LimitDegrees = setup.swing2LimitDegrees;
        joint.twistLimitDegrees = setup.twistLimitDegrees;
        joint.twistAsymmetric = setup.twistAsymmetric;
        joint.twistLowerLimitDegrees = setup.twistLowerLimitDegrees;
        joint.twistUpperLimitDegrees = setup.twistUpperLimitDegrees;

        // BUG #5 FIX: Universal T-pose / A-pose hinge axis derivation!
        // `Cross(limbDir, characterForward)` yields an axis strictly orthogonal to both the limb
        // and the forward plane: positive twist folds T/A-pose elbows inward toward the chest,
        // while negative twist folds knees anatomically backward!
        Math::Vector3UVE jointAxisLocal = setup.jointAxisLocal;
        if (!(Math::LengthSquaredUVE(jointAxisLocal) > kDirectionEpsilonUVE)) {
            const bool isHinge = setup.twistMotion == Physics::RagdollMotionUVE::Limited &&
                                 setup.swing1Motion == Physics::RagdollMotionUVE::Locked;
            Math::Vector3UVE worldAxis{};
            if (isHinge) {
                const Math::Vector3UVE limbDir =
                    SafeNormalizeUVE(Math::RotateVectorUVE(childBody.rotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F}),
                                     Math::Vector3UVE{0.0F, -1.0F, 0.0F});
                const Math::Vector3UVE crossAxis = Math::CrossUVE(limbDir, characterForward);
                if (Math::LengthSquaredUVE(crossAxis) > kDirectionEpsilonUVE) {
                    worldAxis = SafeNormalizeUVE(crossAxis, characterRight);
                } else {
                    worldAxis = characterRight;
                }
            } else {
                worldAxis = SafeNormalizeUVE(childBody.position - parentBody.position,
                                             Math::Vector3UVE{0.0F, 1.0F, 0.0F});
            }
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

    // Jolt-style non-adjacent self-collision filter table: disable parent-child + bind-pose overlaps.
    component.solver.RebuildSelfCollisionFilterUVE(/*disableCurrentPoseOverlaps=*/true, /*minimumJointHops=*/2U);

    EnsureSkeletonPoseUVE(skeleton);
    component.bindings = std::move(bindings);
    component.pendingSeconds = 0.0F;
    component.hadFloorSupport = false;
    component.lastReport = Physics::RagdollStepReportUVE{};
    component.initialized = true;
    return true;
}

bool ActivateRagdollUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const RagdollModeUVE mode,
                        const Math::Vector3UVE& extraVelocity) {
    if (!IsFinite3DObjectVectorUVE(extraVelocity)) return false;
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Ragdoll3DComponentUVE>(entity)) {
        return false;
    }
    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(entity);
    if (!ragdoll.initialized && !InitializeRagdoll3DUVE(entityManager, entity)) return false;

    ragdoll.mode = mode;
    ragdoll.enabled = true;
    ragdoll.solver.WakeUVE();

    const std::size_t bodyCount = ragdoll.solver.GetBodyCountUVE();
    for (std::size_t i = 0U; i < bodyCount; ++i) {
        Physics::RagdollBodyUVE& body = ragdoll.solver.GetBodyUVE(i);
        if (!ragdoll.inheritAnimatedVelocity) {
            body.linearVelocity = Math::Vector3UVE{};
            body.angularVelocity = Math::Vector3UVE{};
        }
        body.linearVelocity += extraVelocity;
    }
    return true;
}

bool CaptureRagdollPoseSnapshotUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                   RagdollPoseSnapshotUVE& outSnapshot) {
    outSnapshot = {};
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Ragdoll3DComponentUVE>(entity)) {
        return false;
    }
    const Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(entity);
    if (!entityManager.IsAliveUVE(ragdoll.skeleton) ||
        !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton)) {
        return false;
    }
    const Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton);
    outSnapshot.bonePoses = GetSkeletonCurrentPoseUVE(skeleton);
    for (const RagdollBoneBindingUVE& binding : ragdoll.bindings) {
        if (binding.isRootBody && binding.bodyIndex < ragdoll.solver.GetBodyCountUVE()) {
            const Physics::RagdollBodyUVE& rootBody = ragdoll.solver.GetBodyUVE(binding.bodyIndex);
            outSnapshot.rootWorldPosition =
                rootBody.position + Math::RotateVectorUVE(rootBody.rotation, binding.rootAnchorLocal);
            outSnapshot.rootWorldRotation = Math::MultiplyUVE(rootBody.rotation, binding.boneOffset);
            break;
        }
    }
    outSnapshot.valid = !outSnapshot.bonePoses.empty();
    return outSnapshot.valid;
}

bool EvaluateRagdollGetUpStateUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                  RagdollGetUpStateUVE& outState, const float floorYOffset) {
    outState = {};
    if (!std::isfinite(floorYOffset)) return false;
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Ragdoll3DComponentUVE>(entity)) {
        return false;
    }
    const Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(entity);
    if (!ragdoll.initialized || ragdoll.solver.GetBodyCountUVE() == 0U) return false;
    if (!entityManager.IsAliveUVE(ragdoll.skeleton) ||
        !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton)) {
        return false;
    }
    const Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton);

    const RagdollBoneBindingUVE* rootBinding = nullptr;
    const RagdollBoneBindingUVE* headOrChestBinding = nullptr;
    float lowestSurfaceY = std::numeric_limits<float>::infinity();
    for (const RagdollBoneBindingUVE& binding : ragdoll.bindings) {
        if (binding.bodyIndex >= ragdoll.solver.GetBodyCountUVE()) continue;
        const Physics::RagdollBodyUVE& body = ragdoll.solver.GetBodyUVE(binding.bodyIndex);
        Math::Vector3UVE segStart{};
        Math::Vector3UVE segEnd{};
        Physics::ComputeRagdollCapsuleWorldSegmentUVE(body, segStart, segEnd);
        lowestSurfaceY = std::min(lowestSurfaceY, std::min(segStart.y, segEnd.y) - body.radius);

        if (binding.isRootBody && rootBinding == nullptr) rootBinding = &binding;
        if (binding.boneIndex < skeleton.bones.size()) {
            const std::string lower = ToLowerAsciiUVE(skeleton.bones[binding.boneIndex].name);
            if (lower.find("head") != std::string::npos || lower.find("chest") != std::string::npos ||
                lower.find("spine") != std::string::npos) {
                headOrChestBinding = &binding;
            }
        }
    }
    if (rootBinding == nullptr) rootBinding = &ragdoll.bindings.front();

    const Physics::RagdollBodyUVE& rootBody = ragdoll.solver.GetBodyUVE(rootBinding->bodyIndex);
    const Math::Vector3UVE rootUp = Math::RotateVectorUVE(rootBody.rotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const Math::Vector3UVE rootForward = Math::RotateVectorUVE(rootBody.rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});

    // If torso/pelvis capsule axis is mostly vertical -> Upright; otherwise check chest facing Y.
    if (rootUp.y > 0.65F) {
        outState.orientation = RagdollLyingOrientationUVE::Upright;
    } else if (rootForward.y >= 0.0F) {
        outState.orientation = RagdollLyingOrientationUVE::FaceUp;
    } else {
        outState.orientation = RagdollLyingOrientationUVE::FaceDown;
    }

    const Math::Vector3UVE rootPos =
        rootBody.position + Math::RotateVectorUVE(rootBody.rotation, rootBinding->rootAnchorLocal);
    const float groundY = std::isfinite(lowestSurfaceY) ? (lowestSurfaceY + floorYOffset) : rootPos.y;
    outState.groundedRootPosition = Math::Vector3UVE{rootPos.x, groundY, rootPos.z};

    // Derive horizontal facing direction (pelvis -> head/chest projected on XZ; flip if FaceUp).
    Math::Vector3UVE horizontalDir{rootForward.x, 0.0F, rootForward.z};
    if (headOrChestBinding != nullptr && headOrChestBinding != rootBinding) {
        const Math::Vector3UVE headPos = ragdoll.solver.GetBodyUVE(headOrChestBinding->bodyIndex).position;
        const Math::Vector3UVE spineXZ{headPos.x - rootBody.position.x, 0.0F, headPos.z - rootBody.position.z};
        if (Math::LengthSquaredUVE(spineXZ) > kDirectionEpsilonUVE) {
            horizontalDir = outState.orientation == RagdollLyingOrientationUVE::FaceUp ? (spineXZ * -1.0F) : spineXZ;
        }
    }
    horizontalDir = SafeNormalizeUVE(horizontalDir, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
    const float yaw = std::atan2(horizontalDir.x, horizontalDir.z);
    Math::QuaternionUVE alignedRot;
    if (!Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, yaw, alignedRot)) {
        alignedRot = Math::QuaternionUVE{};
    }
    outState.alignedRootRotation = alignedRot;
    outState.valid = true;
    return true;
}

bool BeginBlendOutFromRagdollUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                 const float blendDurationSeconds) {
    if (!std::isfinite(blendDurationSeconds) || !(blendDurationSeconds > 0.0F)) return false;
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Ragdoll3DComponentUVE>(entity)) {
        return false;
    }
    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(entity);
    if (!CaptureRagdollPoseSnapshotUVE(entityManager, entity, ragdoll.blendOutSnapshot)) return false;
    ragdoll.blendOutElapsedSeconds = 0.0F;
    ragdoll.blendOutDurationSeconds = blendDurationSeconds;
    ragdoll.mode = RagdollModeUVE::BlendingOut;
    return true;
}

bool StartRagdollEditorPreviewUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Ragdoll3DComponentUVE>(entity)) {
        return false;
    }
    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(entity);
    if (ragdoll.editorPreviewSimulating) return true;
    if (!entityManager.IsAliveUVE(ragdoll.skeleton) ||
        !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton)) {
        return false;
    }
    const Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton);
    ragdoll.editorPreviewBackupPose = skeleton.pose;
    ragdoll.editorPreviewBackupMode = ragdoll.mode;
    ragdoll.initialized = false;
    if (!InitializeRagdoll3DUVE(entityManager, entity)) {
        ragdoll.editorPreviewBackupPose.clear();
        return false;
    }
    ragdoll.mode = RagdollModeUVE::Simulating;
    ragdoll.editorPreviewSimulating = true;
    ragdoll.solver.WakeUVE();
    return true;
}

bool StopRagdollEditorPreviewUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Ragdoll3DComponentUVE>(entity)) {
        return false;
    }
    Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(entity);
    if (!ragdoll.editorPreviewSimulating) return false;
    if (entityManager.IsAliveUVE(ragdoll.skeleton) &&
        entityManager.HasComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton)) {
        Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton);
        skeleton.pose = std::move(ragdoll.editorPreviewBackupPose);
    }
    ragdoll.editorPreviewBackupPose.clear();
    ragdoll.mode = ragdoll.editorPreviewBackupMode;
    ragdoll.editorPreviewSimulating = false;
    ragdoll.initialized = false;
    ragdoll.pendingSeconds = 0.0F;
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
    if (!ragdoll.initialized && !InitializeRagdoll3DUVE(entityManager, entity)) return false;

    const std::size_t bodyCount = ragdoll.solver.GetBodyCountUVE();
    if (bodyCount == 0U) return false;

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

std::size_t UpdateRagdoll3DObjectsUVE(IEntityManagerUVE& entityManager, const float deltaTimeSeconds,
                                      const bool editorPreviewOnly) {
    if (!std::isfinite(deltaTimeSeconds) || !(deltaTimeSeconds > 0.0F)) return 0U;

    std::vector<EntityUVE> ragdollEntities;
    entityManager.ForEachUVE<Ragdoll3DComponentUVE>(
        [&ragdollEntities, editorPreviewOnly](const EntityUVE entity, Ragdoll3DComponentUVE& ragdoll) {
            if (!ragdoll.enabled) return;
            if (editorPreviewOnly && !ragdoll.editorPreviewSimulating) return;
            if (ragdollEntities.size() < kMaximumRagdollEntitiesPerUpdateUVE) {
                ragdollEntities.push_back(entity);
            }
        });
    if (ragdollEntities.empty()) return 0U;

    const std::vector<Physics::Detail::ColliderWorldAabbUVE> colliders =
        Physics::Detail::BuildColliderWorldAabbCacheUVE(entityManager);

    // Collect dynamic Rigid3D motion & inverse mass once per frame for moving-body waking and two-way pushback.
    std::vector<Physics::RagdollColliderMotionUVE> colliderMotions(colliders.size());
    for (std::size_t i = 0U; i < colliders.size(); ++i) {
        const EntityUVE colEntity = colliders[i].entity;
        if (!entityManager.IsAliveUVE(colEntity) || !entityManager.HasComponentUVE<Rigid3DComponentUVE>(colEntity)) {
            continue;
        }
        const Rigid3DComponentUVE& rigid = entityManager.GetComponentUVE<Rigid3DComponentUVE>(colEntity);
        const float invMass = ResolveRigid3DInverseMassUVE(rigid);
        Physics::RagdollColliderMotionUVE motion;
        motion.linearVelocity = rigid.velocity;
        motion.angularVelocity = rigid.angularVelocity;
        motion.worldCenter = colliders[i].worldAabb.GetCenterUVE();
        motion.inverseMass = invMass;
        motion.isDynamic = !rigid.isKinematic && invMass > 0.0F;
        colliderMotions[i] = motion;
    }

    std::vector<std::size_t> broadphaseCandidates;
    broadphaseCandidates.reserve(32U);
    std::size_t advancedCount = 0U;

    for (const EntityUVE entity : ragdollEntities) {
        if (!entityManager.IsAliveUVE(entity) || !entityManager.HasComponentUVE<Ragdoll3DComponentUVE>(entity) ||
            entityManager.HasComponentUVE<DisabledComponentUVE>(entity)) {
            continue;
        }
        Ragdoll3DComponentUVE& ragdoll = entityManager.GetComponentUVE<Ragdoll3DComponentUVE>(entity);
        if (!ragdoll.enabled) continue;
        if (editorPreviewOnly && !ragdoll.editorPreviewSimulating) continue;
        if (!ragdoll.initialized && !InitializeRagdoll3DUVE(entityManager, entity)) continue;
        if (!entityManager.IsAliveUVE(ragdoll.skeleton) ||
            !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton)) {
            ragdoll.initialized = false;
            continue;
        }
        Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(ragdoll.skeleton);
        ObjectWorldFrameUVE skeletonFrame;
        if (!ResolveSkeletonWorldFrameUVE(entityManager, ragdoll.skeleton, skeletonFrame)) continue;

        // ---- Mode 1: Kinematic (animation controls bodies; record velocities for activation) ----
        if (ragdoll.mode == RagdollModeUVE::Kinematic) {
            SyncBodiesFromAnimatedPoseUVE(ragdoll, skeleton, skeletonFrame, deltaTimeSeconds,
                                          /*kinematicOnlyBodies=*/false);
            continue;
        }

        // ---- Mode 2: BlendingOut (smooth Pose Snapshot -> Get-Up / live animation blend) ----
        if (ragdoll.mode == RagdollModeUVE::BlendingOut) {
            EnsureSkeletonPoseUVE(skeleton);
            ragdoll.blendOutElapsedSeconds += deltaTimeSeconds;
            const float duration = std::max(1.0e-4F, ragdoll.blendOutDurationSeconds);
            const float t = std::clamp(ragdoll.blendOutElapsedSeconds / duration, 0.0F, 1.0F);
            // Smoothstep easing for natural organic get-up transition.
            const float smoothT = t * t * (3.0F - 2.0F * t);
            if (ragdoll.blendOutSnapshot.valid) {
                const std::size_t count = std::min(skeleton.pose.size(), ragdoll.blendOutSnapshot.bonePoses.size());
                for (std::size_t i = 0U; i < count; ++i) {
                    skeleton.pose[i].position =
                        LerpVectorUVE(ragdoll.blendOutSnapshot.bonePoses[i].position, skeleton.pose[i].position, smoothT);
                    skeleton.pose[i].rotation =
                        SlerpQuaternionUVE(ragdoll.blendOutSnapshot.bonePoses[i].rotation, skeleton.pose[i].rotation,
                                           smoothT);
                }
            }
            ++advancedCount;
            if (t >= 1.0F) {
                ragdoll.mode = RagdollModeUVE::Kinematic;
                ragdoll.blendOutSnapshot.valid = false;
            }
            continue;
        }

        // ---- Mode 3 & 4: Simulating (Passive) or Active (PD Motors) ----
        bool animatedTargetChanged = false;
        UpdatePartialAndMotorTargetsUVE(ragdoll, skeleton, skeletonFrame, deltaTimeSeconds, animatedTargetChanged);

        Physics::RagdollStepSettingsUVE effectiveStepSettings{};
        std::uint32_t maxSubsteps = Physics::kMaximumRagdollSubstepsUVE;
        bool dynamicPushback = ragdoll.enableDynamicPushback;
        ResolveEffectiveStepSettingsUVE(ragdoll, effectiveStepSettings, maxSubsteps, dynamicPushback);

        Physics::RagdollCollisionFilterUVE filter = ragdoll.filter;
        if (filter.ignoreEntity == kInvalidEntityUVE) filter.ignoreEntity = ragdoll.skeleton;

        // Broadphase pre-filter once per frame (expanded by max expected displacement during frame).
        const float expandMargin = std::clamp(deltaTimeSeconds * 15.0F + 0.15F, 0.15F, 1.5F);
        Physics::BuildRagdollBroadphaseCandidatesUVE(ragdoll.solver, colliders, filter, expandMargin,
                                                     broadphaseCandidates);

        std::array<Physics::RagdollContactUVE, Physics::kMaximumRagdollContactsUVE> contacts{};

        // BUG #3 FIX (Smart Sleep): NEVER wake a sleeping corpse on resting static floor contacts!
        // Wake ONLY on moving dynamic/kinematic collider hits, lost floor support, or active motor target change.
        if (ragdoll.solver.IsSleepingUVE()) {
            const Physics::RagdollContactStatsUVE probeStats =
                broadphaseCandidates.empty()
                    ? Physics::RagdollContactStatsUVE{}
                    : Physics::BuildRagdollContactsUVE(ragdoll.solver, colliders, filter,
                                                       std::span<Physics::RagdollContactUVE>{contacts},
                                                       colliderMotions, broadphaseCandidates);
            const bool lostFloor = ragdoll.hadFloorSupport && !probeStats.hasFloorSupport;
            const bool activeMotorMoved = ragdoll.mode == RagdollModeUVE::Active && animatedTargetChanged;
            if (probeStats.hasMovingColliderContact || lostFloor || activeMotorMoved) {
                ragdoll.solver.WakeUVE();
            } else {
                ragdoll.hadFloorSupport = probeStats.hasFloorSupport;
                WriteRagdollPoseToSkeletonUVE(entityManager, ragdoll, skeleton, skeletonFrame);
                continue;
            }
        }

        ragdoll.pendingSeconds += deltaTimeSeconds;
        std::size_t substeps = 0U;
        const float stepSeconds = Physics::kRagdollFixedStepSecondsUVE;
        bool lastHadFloorSupport = ragdoll.hadFloorSupport;

        while (ragdoll.pendingSeconds >= stepSeconds && substeps < maxSubsteps) {
            // BUG #4 FIX: Refresh world contacts PER SUBSTEP using the fast broadphase candidate list!
            Physics::RagdollContactStatsUVE substepStats{};
            if (!broadphaseCandidates.empty()) {
                substepStats = Physics::BuildRagdollContactsUVE(ragdoll.solver, colliders, filter,
                                                                std::span<Physics::RagdollContactUVE>{contacts},
                                                                colliderMotions, broadphaseCandidates);
            }
            lastHadFloorSupport = substepStats.hasFloorSupport;

            const std::span<const Physics::RagdollContactUVE> contactSpan{
                contacts.data(), substepStats.generatedContactCount};
            ragdoll.lastReport =
                ragdoll.solver.StepUVE(ragdoll.gravity, stepSeconds, contactSpan, effectiveStepSettings);

            if (dynamicPushback && !ragdoll.solver.GetLastContactFeedbackUVE().empty()) {
                ApplyDynamicRigid3DPushbackUVE(entityManager, colliders, colliderMotions,
                                               ragdoll.solver.GetLastContactFeedbackUVE());
            }

            ragdoll.pendingSeconds -= stepSeconds;
            ++substeps;
        }
        ragdoll.hadFloorSupport = lastHadFloorSupport;
        if (substeps > 0U) ++advancedCount;
        ragdoll.pendingSeconds =
            std::min(ragdoll.pendingSeconds, stepSeconds * static_cast<float>(maxSubsteps));

        WriteRagdollPoseToSkeletonUVE(entityManager, ragdoll, skeleton, skeletonFrame);
    }
    return advancedCount;
}

} // namespace UVE::Scene
