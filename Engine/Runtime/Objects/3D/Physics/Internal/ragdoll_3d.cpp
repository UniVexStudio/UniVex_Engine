// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ragdoll_3d.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "character_controller_component.h"
#include "collider_component.h"
#include "disabled_component.h"
#include "hierarchy_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"
#include "skeleton_3d.h"
#include "collider_world_aabb_cache.h"
#include "retarget_humanoid.h"
#include "retarget_match.h"

namespace UVE::Scene {
namespace {

constexpr float kDirectionEpsilon = 1.0e-6F;
constexpr float kMinimumBoneLength = 0.02F;
constexpr std::size_t kMaximumRagdollEntitiesPerUpdate = 64U;
constexpr float kMaxInheritedLinearSpeed = 25.0F;
constexpr float kMaxInheritedAngularSpeed = 30.0F;
constexpr float kMaxDynamicPushbackImpulse = 120.0F;

/// Hinge ranges follow the solver's twist sign about the axis `forward x limb`. With that axis a positive
/// twist folds an elbow forward and a knee backward, so both hinge rows read [-5, 150] (a hinge never bends
/// the other way). Measured by kicking forearms and shins in zero gravity; see ragdoll_3d_tests.cpp.
/// Expanded humanoid profile table covering standard DCC & character rig naming conventions.
/// Ordered most-specific-first; all patterns strictly lowercase.
constexpr std::array<HumanoidRagdollProfileEntry, 50U> kHumanoidProfile{{
    {"lowerarm", 1.5F, 0.045F, 0.10F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"lower_arm", 1.5F, 0.045F, 0.10F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"forearm", 1.5F, 0.045F, 0.10F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"fore_arm", 1.5F, 0.045F, 0.10F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"arm_lower", 1.5F, 0.045F, 0.10F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"elbow", 1.5F, 0.045F, 0.10F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"upperarm", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"upper_arm", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"arm_upper", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"bicep", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"leftarm", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"rightarm", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"l_arm", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"r_arm", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"arm_l", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"arm_r", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"arm.l", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"arm.r", 2.5F, 0.055F, 0.12F, 0.05F, 3.0F, 90.0F, 90.0F, 60.0F, false, -60.0F, 60.0F},
    {"clavicle", 1.5F, 0.05F, 0.08F, 0.05F, 2.5F, 20.0F, 20.0F, 30.0F, false, -30.0F, 30.0F},
    {"collar", 1.5F, 0.05F, 0.08F, 0.05F, 2.5F, 20.0F, 20.0F, 30.0F, false, -30.0F, 30.0F},
    {"shoulder", 1.5F, 0.05F, 0.08F, 0.05F, 2.5F, 20.0F, 20.0F, 30.0F, false, -30.0F, 30.0F},
    {"hand", 0.5F, 0.035F, 0.04F, 0.10F, 4.0F, 60.0F, 60.0F, 40.0F, false, -40.0F, 40.0F},
    {"wrist", 0.5F, 0.035F, 0.04F, 0.10F, 4.0F, 60.0F, 60.0F, 40.0F, false, -40.0F, 40.0F},
    {"thigh", 9.0F, 0.08F, 0.16F, 0.05F, 3.0F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"femur", 9.0F, 0.08F, 0.16F, 0.05F, 3.0F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"upperleg", 9.0F, 0.08F, 0.16F, 0.05F, 3.0F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"upper_leg", 9.0F, 0.08F, 0.16F, 0.05F, 3.0F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"upleg", 9.0F, 0.08F, 0.16F, 0.05F, 3.0F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"up_leg", 9.0F, 0.08F, 0.16F, 0.05F, 3.0F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"leg_upper", 9.0F, 0.08F, 0.16F, 0.05F, 3.0F, 70.0F, 70.0F, 40.0F, false, -40.0F, 40.0F},
    {"calf", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"shin", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"tibia", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"lowerleg", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"lower_leg", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"leg_lower", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"leftleg", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"rightleg", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"l_leg", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"r_leg", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"leg_l", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"leg_r", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"leg.l", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"leg.r", 3.5F, 0.06F, 0.16F, 0.05F, 3.5F, 5.0F, 5.0F, 150.0F, true, -5.0F, 150.0F},
    {"foot", 1.0F, 0.04F, 0.06F, 0.10F, 4.0F, 5.0F, 5.0F, 40.0F, true, -40.0F, 40.0F},
    {"ankle", 1.0F, 0.04F, 0.06F, 0.10F, 4.0F, 5.0F, 5.0F, 40.0F, true, -40.0F, 40.0F},
    {"neck", 1.5F, 0.05F, 0.04F, 0.05F, 2.5F, 30.0F, 30.0F, 40.0F, false, -40.0F, 40.0F},
    {"head", 5.0F, 0.09F, 0.05F, 0.05F, 2.5F, 30.0F, 30.0F, 30.0F, false, -30.0F, 30.0F},
    {"spine", 6.0F, 0.10F, 0.08F, 0.05F, 2.0F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
    {"abdomen", 6.0F, 0.10F, 0.08F, 0.05F, 2.0F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
}};

/// Separate torso aliases checked after `kHumanoidProfile` so chest/torso rigs auto-build cleanly.
constexpr std::array<HumanoidRagdollProfileEntry, 5U> kHumanoidTorsoFallbackProfile{{
    {"upperchest", 6.0F, 0.11F, 0.09F, 0.05F, 2.0F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
    {"upper_chest", 6.0F, 0.11F, 0.09F, 0.05F, 2.0F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
    {"chest", 6.0F, 0.11F, 0.09F, 0.05F, 2.0F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
    {"torso", 6.0F, 0.11F, 0.09F, 0.05F, 2.0F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
    {"waist", 6.0F, 0.10F, 0.08F, 0.05F, 2.0F, 25.0F, 25.0F, 20.0F, false, -20.0F, 20.0F},
}};

/// The root row is separate because `pelvis` and `hips`/`hip` win over `spine`.
constexpr std::array<HumanoidRagdollProfileEntry, 3U> kHumanoidRootProfile{{
    {"pelvis", 12.0F, 0.12F, 0.08F, 0.05F, 2.0F, 45.0F, 45.0F, 35.0F, false, -35.0F, 35.0F},
    {"hips", 12.0F, 0.12F, 0.08F, 0.05F, 2.0F, 45.0F, 45.0F, 35.0F, false, -35.0F, 35.0F},
    {"hip", 12.0F, 0.12F, 0.08F, 0.05F, 2.0F, 45.0F, 45.0F, 35.0F, false, -35.0F, 35.0F},
}};

/// Fingers and toes, matched only against the engine's canonical bone names (never against a rig's own
/// names, where "index" or "ring" are ordinary words). They are light, so they get strong damping and
/// ball joints with short cones; the joint filter keeps bodies this light from colliding with the body.
constexpr std::array<HumanoidRagdollProfileEntry, 6U> kHumanoidDetailProfile{{
    {"thumb", 0.040F, 0.010F, 0.014F, 0.20F, 3.0F, 50.0F, 50.0F, 15.0F, false, -15.0F, 15.0F},
    {"index", 0.030F, 0.008F, 0.012F, 0.20F, 3.0F, 45.0F, 45.0F, 10.0F, false, -10.0F, 10.0F},
    {"middle", 0.035F, 0.009F, 0.012F, 0.20F, 3.0F, 45.0F, 45.0F, 10.0F, false, -10.0F, 10.0F},
    {"ring", 0.030F, 0.008F, 0.012F, 0.20F, 3.0F, 45.0F, 45.0F, 10.0F, false, -10.0F, 10.0F},
    {"pinky", 0.020F, 0.007F, 0.010F, 0.20F, 3.0F, 45.0F, 45.0F, 10.0F, false, -10.0F, 10.0F},
    {"toe", 0.150F, 0.016F, 0.020F, 0.10F, 3.0F, 30.0F, 30.0F, 10.0F, false, -10.0F, 10.0F},
}};

/// Bodies at or below this mass are detail (fingers, toes): they never collide with other bodies of the
/// same ragdoll, only with the world, so a clenched hand does not fight the wrist it hangs from, and they
/// do not take part in the solver's sleep decision.
constexpr float kDetailBodyMassKilograms = Physics::kRagdollDetailBodyMass;

[[nodiscard]] std::string ToLowerAscii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const char character : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

[[nodiscard]] const HumanoidRagdollProfileEntry* FindProfileEntry(const std::string_view boneName) noexcept {
    const std::string lowered = ToLowerAscii(boneName);
    for (const HumanoidRagdollProfileEntry& entry : kHumanoidRootProfile) {
        if (lowered.find(entry.pattern) != std::string::npos) return &entry;
    }
    for (const HumanoidRagdollProfileEntry& entry : kHumanoidProfile) {
        if (lowered.find(entry.pattern) != std::string::npos) return &entry;
    }
    for (const HumanoidRagdollProfileEntry& entry : kHumanoidTorsoFallbackProfile) {
        if (lowered.find(entry.pattern) != std::string::npos) return &entry;
    }
    return nullptr;
}

[[nodiscard]] const HumanoidRagdollProfileEntry* FindCanonicalProfileEntry(
    const std::string_view loweredCanonicalName) noexcept {
    for (const HumanoidRagdollProfileEntry& entry : kHumanoidDetailProfile) {
        if (loweredCanonicalName.find(entry.pattern) != std::string_view::npos) return &entry;
    }
    for (const HumanoidRagdollProfileEntry& entry : kHumanoidRootProfile) {
        if (loweredCanonicalName.find(entry.pattern) != std::string_view::npos) return &entry;
    }
    for (const HumanoidRagdollProfileEntry& entry : kHumanoidProfile) {
        if (loweredCanonicalName.find(entry.pattern) != std::string_view::npos) return &entry;
    }
    for (const HumanoidRagdollProfileEntry& entry : kHumanoidTorsoFallbackProfile) {
        if (loweredCanonicalName.find(entry.pattern) != std::string_view::npos) return &entry;
    }
    return nullptr;
}

[[nodiscard]] bool IsMidlineBoneName(const std::string_view boneName) {
    const std::string lowered = ToLowerAscii(boneName);
    return lowered.find("spine") != std::string::npos || lowered.find("chest") != std::string::npos ||
           lowered.find("torso") != std::string::npos || lowered.find("neck") != std::string::npos ||
           lowered.find("head") != std::string::npos;
}

[[nodiscard]] bool IsLeftBoneName(const std::string_view boneName) {
    const std::string lowered = ToLowerAscii(boneName);
    return lowered.find("left") != std::string::npos || lowered.ends_with("_l") || lowered.ends_with(".l");
}

[[nodiscard]] bool IsRightBoneName(const std::string_view boneName) {
    const std::string lowered = ToLowerAscii(boneName);
    return lowered.find("right") != std::string::npos || lowered.ends_with("_r") || lowered.ends_with(".r");
}

[[nodiscard]] Math::Vector3 SafeNormalize(const Math::Vector3& v,
                                                const Math::Vector3& fallback) noexcept {
    const float lenSq = Math::LengthSquared(v);
    if (!std::isfinite(lenSq) || !(lenSq > kDirectionEpsilon)) return fallback;
    return v * (1.0F / std::sqrt(lenSq));
}

[[nodiscard]] Math::Vector3 ClampVectorLength(const Math::Vector3& v, const float maxLength) noexcept {
    if (!Math::IsFinite(v) || !(maxLength > 0.0F)) return Math::Vector3{};
    const float lenSq = Math::LengthSquared(v);
    if (lenSq <= maxLength * maxLength) return v;
    return v * (maxLength / std::sqrt(lenSq));
}

[[nodiscard]] Math::Vector3 LerpVector(const Math::Vector3& a, const Math::Vector3& b,
                                             const float t) noexcept {
    const float alpha = std::clamp(t, 0.0F, 1.0F);
    return a + (b - a) * alpha;
}

[[nodiscard]] Math::Quaternion SlerpQuaternion(const Math::Quaternion& a, const Math::Quaternion& b,
                                                     const float t) noexcept {
    const float alpha = std::clamp(t, 0.0F, 1.0F);
    if (alpha <= 0.0F) return a;
    if (alpha >= 1.0F) return b;
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Math::Quaternion target = b;
    if (dot < 0.0F) {
        dot = -dot;
        target = Math::Quaternion{-b.x, -b.y, -b.z, -b.w};
    }
    Math::Quaternion out{};
    if (dot > 0.9995F) {
        out = Math::Quaternion{
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
        out = Math::Quaternion{
            a.x * wA + target.x * wB,
            a.y * wA + target.y * wB,
            a.z * wA + target.z * wB,
            a.w * wA + target.w * wB,
        };
    }
    Math::Quaternion normalized;
    return Math::TryNormalize(out, normalized) ? normalized : b;
}

[[nodiscard]] Math::Vector3 ComputeAngularVelocityBetween(const Math::Quaternion& from,
                                                                const Math::Quaternion& to,
                                                                const float dt) noexcept {
    if (!(dt > 1.0e-5F)) return Math::Vector3{};
    Math::Quaternion delta = Math::Multiply(to, Math::Conjugate(from));
    if (!Math::IsFinite(delta)) return Math::Vector3{};
    if (delta.w < 0.0F) {
        delta = Math::Quaternion{-delta.x, -delta.y, -delta.z, -delta.w};
    }
    const Math::Vector3 imag{delta.x, delta.y, delta.z};
    const float sinHalf = std::sqrt(Math::LengthSquared(imag));
    Math::Vector3 rotVec{};
    if (sinHalf > 1.0e-6F) {
        const float angle = 2.0F * std::atan2(sinHalf, std::clamp(delta.w, -1.0F, 1.0F));
        rotVec = imag * (angle / sinHalf);
    } else {
        rotVec = imag * 2.0F;
    }
    return ClampVectorLength(rotVec * (1.0F / dt), kMaxInheritedAngularSpeed);
}

/// Shortest-arc rotation taking the capsule's local +Y onto `axis`.
[[nodiscard]] Math::Quaternion MakeCapsuleRotationFromAxis(const Math::Vector3& axis) noexcept {
    const float lengthSquared = Math::LengthSquared(axis);
    if (!std::isfinite(lengthSquared) || !(lengthSquared > kDirectionEpsilon)) return Math::Quaternion{};
    const Math::Vector3 normalised = axis * (1.0F / std::sqrt(lengthSquared));
    const Math::Vector3 from{0.0F, 1.0F, 0.0F};
    const float cosine = Math::Dot(from, normalised);
    if (cosine >= 1.0F - 1.0e-6F) return Math::Quaternion{};

    Math::Vector3 rotationAxis = Math::Cross(from, normalised);
    const float sine = std::sqrt(std::max(0.0F, 1.0F - cosine * cosine));
    const float rotationLengthSquared = Math::LengthSquared(rotationAxis);
    if (!(rotationLengthSquared > kDirectionEpsilon)) {
        rotationAxis = Math::Vector3{1.0F, 0.0F, 0.0F};
    } else {
        rotationAxis = rotationAxis * (1.0F / std::sqrt(rotationLengthSquared));
    }
    Math::Quaternion rotation;
    if (!Math::TryMakeAxisAngle(rotationAxis, std::atan2(sine, cosine), rotation)) {
        return Math::Quaternion{};
    }
    return rotation;
}

[[nodiscard]] std::uint32_t BoneDepth(const std::vector<SkeletonBone>& bones,
                                         const std::uint32_t boneIndex) noexcept {
    std::uint32_t depth = 0U;
    std::int32_t current = static_cast<std::int32_t>(boneIndex);
    constexpr std::uint32_t kMaximumDepth = static_cast<std::uint32_t>(kMaximumSkeletonBones) + 1U;
    while (current >= 0 && depth < kMaximumDepth) {
        const std::size_t index = static_cast<std::size_t>(current);
        if (index >= bones.size()) break;
        current = bones[index].parentIndex;
        ++depth;
    }
    return depth;
}

[[nodiscard]] bool IsBoneDescendantOrSelf(const std::vector<SkeletonBone>& bones,
                                             const std::uint32_t boneIndex,
                                             const std::uint32_t ancestorBoneIndex) noexcept {
    if (ancestorBoneIndex == kInvalidSkeletonBoneIndex) return true;
    std::int32_t current = static_cast<std::int32_t>(boneIndex);
    constexpr std::uint32_t kMaximumDepth = static_cast<std::uint32_t>(kMaximumSkeletonBones) + 1U;
    std::uint32_t hops = 0U;
    while (current >= 0 && hops < kMaximumDepth) {
        const std::uint32_t idx = static_cast<std::uint32_t>(current);
        if (idx == ancestorBoneIndex) return true;
        if (idx >= bones.size()) break;
        current = bones[idx].parentIndex;
        ++hops;
    }
    return false;
}

void EnsureSkeletonPose(Skeleton3DComponent& skeleton) {
    const std::size_t boneCount = skeleton.bones.size();
    if (skeleton.pose.size() >= boneCount) return;
    const std::size_t filled = skeleton.pose.size();
    skeleton.pose.resize(boneCount);
    for (std::size_t index = filled; index < boneCount; ++index) {
        skeleton.pose[index] = SkeletonBonePose{skeleton.bones[index].localPosition,
                                                   skeleton.bones[index].localRotation,
                                                   skeleton.bones[index].localScale};
    }
}

[[nodiscard]] bool ResolveSkeletonWorldFrame(IEntityManager& entityManager, const Entity entity,
                                                ObjectWorldFrame& outFrame) {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<WorldTransformComponent>(entity)) {
        return false;
    }
    const WorldTransformComponent& world = entityManager.GetComponent<WorldTransformComponent>(entity);
    outFrame.position = world.worldPosition;
    outFrame.rotation = world.worldRotation;
    outFrame.scale = world.worldScale;
    if (!IsFinite3DObjectVector(outFrame.position) || !Math::IsFinite(outFrame.rotation) ||
        !IsFinite3DObjectVector(outFrame.scale)) {
        return false;
    }
    return true;
}

[[nodiscard]] std::uint32_t FindSimulatedAncestor(const std::vector<SkeletonBone>& bones,
                                                     const std::vector<bool>& simulated,
                                                     const std::uint32_t boneIndex) noexcept {
    std::int32_t current = bones.empty() ? -1 : bones[boneIndex].parentIndex;
    constexpr std::uint32_t kMaximumDepth = static_cast<std::uint32_t>(kMaximumSkeletonBones) + 1U;
    std::uint32_t hops = 0U;
    while (current >= 0 && hops < kMaximumDepth) {
        const std::size_t index = static_cast<std::size_t>(current);
        if (index >= bones.size()) return kInvalidSkeletonBoneIndex;
        if (simulated[index]) return static_cast<std::uint32_t>(index);
        current = bones[index].parentIndex;
        ++hops;
    }
    return kInvalidSkeletonBoneIndex;
}

[[nodiscard]] Math::Vector3 ClosestAxisPoint(const Physics::RagdollBody& body,
                                                   const Math::Vector3& point) noexcept {
    const Math::Vector3 axis = Math::RotateVector(body.rotation, Math::Vector3{0.0F, 1.0F, 0.0F});
    const float halfHeight = std::max(0.0F, body.halfHeight);
    const float projection = Math::Dot(point - body.position, axis);
    const float clamped = std::clamp(projection, -halfHeight, halfHeight);
    return body.position + axis * clamped;
}

/// Picks the anatomically primary child of `boneIndex` for multi-child bones (e.g. Pelvis chooses
/// Spine over LeftThigh/RightThigh; Chest chooses Neck/Head over Clavicle/UpperArm).
[[nodiscard]] std::uint32_t FindPrimaryChild(const std::vector<SkeletonBone>& bones,
                                                const std::uint32_t boneIndex) noexcept {
    std::uint32_t firstChild = kInvalidSkeletonBoneIndex;
    std::uint32_t midlineChild = kInvalidSkeletonBoneIndex;
    for (std::size_t index = 0U; index < bones.size(); ++index) {
        if (bones[index].parentIndex != static_cast<std::int32_t>(boneIndex)) continue;
        const std::uint32_t childIdx = static_cast<std::uint32_t>(index);
        if (firstChild == kInvalidSkeletonBoneIndex) firstChild = childIdx;
        if (midlineChild == kInvalidSkeletonBoneIndex && IsMidlineBoneName(bones[index].name)) {
            midlineChild = childIdx;
        }
    }
    return midlineChild != kInvalidSkeletonBoneIndex ? midlineChild : firstChild;
}

/// Detects the character's forward direction in world space from left/right limb symmetry so
/// elbow & knee hinge axes bend anatomically on both +Z-facing (glTF/Mixamo/VRM) and -Z-facing rigs.
[[nodiscard]] Math::Vector3 DetectCharacterForwardWorld(const Skeleton3DComponent& skeleton,
                                                              const ObjectWorldFrame& skeletonFrame,
                                                              const std::vector<ObjectWorldFrame>& bindFrames,
                                                              const std::vector<bool>& bindResolved) noexcept {
    const Math::Vector3 skelRight =
        SafeNormalize(Math::RotateVector(skeletonFrame.rotation, Math::Vector3{1.0F, 0.0F, 0.0F}),
                         Math::Vector3{1.0F, 0.0F, 0.0F});
    const Math::Vector3 skelUp =
        SafeNormalize(Math::RotateVector(skeletonFrame.rotation, Math::Vector3{0.0F, 1.0F, 0.0F}),
                         Math::Vector3{0.0F, 1.0F, 0.0F});
    const Math::Vector3 defaultForward =
        SafeNormalize(Math::RotateVector(skeletonFrame.rotation, Math::Vector3{0.0F, 0.0F, 1.0F}),
                         Math::Vector3{0.0F, 0.0F, 1.0F});

    Math::Vector3 leftSum{};
    Math::Vector3 rightSum{};
    std::size_t leftCount = 0U;
    std::size_t rightCount = 0U;
    for (std::size_t i = 0U; i < skeleton.bones.size(); ++i) {
        if (!bindResolved[i]) continue;
        if (IsLeftBoneName(skeleton.bones[i].name)) {
            leftSum += bindFrames[i].position - skeletonFrame.position;
            ++leftCount;
        } else if (IsRightBoneName(skeleton.bones[i].name)) {
            rightSum += bindFrames[i].position - skeletonFrame.position;
            ++rightCount;
        }
    }
    if (leftCount > 0U && rightCount > 0U) {
        const Math::Vector3 leftAvg = leftSum * (1.0F / static_cast<float>(leftCount));
        const Math::Vector3 rightAvg = rightSum * (1.0F / static_cast<float>(rightCount));
        const Math::Vector3 rightToLeft = leftAvg - rightAvg;
        // Forward is where the character faces: with up and the left side known, `left = up x forward`,
        // so `forward = left x up`. A character facing +Z has its left at +X (X x Y = Z). The product used
        // to be reversed, which made this branch disagree with the single-sided one below; the hinge axis
        // (Cross(forward, limb)) is chosen for the true facing direction.
        const Math::Vector3 forwardCand = Math::Cross(rightToLeft, skelUp);
        if (Math::LengthSquared(forwardCand) > kDirectionEpsilon) {
            return SafeNormalize(forwardCand, defaultForward);
        }
    } else if (leftCount > 0U) {
        const Math::Vector3 leftAvg = leftSum * (1.0F / static_cast<float>(leftCount));
        const float side = Math::Dot(leftAvg, skelRight);
        if (std::fabs(side) > 0.02F) {
            return side > 0.0F ? defaultForward : (defaultForward * -1.0F);
        }
    }
    return defaultForward;
}

void ResolveEffectiveStepSettings(const Ragdoll3DComponent& ragdoll,
                                     Physics::RagdollStepSettings& outStepSettings,
                                     std::uint32_t& outMaxSubsteps, bool& outDynamicPushback) noexcept {
    outStepSettings = ragdoll.stepSettings;
    outMaxSubsteps = std::clamp<std::uint32_t>(ragdoll.maxSubstepsPerFrame, 1U, Physics::kMaximumRagdollSubsteps);
    outDynamicPushback = ragdoll.enableDynamicPushback;

    switch (ragdoll.qualityPreset) {
    case RagdollQualityPreset::High:
        outStepSettings.enableSelfCollision = ragdoll.enableSelfCollision;
        outStepSettings.stabilizeInertia = true;
        break;
    case RagdollQualityPreset::Balanced:
        outStepSettings.velocityIterations = std::min<std::size_t>(outStepSettings.velocityIterations, 10U);
        outStepSettings.positionIterations = std::min<std::size_t>(outStepSettings.positionIterations, 5U);
        outStepSettings.enableSelfCollision = ragdoll.enableSelfCollision;
        outStepSettings.stabilizeInertia = true;
        outMaxSubsteps = std::min<std::uint32_t>(outMaxSubsteps, 4U);
        break;
    case RagdollQualityPreset::Fast:
        outStepSettings.velocityIterations = std::min<std::size_t>(outStepSettings.velocityIterations, 6U);
        outStepSettings.positionIterations = std::min<std::size_t>(outStepSettings.positionIterations, 3U);
        outStepSettings.enableSelfCollision = false;
        outStepSettings.stabilizeInertia = true;
        outMaxSubsteps = std::min<std::uint32_t>(outMaxSubsteps, 2U);
        outDynamicPushback = false;
        break;
    case RagdollQualityPreset::Custom:
        outStepSettings.enableSelfCollision = ragdoll.enableSelfCollision;
        break;
    }
}

void SyncBodiesFromAnimatedPose(Ragdoll3DComponent& ragdoll, const Skeleton3DComponent& skeleton,
                                   const ObjectWorldFrame& skeletonFrame, const float deltaTimeSeconds,
                                   const bool kinematicOnlyBodies) {
    for (RagdollBoneBinding& binding : ragdoll.bindings) {
        if (binding.bodyIndex >= ragdoll.solver.GetBodyCount()) continue;
        if (binding.boneIndex >= skeleton.bones.size()) continue;
        Physics::RagdollBody& body = ragdoll.solver.GetBody(binding.bodyIndex);
        if (kinematicOnlyBodies && !body.kinematic) continue;

        ObjectWorldFrame boneFrame;
        if (!TryResolveSkeletonBoneWorldFrame(skeleton, binding.boneIndex, skeletonFrame, boneFrame)) continue;

        Math::Quaternion inverseOffset;
        if (!Math::TryInverse(binding.boneOffset, inverseOffset)) continue;
        Math::Quaternion bodyRot;
        if (!Math::TryNormalize(Math::Multiply(boneFrame.rotation, inverseOffset), bodyRot)) continue;
        const Math::Vector3 bodyPos =
            boneFrame.position - Math::RotateVector(bodyRot, binding.rootAnchorLocal);
        if (!Math::IsFinite(bodyPos)) continue;

        if (binding.hasPreviousWorldPose && deltaTimeSeconds > 1.0e-5F) {
            body.linearVelocity =
                ClampVectorLength((bodyPos - binding.prevWorldPosition) * (1.0F / deltaTimeSeconds),
                                     kMaxInheritedLinearSpeed);
            body.angularVelocity =
                ComputeAngularVelocityBetween(binding.prevWorldRotation, bodyRot, deltaTimeSeconds);
        } else {
            body.linearVelocity = Math::Vector3{};
            body.angularVelocity = Math::Vector3{};
        }
        body.position = bodyPos;
        body.rotation = bodyRot;
        binding.prevWorldPosition = bodyPos;
        binding.prevWorldRotation = bodyRot;
        binding.hasPreviousWorldPose = true;
    }
}

void UpdatePartialAndMotorTargets(Ragdoll3DComponent& ragdoll, const Skeleton3DComponent& skeleton,
                                     const ObjectWorldFrame& skeletonFrame, const float deltaTimeSeconds,
                                     bool& outAnimatedTargetChanged) {
    outAnimatedTargetChanged = false;
    std::uint32_t partialRootBone = kInvalidSkeletonBoneIndex;
    if (!ragdoll.partialRootBoneName.empty()) {
        static_cast<void>(TryFindSkeletonBoneIndex(skeleton, ragdoll.partialRootBoneName, partialRootBone));
    }

    const std::size_t bodyCount = ragdoll.solver.GetBodyCount();
    std::array<Math::Vector3, Physics::kMaximumRagdollBodies> targetBodyPos{};
    std::array<Math::Quaternion, Physics::kMaximumRagdollBodies> targetBodyRot{};
    std::array<bool, Physics::kMaximumRagdollBodies> hasTarget{};

    for (RagdollBoneBinding& binding : ragdoll.bindings) {
        if (binding.bodyIndex >= bodyCount || binding.boneIndex >= skeleton.bones.size()) continue;
        Physics::RagdollBody& body = ragdoll.solver.GetBody(binding.bodyIndex);

        const bool setupSim =
            binding.setupIndex < ragdoll.bones.size() ? ragdoll.bones[binding.setupIndex].simulate : true;
        const bool inSubtree = IsBoneDescendantOrSelf(skeleton.bones, binding.boneIndex, partialRootBone);
        body.kinematic = !(setupSim && inSubtree);

        ObjectWorldFrame boneFrame;
        if (TryResolveSkeletonBoneWorldFrame(skeleton, binding.boneIndex, skeletonFrame, boneFrame)) {
            Math::Quaternion invOffset;
            if (Math::TryInverse(binding.boneOffset, invOffset)) {
                Math::Quaternion desiredRot;
                if (Math::TryNormalize(Math::Multiply(boneFrame.rotation, invOffset), desiredRot)) {
                    const Math::Vector3 desiredPos =
                        boneFrame.position - Math::RotateVector(desiredRot, binding.rootAnchorLocal);
                    if (Math::IsFinite(desiredPos)) {
                        targetBodyPos[binding.bodyIndex] = desiredPos;
                        targetBodyRot[binding.bodyIndex] = desiredRot;
                        hasTarget[binding.bodyIndex] = true;
                        if (binding.hasPreviousWorldPose) {
                            const float posDeltaSq = Math::LengthSquared(desiredPos - binding.prevWorldPosition);
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
        if (ragdoll.mode == RagdollMode::Active && binding.isRootBody && !body.kinematic &&
            hasTarget[binding.bodyIndex]) {
            // A stagger keeps the pelvis near where the animation puts it - the body is hit, not dropped - so the
            // root is pulled toward its animated position at least this firmly while the stagger lasts.
            constexpr float kStaggerRootLinearStiffness = 900.0F;
            constexpr float kStaggerRootLinearDamping = 70.0F;
            constexpr float kStaggerRootMaxLinearForce = 3000.0F;
            const float rootLinearStiffness =
                ragdoll.staggering ? std::max(ragdoll.rootDriveLinearStiffness, kStaggerRootLinearStiffness)
                                   : ragdoll.rootDriveLinearStiffness;
            const float rootLinearDamping =
                ragdoll.staggering ? std::max(ragdoll.rootDriveLinearDamping, kStaggerRootLinearDamping)
                                   : ragdoll.rootDriveLinearDamping;
            const float rootMaxLinearForce =
                ragdoll.staggering ? std::max(ragdoll.rootDriveMaxLinearForce, kStaggerRootMaxLinearForce)
                                   : ragdoll.rootDriveMaxLinearForce;
            body.worldDrive.enabled = (rootMaxLinearForce > 0.0F &&
                                       (rootLinearStiffness > 0.0F || rootLinearDamping > 0.0F)) ||
                                      (ragdoll.rootDriveMaxAngularTorque > 0.0F &&
                                       (ragdoll.rootDriveAngularStiffness > 0.0F || ragdoll.rootDriveAngularDamping > 0.0F));
            body.worldDrive.targetPosition = targetBodyPos[binding.bodyIndex];
            body.worldDrive.targetRotation = targetBodyRot[binding.bodyIndex];
            body.worldDrive.linearStiffness = rootLinearStiffness;
            body.worldDrive.linearDamping = rootLinearDamping;
            body.worldDrive.maxLinearForce = rootMaxLinearForce;
            body.worldDrive.angularStiffness = ragdoll.rootDriveAngularStiffness;
            body.worldDrive.angularDamping = ragdoll.rootDriveAngularDamping;
            body.worldDrive.maxAngularTorque = ragdoll.rootDriveMaxAngularTorque;
            if (binding.hasPreviousWorldPose && deltaTimeSeconds > 1.0e-5F) {
                body.worldDrive.targetLinearVelocity = ClampVectorLength(
                    (targetBodyPos[binding.bodyIndex] - binding.prevWorldPosition) * (1.0F / deltaTimeSeconds),
                    kMaxInheritedLinearSpeed);
                body.worldDrive.targetAngularVelocity = ComputeAngularVelocityBetween(
                    binding.prevWorldRotation, targetBodyRot[binding.bodyIndex], deltaTimeSeconds);
            } else {
                body.worldDrive.targetLinearVelocity = Math::Vector3{};
                body.worldDrive.targetAngularVelocity = Math::Vector3{};
            }
        } else {
            body.worldDrive.enabled = false;
        }
    }

    // Move kinematic anchor bodies directly to their animated transforms (used in partial ragdoll!).
    SyncBodiesFromAnimatedPose(ragdoll, skeleton, skeletonFrame, deltaTimeSeconds, /*kinematicOnlyBodies=*/true);

    // Update joint PD motors for Active mode (and disable them in Passive Simulating mode).
    const std::size_t jointCount = ragdoll.solver.GetJointCount();
    for (std::size_t jointIndex = 0U; jointIndex < jointCount; ++jointIndex) {
        Physics::RagdollJoint& joint = ragdoll.solver.GetJoint(jointIndex);
        if (ragdoll.mode != RagdollMode::Active) {
            joint.motor.enabled = false;
            continue;
        }
        if (joint.firstBody >= bodyCount || joint.secondBody >= bodyCount ||
            !hasTarget[joint.firstBody] || !hasTarget[joint.secondBody]) {
            joint.motor.enabled = false;
            continue;
        }
        const Math::Quaternion animFirstFrame = Math::Multiply(targetBodyRot[joint.firstBody], joint.firstFrame);
        const Math::Quaternion animSecondFrame =
            Math::Multiply(targetBodyRot[joint.secondBody], joint.secondFrame);
        Math::Quaternion invAnimFirst;
        if (!Math::TryInverse(animFirstFrame, invAnimFirst)) {
            joint.motor.enabled = false;
            continue;
        }
        Math::Quaternion targetRel;
        if (!Math::TryNormalize(Math::Multiply(invAnimFirst, animSecondFrame), targetRel)) {
            targetRel = Math::Quaternion{};
        }

        float strengthScale = 1.0F;
        for (const RagdollBoneBinding& binding : ragdoll.bindings) {
            if (binding.bodyIndex == joint.secondBody && binding.setupIndex < ragdoll.bones.size()) {
                strengthScale = std::clamp(ragdoll.bones[binding.setupIndex].motorStrengthScale, 0.0F, 1.0F);
                break;
            }
        }

        joint.motor.enabled = true;
        joint.motor.targetRelativeRotation = targetRel;
        joint.motor.targetAngularVelocity = Math::Vector3{};
        joint.motor.angularStiffness = ragdoll.motorStiffness;
        joint.motor.angularDamping = ragdoll.motorDamping;
        joint.motor.maxTorque = ragdoll.maxMotorTorque;
        joint.motor.strengthScale = strengthScale;
    }

    // Track target pose history in Active mode as well.
    if (ragdoll.mode == RagdollMode::Active) {
        for (RagdollBoneBinding& binding : ragdoll.bindings) {
            if (binding.bodyIndex < bodyCount && hasTarget[binding.bodyIndex]) {
                binding.prevWorldPosition = targetBodyPos[binding.bodyIndex];
                binding.prevWorldRotation = targetBodyRot[binding.bodyIndex];
                binding.hasPreviousWorldPose = true;
            }
        }
    }
}

void WriteRagdollPoseToSkeleton(IEntityManager& entityManager, Ragdoll3DComponent& ragdoll,
                                   Skeleton3DComponent& skeleton, const ObjectWorldFrame& skeletonFrame) {
    EnsureSkeletonPose(skeleton);
    const float globalBlend =
        std::clamp(ragdoll.staggering ? ragdoll.effectiveBlend : ragdoll.physicsBlendWeight, 0.0F, 1.0F);
    if (!(globalBlend > 0.0F)) return;

    std::vector<RagdollBoneBinding> ordered = ragdoll.bindings;
    std::sort(ordered.begin(), ordered.end(),
              [](const RagdollBoneBinding& lhs, const RagdollBoneBinding& rhs) {
                  if (lhs.depth != rhs.depth) return lhs.depth < rhs.depth;
                  return lhs.boneIndex < rhs.boneIndex;
              });

    for (const RagdollBoneBinding& binding : ordered) {
        if (binding.boneIndex >= skeleton.bones.size()) continue;
        if (binding.bodyIndex >= ragdoll.solver.GetBodyCount()) continue;
        const Physics::RagdollBody& body = ragdoll.solver.GetBody(binding.bodyIndex);
        if (body.kinematic) continue;
        if (!Math::IsFinite(body.position) || !Math::IsFinite(body.rotation)) continue;

        const float boneBlend =
            binding.setupIndex < ragdoll.bones.size()
                ? std::clamp(ragdoll.bones[binding.setupIndex].blendWeight, 0.0F, 1.0F)
                : 1.0F;
        const float weight = globalBlend * boneBlend;
        if (!(weight > 0.0F)) continue;

        // Resolve the IMMEDIATE skeleton parent frame (not just simulated ancestor!) so both root
        // position and child rotations match the skeleton hierarchy cleanly.
        ObjectWorldFrame parentFrame = skeletonFrame;
        const std::int32_t skeletonParent = skeleton.bones[binding.boneIndex].parentIndex;
        if (skeletonParent >= 0 && static_cast<std::size_t>(skeletonParent) < skeleton.bones.size()) {
            ObjectWorldFrame resolvedParent;
            if (TryResolveSkeletonBoneWorldFrame(skeleton, static_cast<std::uint32_t>(skeletonParent),
                                                    skeletonFrame, resolvedParent)) {
                parentFrame = resolvedParent;
            }
        }

        Math::Quaternion inverseParent;
        if (!Math::TryInverse(parentFrame.rotation, inverseParent)) continue;

        const Math::Quaternion boneWorldRotation = Math::Multiply(body.rotation, binding.boneOffset);
        Math::Quaternion localRotation;
        if (!Math::TryNormalize(Math::Multiply(inverseParent, boneWorldRotation), localRotation)) {
            continue;
        }

        // BUG #1 FIX (Floating Corpse): Root/pelvis writes BOTH world position AND rotation back!
        // Non-root limbs remain rotation-only so bone lengths can never stretch.
        if (binding.isRootBody) {
            const Math::Vector3 boneWorldPosition =
                body.position + Math::RotateVector(body.rotation, binding.rootAnchorLocal);
            if (Math::IsFinite(boneWorldPosition)) {
                if (ragdoll.driveEntityTransform &&
                    entityManager.HasComponent<TransformComponent>(ragdoll.skeleton)) {
                    TransformComponent& transform =
                        entityManager.GetComponent<TransformComponent>(ragdoll.skeleton);
                    const Math::Vector3 targetEntityPos =
                        LerpVector(transform.localPosition, boneWorldPosition - skeleton.bones[binding.boneIndex].localPosition, weight);
                    if (Math::IsFinite(targetEntityPos)) {
                        transform.localPosition = targetEntityPos;
                        if (entityManager.HasComponent<WorldTransformComponent>(ragdoll.skeleton)) {
                            entityManager.GetComponent<WorldTransformComponent>(ragdoll.skeleton).dirty = true;
                        }
                    }
                } else {
                    const Math::Vector3 relWorld = boneWorldPosition - parentFrame.position;
                    Math::Vector3 localPos = Math::RotateVector(inverseParent, relWorld);
                    if (std::fabs(parentFrame.scale.x) > kDirectionEpsilon) localPos.x /= parentFrame.scale.x;
                    if (std::fabs(parentFrame.scale.y) > kDirectionEpsilon) localPos.y /= parentFrame.scale.y;
                    if (std::fabs(parentFrame.scale.z) > kDirectionEpsilon) localPos.z /= parentFrame.scale.z;
                    if (Math::IsFinite(localPos)) {
                        skeleton.pose[binding.boneIndex].position =
                            LerpVector(skeleton.pose[binding.boneIndex].position, localPos, weight);
                    }
                }
            }
        }

        skeleton.pose[binding.boneIndex].rotation =
            SlerpQuaternion(skeleton.pose[binding.boneIndex].rotation, localRotation, weight);
    }
}

void ApplyDynamicRigid3DPushback(IEntityManager& entityManager,
                                    const std::span<const Physics::Detail::ColliderWorldAabb> colliders,
                                    std::span<Physics::RagdollColliderMotion> colliderMotions,
                                    const std::span<const Physics::RagdollContactFeedback> feedbackList) {
    for (const Physics::RagdollContactFeedback& fb : feedbackList) {
        if (fb.colliderIndex >= colliders.size()) continue;
        const Entity target = colliders[fb.colliderIndex].entity;
        if (!entityManager.IsAlive(target) || !entityManager.HasComponent<Rigid3DComponent>(target)) continue;
        Rigid3DComponent& rigid = entityManager.GetComponent<Rigid3DComponent>(target);
        if (rigid.isKinematic) continue;
        const float invMass = ResolveRigid3DInverseMass(rigid);
        if (!(invMass > 0.0F)) continue;

        const Math::Vector3 pushImpulse = ClampVectorLength(fb.appliedImpulse * -1.0F, kMaxDynamicPushbackImpulse);
        ApplyRigid3DLinearImpulse(rigid, pushImpulse);

        // Optional angular impulse when the dynamic body has non-zero inverse inertia.
        if (Math::LengthSquared(rigid.inverseInertia) > 0.0F &&
            entityManager.HasComponent<WorldTransformComponent>(target)) {
            const WorldTransformComponent& world = entityManager.GetComponent<WorldTransformComponent>(target);
            const Math::Vector3 worldTorqueImpulse = Math::Cross(fb.worldPoint - world.worldPosition, pushImpulse);
            const Math::Vector3 localTorque =
                Math::RotateVector(Math::Conjugate(world.worldRotation), worldTorqueImpulse);
            const Math::Vector3 deltaOmega{
                localTorque.x * rigid.inverseInertia.x,
                localTorque.y * rigid.inverseInertia.y,
                localTorque.z * rigid.inverseInertia.z,
            };
            if (Math::IsFinite(deltaOmega)) {
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

std::span<const HumanoidRagdollProfileEntry> GetHumanoidRagdollProfile() noexcept {
    return std::span<const HumanoidRagdollProfileEntry>{kHumanoidProfile};
}

bool IsRagdoll3DComponentAuthoredValid(const Ragdoll3DComponent& value) noexcept {
    if (!IsFinite3DObjectVector(value.gravity) || !IsFinite3DObjectVector(value.initialVelocity)) {
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
    if (value.maxSubstepsPerFrame == 0U || value.maxSubstepsPerFrame > Physics::kMaximumRagdollSubsteps) {
        return false;
    }
    if (!std::isfinite(value.recoverDelaySeconds) || value.recoverDelaySeconds < 0.0F ||
        value.recoverDelaySeconds > 600.0F || !std::isfinite(value.recoverDurationSeconds) ||
        !(value.recoverDurationSeconds > 0.0F) || value.recoverDurationSeconds > 60.0F) {
        return false;
    }
    if (!std::isfinite(value.staggerImpulseThreshold) || value.staggerImpulseThreshold < 0.0F ||
        !std::isfinite(value.fallImpulseThreshold) || value.fallImpulseThreshold < value.staggerImpulseThreshold ||
        !std::isfinite(value.fallLandingSpeed) || value.fallLandingSpeed < 0.0F ||
        !std::isfinite(value.staggerBlendPeak) || value.staggerBlendPeak < 0.0F || value.staggerBlendPeak > 1.0F ||
        !std::isfinite(value.staggerRiseSeconds) || value.staggerRiseSeconds < 0.0F ||
        value.staggerRiseSeconds > 10.0F || !std::isfinite(value.staggerDecaySeconds) ||
        !(value.staggerDecaySeconds > 0.0F) || value.staggerDecaySeconds > 30.0F ||
        !std::isfinite(value.staggerFallTiltDegrees) || !(value.staggerFallTiltDegrees > 0.0F) ||
        value.staggerFallTiltDegrees > 180.0F) {
        return false;
    }
    for (const RagdollBoneSetup& bone : value.bones) {
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

bool IsRagdoll3DComponentValid(const Ragdoll3DComponent& value) noexcept {
    if (value.skeleton == kInvalidEntity) return false;
    return IsRagdoll3DComponentAuthoredValid(value);
}

bool IsRagdoll3DObjectDefinitionValid(const Ragdoll3DObjectDefinition& value) noexcept {
    return IsRagdoll3DComponentAuthoredValid(value.ragdoll);
}

void ApplyRagdoll3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                       const Ragdoll3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, Ragdoll3DObjectDefinition::defaultName);
    entityManager.AddComponent<Ragdoll3DComponent>(entity, value.ragdoll);
}

void ApplyRagdollQualityPreset(Ragdoll3DComponent& ragdoll, const RagdollQualityPreset preset) noexcept {
    ragdoll.qualityPreset = preset;
    switch (preset) {
    case RagdollQualityPreset::High:
        ragdoll.stepSettings.velocityIterations = Physics::kRagdollDefaultVelocityIterations;
        ragdoll.stepSettings.positionIterations = Physics::kRagdollDefaultPositionIterations;
        ragdoll.stepSettings.stabilizeInertia = true;
        ragdoll.enableSelfCollision = true;
        ragdoll.enableDynamicPushback = true;
        ragdoll.maxSubstepsPerFrame = Physics::kMaximumRagdollSubsteps;
        ragdoll.staggerBlendPeak = 0.6F;
        ragdoll.staggerDecaySeconds = 0.6F;
        break;
    case RagdollQualityPreset::Balanced:
        ragdoll.stepSettings.velocityIterations = 10U;
        ragdoll.stepSettings.positionIterations = 5U;
        ragdoll.stepSettings.stabilizeInertia = true;
        ragdoll.enableSelfCollision = true;
        ragdoll.enableDynamicPushback = true;
        ragdoll.maxSubstepsPerFrame = 4U;
        ragdoll.staggerBlendPeak = 0.55F;
        ragdoll.staggerDecaySeconds = 0.45F;
        break;
    case RagdollQualityPreset::Fast:
        ragdoll.stepSettings.velocityIterations = 6U;
        ragdoll.stepSettings.positionIterations = 3U;
        ragdoll.stepSettings.stabilizeInertia = true;
        ragdoll.enableSelfCollision = false;
        ragdoll.enableDynamicPushback = false;
        ragdoll.maxSubstepsPerFrame = 2U;
        ragdoll.staggerBlendPeak = 0.5F;
        ragdoll.staggerDecaySeconds = 0.3F;
        break;
    case RagdollQualityPreset::Custom:
        break;
    }
}

namespace {

[[nodiscard]] RagdollBoneSetup MakeSetupFromProfile(const HumanoidRagdollProfileEntry& entry,
                                                    const std::string& boneName, const float sizeScale) {
    RagdollBoneSetup setup;
    setup.boneName = boneName;
    setup.mass = entry.mass;
    // Capsule thickness follows the rig: the profile is written for a 1.8 m person, so a rig twice as
    // tall gets bodies twice as thick instead of thin sticks.
    setup.radius = entry.radius * sizeScale;
    setup.halfHeight = entry.halfHeight * sizeScale;
    setup.linearDamping = entry.linearDamping;
    setup.angularDamping = entry.angularDamping;
    setup.swing1LimitDegrees = entry.swing1LimitDegrees;
    setup.swing2LimitDegrees = entry.swing2LimitDegrees;
    setup.twistLimitDegrees = entry.twistLimitDegrees;
    if (entry.hinge) {
        setup.swing1Motion = Physics::RagdollMotion::Locked;
        setup.swing2Motion = Physics::RagdollMotion::Locked;
        setup.twistMotion = Physics::RagdollMotion::Limited;
        setup.twistAsymmetric = true;
        setup.twistLowerLimitDegrees = entry.twistLowerLimitDegrees;
        setup.twistUpperLimitDegrees = entry.twistUpperLimitDegrees;
    }
    return setup;
}

/// Keeps at most `kMaximumRagdollBones` of `candidates`, the heaviest first (pelvis, thighs, torso, head;
/// fingers last), shallower bones winning ties, and returns them in rig order.
template <typename Candidate>
void KeepStructuralBodies(std::vector<Candidate>& candidates) {
    if (candidates.size() <= kMaximumRagdollBones) return;
    std::vector<Candidate> ranked = candidates;
    std::stable_sort(ranked.begin(), ranked.end(), [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.entry->mass != rhs.entry->mass) return lhs.entry->mass > rhs.entry->mass;
        if (lhs.depth != rhs.depth) return lhs.depth < rhs.depth;
        return lhs.boneIndex < rhs.boneIndex;
    });
    ranked.resize(kMaximumRagdollBones);
    std::sort(ranked.begin(), ranked.end(),
              [](const Candidate& lhs, const Candidate& rhs) { return lhs.boneIndex < rhs.boneIndex; });
    candidates = std::move(ranked);
}

/// The whole body from the engine's own humanoid: every body bone and every finger and toe, whatever the
/// rig calls them (Mixamo, Blender, UE and VRM names all map to the same set). Empty when the rig is not
/// recognisably a humanoid, so the caller can fall back to name patterns.
[[nodiscard]] std::vector<RagdollBoneSetup> BuildCanonicalHumanoidRagdollBones(
    const std::vector<SkeletonBone>& skeletonBones) {
    std::vector<std::string> names;
    names.reserve(skeletonBones.size());
    for (const SkeletonBone& bone : skeletonBones) names.push_back(bone.name);
    const Retarget::HumanoidReference& reference = Retarget::GetHumanoidReference();
    const Retarget::HumanoidMatch match = Retarget::MatchHumanoidNames(names, reference);

    struct Candidate final {
        std::size_t boneIndex = 0U;
        std::uint32_t depth = 0U;
        const HumanoidRagdollProfileEntry* entry = nullptr;
        std::string canonicalName;
    };
    std::vector<Candidate> candidates;
    std::size_t bodyBones = 0U;
    for (std::size_t boneIndex = 0U; boneIndex < skeletonBones.size(); ++boneIndex) {
        if (boneIndex >= match.referenceOfBone.size()) break;
        const std::int32_t reference_index = match.referenceOfBone[boneIndex];
        if (reference_index < 0 || static_cast<std::size_t>(reference_index) >= reference.info.size()) continue;
        const Retarget::HumanoidBoneKind kind = reference.info[static_cast<std::size_t>(reference_index)].kind;
        if (kind != Retarget::HumanoidBoneKind::Body && kind != Retarget::HumanoidBoneKind::Finger) continue;
        const std::string canonicalName =
            reference.skeleton.bones[static_cast<std::size_t>(reference_index)].name;
        const std::string lowered = ToLowerAscii(canonicalName);
        // A metacarpal is the palm's own rigid frame: it rides on the hand body.
        if (lowered.find("metacarpal") != std::string::npos) continue;
        const HumanoidRagdollProfileEntry* const entry = FindCanonicalProfileEntry(lowered);
        if (entry == nullptr) continue;
        if (kind == Retarget::HumanoidBoneKind::Body) ++bodyBones;
        candidates.push_back(Candidate{boneIndex, BoneDepth(skeletonBones, static_cast<std::uint32_t>(boneIndex)),
                                       entry, canonicalName});
    }
    // Hips, a spine, a head and at least one limb pair: below that it is not a humanoid worth building.
    if (bodyBones < 8U) return {};
    KeepStructuralBodies(candidates);

    const float sizeScale = std::clamp(std::isfinite(match.heightScale) ? match.heightScale : 1.0F, 0.25F, 4.0F);
    std::vector<RagdollBoneSetup> setups;
    setups.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        setups.push_back(MakeSetupFromProfile(*candidate.entry, skeletonBones[candidate.boneIndex].name, sizeScale));
    }
    return setups;
}

} // namespace

std::vector<RagdollBoneSetup> BuildHumanoidRagdollBones(const std::vector<SkeletonBone>& skeletonBones) {
    // The engine's own humanoid first: it recognises far more rigs than name patterns do and it knows
    // the fingers. Name patterns below remain for rigs it cannot place.
    if (std::vector<RagdollBoneSetup> canonical = BuildCanonicalHumanoidRagdollBones(skeletonBones);
        !canonical.empty()) {
        return canonical;
    }
    // Control rigs, twist/roll correctives and leaf markers never carry a body of their own.
    const auto isControlOrHelperBone = [](const std::string_view name) {
        const std::string lowered = ToLowerAscii(name);
        static constexpr std::array<std::string_view, 16U> kHelperPatterns{{
            "ik_", "_ik", "pole", "twist", "_end", "nub", "ctrl_", "_ctrl", "target_", "_target",
            "corrective", "helper", "socket", "attach", "weapon", "prop_",
        }};
        for (const std::string_view pat : kHelperPatterns) {
            if (lowered.find(pat) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    // Digits are detail, not structure. Without this every finger joint matched the "hand" row and a
    // fingered rig produced ~60 bodies - more than the solver accepts, so the whole ragdoll refused
    // to build. The generic builder has always skipped these; the humanoid one now agrees with it.
    // "index", "middle", "ring" and "little" are ordinary words inside other bone names ("spine_middle",
    // "spring_arm"), so they only count as a finger when the name also says it belongs to a hand.
    const auto isDigitBone = [](const std::string_view name) {
        const std::string lowered = ToLowerAscii(name);
        static constexpr std::array<std::string_view, 7U> kAlwaysDigit{{
            "finger", "thumb", "pinky", "palm", "carpal", "toe", "digit",
        }};
        for (const std::string_view pat : kAlwaysDigit) {
            if (lowered.find(pat) != std::string::npos) {
                return true;
            }
        }
        if (lowered.find("hand") == std::string::npos) {
            return false;
        }
        static constexpr std::array<std::string_view, 4U> kHandDigit{{"index", "middle", "ring", "little"}};
        for (const std::string_view pat : kHandDigit) {
            if (lowered.find(pat) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    struct Candidate final {
        std::size_t boneIndex = 0U;
        std::uint32_t depth = 0U;
        const HumanoidRagdollProfileEntry* entry = nullptr;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(skeletonBones.size());
    for (std::size_t boneIndex = 0U; boneIndex < skeletonBones.size(); ++boneIndex) {
        const std::string_view name = skeletonBones[boneIndex].name;
        if (name.empty() || isControlOrHelperBone(name) || isDigitBone(name)) continue;
        const HumanoidRagdollProfileEntry* entry = FindProfileEntry(name);
        if (entry == nullptr) continue;
        candidates.push_back(Candidate{boneIndex, BoneDepth(skeletonBones, static_cast<std::uint32_t>(boneIndex)),
                                       entry});
    }

    // A rig with more matched bones than the solver holds (long spine chains, double necks, extra
    // shoulder helpers) keeps the heaviest - most structural - bodies: pelvis, thighs, torso and head
    // first, hands last. Ties go to the shallower bone, then to rig order, so the choice is stable.
    if (candidates.size() > kMaximumRagdollBones) {
        std::vector<Candidate> ranked = candidates;
        std::stable_sort(ranked.begin(), ranked.end(), [](const Candidate& lhs, const Candidate& rhs) {
            if (lhs.entry->mass != rhs.entry->mass) return lhs.entry->mass > rhs.entry->mass;
            if (lhs.depth != rhs.depth) return lhs.depth < rhs.depth;
            return lhs.boneIndex < rhs.boneIndex;
        });
        ranked.resize(kMaximumRagdollBones);
        std::sort(ranked.begin(), ranked.end(),
                  [](const Candidate& lhs, const Candidate& rhs) { return lhs.boneIndex < rhs.boneIndex; });
        candidates = std::move(ranked);
    }

    std::vector<RagdollBoneSetup> setups;
    setups.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        const HumanoidRagdollProfileEntry* entry = candidate.entry;
        RagdollBoneSetup setup;
        setup.boneName = skeletonBones[candidate.boneIndex].name;
        setup.mass = entry->mass;
        setup.radius = entry->radius;
        setup.halfHeight = entry->halfHeight;
        setup.linearDamping = entry->linearDamping;
        setup.angularDamping = entry->angularDamping;
        setup.swing1LimitDegrees = entry->swing1LimitDegrees;
        setup.swing2LimitDegrees = entry->swing2LimitDegrees;
        setup.twistLimitDegrees = entry->twistLimitDegrees;
        if (entry->hinge) {
            setup.swing1Motion = Physics::RagdollMotion::Locked;
            setup.swing2Motion = Physics::RagdollMotion::Locked;
            setup.twistMotion = Physics::RagdollMotion::Limited;
            setup.twistAsymmetric = true;
            setup.twistLowerLimitDegrees = entry->twistLowerLimitDegrees;
            setup.twistUpperLimitDegrees = entry->twistUpperLimitDegrees;
        }
        setups.push_back(std::move(setup));
    }
    return setups;
}

std::vector<RagdollBoneSetup> BuildAutoRagdollBones(const std::vector<SkeletonBone>& skeletonBones) {
    std::vector<RagdollBoneSetup> setups = BuildHumanoidRagdollBones(skeletonBones);
    if (setups.size() < 2U) {
        setups = BuildGenericRagdollBones(skeletonBones);
    }
    return setups;
}

bool TrimRagdollBonesToCap(std::vector<RagdollBoneSetup>& bones) {
    if (bones.size() <= kMaximumRagdollBones) return false;
    const auto isDetailBone = [](const std::string_view name) {
        const std::string lowered = ToLowerAscii(name);
        static constexpr std::array<std::string_view, 12U> kDetailPatterns{{
            "finger", "thumb", "pinky", "palm", "carpal", "toe", "digit", "twist", "_end", "nub", "corrective",
            "helper",
        }};
        for (const std::string_view pat : kDetailPatterns) {
            if (lowered.find(pat) != std::string::npos) return true;
        }
        // Plain words inside other bone names ("spine_middle"): a finger only when it is on a hand.
        if (lowered.find("hand") != std::string::npos) {
            for (const std::string_view pat : {std::string_view{"index"}, std::string_view{"middle"},
                                               std::string_view{"ring"}, std::string_view{"little"}}) {
                if (lowered.find(pat) != std::string::npos) return true;
            }
        }
        return false;
    };
    std::vector<std::size_t> ranked(bones.size());
    for (std::size_t index = 0U; index < ranked.size(); ++index) ranked[index] = index;
    std::stable_sort(ranked.begin(), ranked.end(), [&bones, &isDetailBone](const std::size_t lhs, const std::size_t rhs) {
        const bool lhsDetail = isDetailBone(bones[lhs].boneName);
        const bool rhsDetail = isDetailBone(bones[rhs].boneName);
        if (lhsDetail != rhsDetail) return !lhsDetail;
        if (bones[lhs].mass != bones[rhs].mass) return bones[lhs].mass > bones[rhs].mass;
        return lhs < rhs;
    });
    ranked.resize(kMaximumRagdollBones);
    std::sort(ranked.begin(), ranked.end());
    std::vector<RagdollBoneSetup> kept;
    kept.reserve(ranked.size());
    for (const std::size_t index : ranked) kept.push_back(std::move(bones[index]));
    bones = std::move(kept);
    return true;
}

std::vector<RagdollBoneSetup> BuildGenericRagdollBones(const std::vector<SkeletonBone>& skeletonBones) {
    std::vector<RagdollBoneSetup> setups;
    if (skeletonBones.empty()) {
        return setups;
    }
    const auto isDetailOrHelperBone = [](const std::string_view name) {
        const std::string lowered = ToLowerAscii(name);
        static constexpr std::array<std::string_view, 25U> kSkipPatterns{{
            "finger", "thumb", "index", "middle", "ring", "pinky", "toe", "eye", "eyelid",
            "eyebrow", "brow", "jaw", "tongue", "cheek", "lip", "hair", "cloth", "cape",
            "skirt", "twist", "ik_", "_ik", "pole", "_end", "nub",
        }};
        for (const std::string_view pat : kSkipPatterns) {
            if (lowered.find(pat) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    // Prioritize shallower structural bones first so large rigs keep trunk + limbs within the body cap.
    std::vector<std::size_t> indices;
    indices.reserve(skeletonBones.size());
    for (std::size_t i = 0U; i < skeletonBones.size(); ++i) {
        if (!skeletonBones[i].name.empty() && !isDetailOrHelperBone(skeletonBones[i].name)) {
            indices.push_back(i);
        }
    }
    if (indices.empty()) {
        for (std::size_t i = 0U; i < skeletonBones.size(); ++i) {
            if (!skeletonBones[i].name.empty()) {
                indices.push_back(i);
            }
        }
    }
    std::sort(indices.begin(), indices.end(), [&skeletonBones](const std::size_t lhs, const std::size_t rhs) {
        const std::uint32_t dl = BoneDepth(skeletonBones, static_cast<std::uint32_t>(lhs));
        const std::uint32_t dr = BoneDepth(skeletonBones, static_cast<std::uint32_t>(rhs));
        if (dl != dr) return dl < dr;
        return lhs < rhs;
    });

    const std::size_t maxCount = std::min<std::size_t>(indices.size(), kMaximumRagdollBones);
    setups.reserve(maxCount);
    for (std::size_t idx = 0U; idx < maxCount; ++idx) {
        const std::size_t boneIndex = indices[idx];
        const std::uint32_t depth = BoneDepth(skeletonBones, static_cast<std::uint32_t>(boneIndex));
        RagdollBoneSetup setup;
        setup.boneName = skeletonBones[boneIndex].name;
        setup.mass = depth == 0U ? 10.0F : (depth == 1U ? 6.0F : 2.5F);
        setup.radius = depth == 0U ? 0.10F : (depth <= 2U ? 0.065F : 0.045F);
        setup.halfHeight = 0.08F;
        setup.linearDamping = 0.06F;
        setup.angularDamping = depth <= 1U ? 0.5F : 0.85F;
        setup.swing1LimitDegrees = 45.0F;
        setup.swing2LimitDegrees = 45.0F;
        setup.twistLimitDegrees = 35.0F;
        setups.push_back(std::move(setup));
    }
    return setups;
}

bool SyncRagdollBodiesToSkeletonPose(IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) {
        return false;
    }
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    if (!ragdoll.initialized || ragdoll.bindings.empty() || ragdoll.solver.GetBodyCount() == 0U) {
        return false;
    }
    if (ragdoll.skeleton == kInvalidEntity || !entityManager.IsAlive(ragdoll.skeleton) ||
        !entityManager.HasComponent<Skeleton3DComponent>(ragdoll.skeleton)) {
        return false;
    }
    const Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(ragdoll.skeleton);
    ObjectWorldFrame skeletonFrame;
    if (!ResolveSkeletonWorldFrame(entityManager, ragdoll.skeleton, skeletonFrame)) {
        return false;
    }
    SyncBodiesFromAnimatedPose(ragdoll, skeleton, skeletonFrame, 0.0F, /*kinematicOnlyBodies=*/false);
    return true;
}

Entity FindNearestSkeleton3D(IEntityManager& entityManager, const Entity from) {
    if (!entityManager.IsAlive(from)) return kInvalidEntity;
    if (entityManager.HasComponent<Skeleton3DComponent>(from)) return from;

    std::unordered_map<Entity, std::vector<Entity>> children;
    std::size_t skeletonCount = 0U;
    Entity onlySkeleton = kInvalidEntity;
    entityManager.ForEach<HierarchyComponent>([&children](const Entity entity, const HierarchyComponent& hierarchy) {
        if (hierarchy.parent != kInvalidEntity) children[hierarchy.parent].push_back(entity);
    });
    entityManager.ForEach<Skeleton3DComponent>(
        [&skeletonCount, &onlySkeleton](const Entity entity, const Skeleton3DComponent&) {
            ++skeletonCount;
            onlySkeleton = entity;
        });
    if (skeletonCount == 0U) return kInvalidEntity;

    const auto parentOf = [&entityManager](const Entity entity) {
        return entityManager.HasComponent<HierarchyComponent>(entity)
                   ? entityManager.GetComponent<HierarchyComponent>(entity).parent
                   : kInvalidEntity;
    };
    // Breadth-first so the closest skeleton in a subtree wins; a rigged one beats an empty one.
    const auto searchSubtree = [&](const Entity root, const Entity skip) {
        Entity firstEmpty = kInvalidEntity;
        std::vector<Entity> queue{root};
        for (std::size_t index = 0U; index < queue.size() && index < 4096U; ++index) {
            const Entity current = queue[index];
            if (current != skip && entityManager.HasComponent<Skeleton3DComponent>(current)) {
                if (!entityManager.GetComponent<Skeleton3DComponent>(current).bones.empty()) return current;
                if (firstEmpty == kInvalidEntity) firstEmpty = current;
            }
            const auto found = children.find(current);
            if (found == children.end()) continue;
            for (const Entity child : found->second) {
                if (child != skip) queue.push_back(child);
            }
        }
        return firstEmpty;
    };

    Entity result = searchSubtree(from, kInvalidEntity);
    if (result != kInvalidEntity) return result;
    Entity searched = from;
    for (Entity ancestor = parentOf(from); ancestor != kInvalidEntity; ancestor = parentOf(ancestor)) {
        if (parentOf(ancestor) == kInvalidEntity) {
            // The scene (or document) root holds every character, so its subtree only answers when
            // it is unambiguous.
            std::size_t underRoot = 0U;
            Entity onlyUnderRoot = kInvalidEntity;
            std::vector<Entity> queue{ancestor};
            for (std::size_t index = 0U; index < queue.size() && index < 4096U && underRoot < 2U; ++index) {
                if (entityManager.HasComponent<Skeleton3DComponent>(queue[index])) {
                    ++underRoot;
                    onlyUnderRoot = queue[index];
                }
                const auto found = children.find(queue[index]);
                if (found != children.end()) queue.insert(queue.end(), found->second.begin(), found->second.end());
            }
            if (underRoot == 1U) return onlyUnderRoot;
            break;
        }
        result = searchSubtree(ancestor, searched);
        if (result != kInvalidEntity) return result;
        searched = ancestor;
    }
    return skeletonCount == 1U ? onlySkeleton : kInvalidEntity;
}

bool PrepareRagdoll3D(IEntityManager& entityManager, const Entity entity, const bool regenerateBones) {
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) {
        return false;
    }
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    const bool linked = ragdoll.skeleton != kInvalidEntity && entityManager.IsAlive(ragdoll.skeleton) &&
                        entityManager.HasComponent<Skeleton3DComponent>(ragdoll.skeleton);
    if (!linked) {
        const Entity found = FindNearestSkeleton3D(entityManager, entity);
        if (found == kInvalidEntity) {
            ragdoll.lastSetupError =
                "No Skeleton3D found for this ragdoll. Place the Ragdoll3D under the character, or link a Skeleton3D.";
            return false;
        }
        ragdoll.skeleton = found;
        ragdoll.initialized = false;
    }
    const Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(ragdoll.skeleton);
    if (skeleton.bones.empty()) {
        ragdoll.lastSetupError = "The linked Skeleton3D has no bones yet. Assign a rigged source model first.";
        return false;
    }
    if (regenerateBones || ragdoll.bones.empty()) {
        ragdoll.bones = BuildAutoRagdollBones(skeleton.bones);
        ragdoll.initialized = false;
        if (ragdoll.bones.size() < 2U) {
            ragdoll.bones.clear();
            ragdoll.lastSetupError = "Fewer than two bones on this skeleton could be matched to ragdoll bodies. "
                                     "Add the bones to simulate by hand.";
            return false;
        }
    }
    ragdoll.lastSetupError.clear();
    return true;
}

bool InitializeRagdoll3D(IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) {
        return false;
    }
    Ragdoll3DComponent& component = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    const auto fail = [&component](std::string reason) {
        component.lastSetupError = std::move(reason);
        component.initialized = false;
        return false;
    };
    if (component.skeleton == kInvalidEntity || !entityManager.IsAlive(component.skeleton) ||
        !entityManager.HasComponent<Skeleton3DComponent>(component.skeleton)) {
        return fail("No Skeleton3D is linked to this ragdoll.");
    }
    Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(component.skeleton);
    if (skeleton.bones.empty()) return fail("The linked Skeleton3D has no bones yet.");

    ObjectWorldFrame skeletonFrame;
    if (!ResolveSkeletonWorldFrame(entityManager, component.skeleton, skeletonFrame)) {
        return fail("The linked Skeleton3D has no valid world transform.");
    }

    const auto resolveSetups = [&component, &skeleton](std::vector<std::size_t>& setupIndices,
                                                       std::vector<std::uint32_t>& boneIndices) {
        setupIndices.clear();
        boneIndices.clear();
        setupIndices.reserve(component.bones.size());
        boneIndices.reserve(component.bones.size());
        for (std::size_t setupIndex = 0U; setupIndex < component.bones.size(); ++setupIndex) {
            std::uint32_t boneIndex = kInvalidSkeletonBoneIndex;
            if (!TryFindSkeletonBoneIndex(skeleton, component.bones[setupIndex].boneName, boneIndex)) continue;
            if (boneIndex >= skeleton.bones.size()) continue;
            setupIndices.push_back(setupIndex);
            boneIndices.push_back(boneIndex);
        }
    };

    if (component.bones.empty()) {
        component.bones = BuildAutoRagdollBones(skeleton.bones);
        if (component.bones.size() < 2U) {
            // One body is not an articulated ragdoll, and keeping it would stop the next attempt
            // from generating a list (an empty one is what asks for generation).
            component.bones.clear();
            return fail("Fewer than two bones on this skeleton could be matched to ragdoll bodies. "
                        "Add the bones to simulate by hand.");
        }
    }
    // An oversized list (authored by hand, or by an older generator) is trimmed to the structural
    // bodies instead of refusing the whole ragdoll.
    static_cast<void>(TrimRagdollBonesToCap(component.bones));

    std::vector<std::size_t> resolvedSetupIndices;
    std::vector<std::uint32_t> resolvedBoneIndices;
    resolveSetups(resolvedSetupIndices, resolvedBoneIndices);
    // A bone list that names none of this rig's bones was generated for a different rig (a mesh was
    // swapped, or the ragdoll was copied between characters). Rebuild it for the rig it is on now.
    if (resolvedSetupIndices.empty()) {
        std::vector<RagdollBoneSetup> regenerated = BuildAutoRagdollBones(skeleton.bones);
        if (regenerated.size() >= 2U) {
            component.bones = std::move(regenerated);
            resolveSetups(resolvedSetupIndices, resolvedBoneIndices);
        }
    }
    if (resolvedSetupIndices.empty()) {
        return fail("None of the ragdoll's bone names exist on the linked Skeleton3D. Press Auto-Generate.");
    }

    std::vector<bool> simulated(skeleton.bones.size(), false);
    for (const std::uint32_t boneIndex : resolvedBoneIndices) simulated[boneIndex] = true;

    std::vector<ObjectWorldFrame> bindFrames(skeleton.bones.size());
    std::vector<bool> bindResolved(skeleton.bones.size(), false);
    for (std::size_t boneIndex = 0U; boneIndex < skeleton.bones.size(); ++boneIndex) {
        bindResolved[boneIndex] = TryResolveSkeletonBoneWorldFrame(
            skeleton, static_cast<std::uint32_t>(boneIndex), skeletonFrame, bindFrames[boneIndex]);
    }

    const Math::Vector3 characterForward =
        DetectCharacterForwardWorld(skeleton, skeletonFrame, bindFrames, bindResolved);
    const Math::Vector3 characterRight =
        SafeNormalize(Math::RotateVector(skeletonFrame.rotation, Math::Vector3{1.0F, 0.0F, 0.0F}),
                         Math::Vector3{1.0F, 0.0F, 0.0F});

    // Build bodies root-first so joint anchors are always expressed against an existing parent body.
    std::vector<std::size_t> order(resolvedSetupIndices.size());
    for (std::size_t index = 0U; index < order.size(); ++index) order[index] = index;
    std::sort(order.begin(), order.end(), [&resolvedBoneIndices, &skeleton](const std::size_t lhs,
                                                                            const std::size_t rhs) {
        const std::uint32_t lhsDepth = BoneDepth(skeleton.bones, resolvedBoneIndices[lhs]);
        const std::uint32_t rhsDepth = BoneDepth(skeleton.bones, resolvedBoneIndices[rhs]);
        if (lhsDepth != rhsDepth) return lhsDepth < rhsDepth;
        return resolvedBoneIndices[lhs] < resolvedBoneIndices[rhs];
    });

    std::vector<Physics::RagdollBody> bodies;
    std::vector<Physics::RagdollJoint> joints;
    std::vector<RagdollBoneBinding> bindings;
    bodies.reserve(order.size());
    bindings.reserve(order.size());
    std::vector<std::size_t> bodyOfBone(skeleton.bones.size(), kInvalidSkeletonBoneIndex);

    for (const std::size_t listIndex : order) {
        const std::size_t setupIndex = resolvedSetupIndices[listIndex];
        const RagdollBoneSetup& setup = component.bones[setupIndex];
        const std::uint32_t boneIndex = resolvedBoneIndices[listIndex];
        if (!bindResolved[boneIndex]) continue;
        const ObjectWorldFrame& boneFrame = bindFrames[boneIndex];

        // BUG #5 FIX: Use midline-aware primary child selection for Pelvis/Chest multi-child bones!
        const std::uint32_t childIndex = FindPrimaryChild(skeleton.bones, boneIndex);
        Math::Vector3 direction{0.0F, 1.0F, 0.0F};
        float length = 2.0F * (std::max(0.0F, setup.halfHeight) + setup.radius);
        if (childIndex != kInvalidSkeletonBoneIndex && childIndex < bindFrames.size() && bindResolved[childIndex]) {
            const Math::Vector3 delta = bindFrames[childIndex].position - boneFrame.position;
            const float distance = std::sqrt(Math::LengthSquared(delta));
            if (std::isfinite(distance) && distance > kMinimumBoneLength) {
                direction = delta * (1.0F / distance);
                length = distance;
            }
        }
        direction = SafeNormalize(direction, Math::Vector3{0.0F, 1.0F, 0.0F});

        Physics::RagdollBody body;
        body.mass = setup.mass;
        body.radius = setup.radius;
        body.halfHeight = std::max(0.005F, length * 0.5F - setup.radius);
        body.linearDamping = setup.linearDamping;
        body.angularDamping = setup.angularDamping;
        body.friction = setup.friction;
        body.restitution = 0.0F;
        body.position = boneFrame.position + direction * (length * 0.5F);
        body.rotation = MakeCapsuleRotationFromAxis(direction);
        body.linearVelocity = component.initialVelocity;
        body.angularVelocity = Math::Vector3{};

        RagdollBoneBinding binding;
        binding.bodyIndex = static_cast<std::uint32_t>(bodies.size());
        binding.boneIndex = boneIndex;
        binding.setupIndex = setupIndex;
        binding.depth = BoneDepth(skeleton.bones, boneIndex);
        Math::Quaternion inverseBody;
        if (Math::TryInverse(body.rotation, inverseBody)) {
            binding.boneOffset = Math::Multiply(inverseBody, boneFrame.rotation);
            binding.rootAnchorLocal = Math::RotateVector(inverseBody, boneFrame.position - body.position);
        }
        binding.prevWorldPosition = body.position;
        binding.prevWorldRotation = body.rotation;
        binding.hasPreviousWorldPose = true;

        bodyOfBone[boneIndex] = bodies.size();
        bodies.push_back(body);
        bindings.push_back(binding);
    }
    if (bodies.empty()) return fail("The skeleton pose could not be resolved for any ragdoll bone.");

    for (RagdollBoneBinding& binding : bindings) {
        const RagdollBoneSetup& setup = component.bones[binding.setupIndex];
        std::uint32_t ancestorBone = kInvalidSkeletonBoneIndex;
        if (!setup.parentBoneName.empty()) {
            std::uint32_t explicitParent = kInvalidSkeletonBoneIndex;
            if (TryFindSkeletonBoneIndex(skeleton, setup.parentBoneName, explicitParent) &&
                explicitParent < bodyOfBone.size() && bodyOfBone[explicitParent] != kInvalidSkeletonBoneIndex &&
                explicitParent != binding.boneIndex) {
                ancestorBone = explicitParent;
            }
        }
        if (ancestorBone == kInvalidSkeletonBoneIndex) {
            ancestorBone = FindSimulatedAncestor(skeleton.bones, simulated, binding.boneIndex);
        }
        binding.parentBoneIndex = ancestorBone;
        if (ancestorBone == kInvalidSkeletonBoneIndex || bodyOfBone[ancestorBone] == kInvalidSkeletonBoneIndex) {
            binding.isRootBody = true;
            binding.parentBodyIndex = Physics::kInvalidRagdollBody;
            continue;
        }

        const std::size_t parentBodyIndex = bodyOfBone[ancestorBone];
        const std::size_t childBodyIndex = binding.bodyIndex;
        binding.parentBodyIndex = static_cast<std::uint32_t>(parentBodyIndex);
        binding.isRootBody = false;

        const Math::Vector3 jointWorldPoint = bindFrames[binding.boneIndex].position;
        const Physics::RagdollBody& parentBody = bodies[parentBodyIndex];
        const Physics::RagdollBody& childBody = bodies[childBodyIndex];
        Math::Quaternion inverseParent;
        Math::Quaternion inverseChild;
        if (!Math::TryInverse(parentBody.rotation, inverseParent) ||
            !Math::TryInverse(childBody.rotation, inverseChild)) {
            continue;
        }

        Physics::RagdollJoint joint;
        joint.firstBody = static_cast<std::uint32_t>(parentBodyIndex);
        joint.secondBody = static_cast<std::uint32_t>(childBodyIndex);
        joint.firstAnchor = Math::RotateVector(inverseParent, jointWorldPoint - parentBody.position);
        joint.secondAnchor = Math::RotateVector(inverseChild, jointWorldPoint - childBody.position);
        joint.swing1Motion = setup.swing1Motion;
        joint.swing2Motion = setup.swing2Motion;
        joint.twistMotion = setup.twistMotion;
        joint.swing1LimitDegrees = setup.swing1LimitDegrees;
        joint.swing2LimitDegrees = setup.swing2LimitDegrees;
        joint.twistLimitDegrees = setup.twistLimitDegrees;
        joint.twistAsymmetric = setup.twistAsymmetric;
        joint.twistLowerLimitDegrees = setup.twistLowerLimitDegrees;
        joint.twistUpperLimitDegrees = setup.twistUpperLimitDegrees;

        // Universal T-pose / A-pose hinge axis derivation: `Cross(characterForward, limbDir)` is an axis
        // strictly orthogonal to both the limb and the facing direction, so the hinge folds in the plane
        // the limb swings in front of and behind the body.
        Math::Vector3 jointAxisLocal = setup.jointAxisLocal;
        if (!(Math::LengthSquared(jointAxisLocal) > kDirectionEpsilon)) {
            const bool isHinge = setup.twistMotion == Physics::RagdollMotion::Limited &&
                                 setup.swing1Motion == Physics::RagdollMotion::Locked;
            Math::Vector3 worldAxis{};
            if (isHinge) {
                const Math::Vector3 limbDir =
                    SafeNormalize(Math::RotateVector(childBody.rotation, Math::Vector3{0.0F, 1.0F, 0.0F}),
                                     Math::Vector3{0.0F, -1.0F, 0.0F});
                // `forward x limb`: with the solver's twist sign, a positive twist about this axis carries the
                // distal bone toward the front (elbows, range [-5, 150]) and a negative one toward the back
                // (knees, range [-150, 5]). Measured by kicking shins and forearms in zero gravity; the
                // opposite order folded elbows backward and knees forward.
                const Math::Vector3 crossAxis = Math::Cross(characterForward, limbDir);
                if (Math::LengthSquared(crossAxis) > kDirectionEpsilon) {
                    worldAxis = SafeNormalize(crossAxis, characterRight);
                } else {
                    worldAxis = characterRight;
                }
            } else {
                worldAxis = SafeNormalize(childBody.position - parentBody.position,
                                             Math::Vector3{0.0F, 1.0F, 0.0F});
            }
            jointAxisLocal = Math::RotateVector(inverseParent, worldAxis);
        }
        joint.firstFrame = Physics::MakeJointFrameFromTwistAxis(jointAxisLocal);
        joint.secondFrame = Physics::ComputeJointSecondFrame(parentBody.rotation, childBody.rotation,
                                                                joint.firstFrame);
        joints.push_back(joint);
    }

    if (!component.solver.Reset(bodies)) {
        return fail("Too many ragdoll bodies (" + std::to_string(bodies.size()) + "); the limit is " +
                    std::to_string(Physics::kMaximumRagdollBodies) + ".");
    }
    for (const Physics::RagdollJoint& joint : joints) {
        if (!component.solver.AddJoint(joint)) return fail("A ragdoll joint could not be created.");
    }

    // Pairwise non-adjacent self-collision filter table: disable parent-child + bind-pose overlaps.
    component.solver.RebuildSelfCollisionFilter(/*disableCurrentPoseOverlaps=*/true, /*minimumJointHops=*/2U);
    // Fingers and toes only touch the world: a body this light fighting its neighbours is jitter, not detail.
    for (const RagdollBoneBinding& detail : bindings) {
        if (detail.setupIndex >= component.bones.size() ||
            component.bones[detail.setupIndex].mass > kDetailBodyMassKilograms) {
            continue;
        }
        for (std::uint32_t other = 0U; other < static_cast<std::uint32_t>(bodies.size()); ++other) {
            if (other != detail.bodyIndex) {
                component.solver.SetSelfCollisionPairEnabled(detail.bodyIndex, other, false);
            }
        }
    }

    EnsureSkeletonPose(skeleton);
    component.bindings = std::move(bindings);
    component.pendingSeconds = 0.0F;
    component.hadFloorSupport = false;
    component.lastReport = Physics::RagdollStepReport{};
    component.lastSetupError.clear();
    component.initialized = true;
    return true;
}

bool ActivateRagdoll(IEntityManager& entityManager, const Entity entity, const RagdollMode mode,
                        const Math::Vector3& extraVelocity) {
    if (!IsFinite3DObjectVector(extraVelocity)) return false;
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) {
        return false;
    }
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    if (!ragdoll.initialized && !InitializeRagdoll3D(entityManager, entity)) return false;

    ragdoll.mode = mode;
    ragdoll.enabled = true;
    ragdoll.solver.Wake();
    // A simulation session that already captured its start pose (entity or editor simulation) keeps it. Otherwise
    // a recovery stands up into the live animation - and, if nothing is animating, into the pose it fell from.
    if (!ragdoll.recoverToCapturedPose && entityManager.IsAlive(ragdoll.skeleton) &&
        entityManager.HasComponent<Skeleton3DComponent>(ragdoll.skeleton)) {
        ragdoll.recoverTargetPose =
            GetSkeletonCurrentPose(entityManager.GetComponent<Skeleton3DComponent>(ragdoll.skeleton));
    }
    ragdoll.restSeconds = 0.0F;
    ragdoll.staggering = false;

    const std::size_t bodyCount = ragdoll.solver.GetBodyCount();
    for (std::size_t i = 0U; i < bodyCount; ++i) {
        Physics::RagdollBody& body = ragdoll.solver.GetBody(i);
        if (!ragdoll.inheritAnimatedVelocity) {
            body.linearVelocity = Math::Vector3{};
            body.angularVelocity = Math::Vector3{};
        }
        body.linearVelocity += extraVelocity;
    }
    return true;
}

bool CaptureRagdollPoseSnapshot(IEntityManager& entityManager, const Entity entity,
                                   RagdollPoseSnapshot& outSnapshot) {
    outSnapshot = {};
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) {
        return false;
    }
    const Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    if (!entityManager.IsAlive(ragdoll.skeleton) ||
        !entityManager.HasComponent<Skeleton3DComponent>(ragdoll.skeleton)) {
        return false;
    }
    const Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(ragdoll.skeleton);
    outSnapshot.bonePoses = GetSkeletonCurrentPose(skeleton);
    for (const RagdollBoneBinding& binding : ragdoll.bindings) {
        if (binding.isRootBody && binding.bodyIndex < ragdoll.solver.GetBodyCount()) {
            const Physics::RagdollBody& rootBody = ragdoll.solver.GetBody(binding.bodyIndex);
            outSnapshot.rootWorldPosition =
                rootBody.position + Math::RotateVector(rootBody.rotation, binding.rootAnchorLocal);
            outSnapshot.rootWorldRotation = Math::Multiply(rootBody.rotation, binding.boneOffset);
            break;
        }
    }
    outSnapshot.valid = !outSnapshot.bonePoses.empty();
    return outSnapshot.valid;
}

bool EvaluateRagdollGetUpState(IEntityManager& entityManager, const Entity entity,
                                  RagdollGetUpState& outState, const float floorYOffset) {
    outState = {};
    if (!std::isfinite(floorYOffset)) return false;
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) {
        return false;
    }
    const Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    if (!ragdoll.initialized || ragdoll.solver.GetBodyCount() == 0U) return false;
    if (!entityManager.IsAlive(ragdoll.skeleton) ||
        !entityManager.HasComponent<Skeleton3DComponent>(ragdoll.skeleton)) {
        return false;
    }
    const Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(ragdoll.skeleton);

    const RagdollBoneBinding* rootBinding = nullptr;
    const RagdollBoneBinding* headOrChestBinding = nullptr;
    float lowestSurfaceY = std::numeric_limits<float>::infinity();
    for (const RagdollBoneBinding& binding : ragdoll.bindings) {
        if (binding.bodyIndex >= ragdoll.solver.GetBodyCount()) continue;
        const Physics::RagdollBody& body = ragdoll.solver.GetBody(binding.bodyIndex);
        Math::Vector3 segStart{};
        Math::Vector3 segEnd{};
        Physics::ComputeRagdollCapsuleWorldSegment(body, segStart, segEnd);
        lowestSurfaceY = std::min(lowestSurfaceY, std::min(segStart.y, segEnd.y) - body.radius);

        if (binding.isRootBody && rootBinding == nullptr) rootBinding = &binding;
        if (binding.boneIndex < skeleton.bones.size()) {
            const std::string lower = ToLowerAscii(skeleton.bones[binding.boneIndex].name);
            if (lower.find("head") != std::string::npos || lower.find("chest") != std::string::npos ||
                lower.find("spine") != std::string::npos) {
                headOrChestBinding = &binding;
            }
        }
    }
    if (rootBinding == nullptr) rootBinding = &ragdoll.bindings.front();

    const Physics::RagdollBody& rootBody = ragdoll.solver.GetBody(rootBinding->bodyIndex);
    const Math::Vector3 rootUp = Math::RotateVector(rootBody.rotation, Math::Vector3{0.0F, 1.0F, 0.0F});
    const Math::Vector3 rootForward = Math::RotateVector(rootBody.rotation, Math::Vector3{0.0F, 0.0F, 1.0F});

    // If torso/pelvis capsule axis is mostly vertical -> Upright; otherwise check chest facing Y.
    if (rootUp.y > 0.65F) {
        outState.orientation = RagdollLyingOrientation::Upright;
    } else if (rootForward.y >= 0.0F) {
        outState.orientation = RagdollLyingOrientation::FaceUp;
    } else {
        outState.orientation = RagdollLyingOrientation::FaceDown;
    }

    const Math::Vector3 rootPos =
        rootBody.position + Math::RotateVector(rootBody.rotation, rootBinding->rootAnchorLocal);
    const float groundY = std::isfinite(lowestSurfaceY) ? (lowestSurfaceY + floorYOffset) : rootPos.y;
    outState.groundedRootPosition = Math::Vector3{rootPos.x, groundY, rootPos.z};

    // Derive horizontal facing direction (pelvis -> head/chest projected on XZ; flip if FaceUp).
    Math::Vector3 horizontalDir{rootForward.x, 0.0F, rootForward.z};
    if (headOrChestBinding != nullptr && headOrChestBinding != rootBinding) {
        const Math::Vector3 headPos = ragdoll.solver.GetBody(headOrChestBinding->bodyIndex).position;
        const Math::Vector3 spineXZ{headPos.x - rootBody.position.x, 0.0F, headPos.z - rootBody.position.z};
        if (Math::LengthSquared(spineXZ) > kDirectionEpsilon) {
            horizontalDir = outState.orientation == RagdollLyingOrientation::FaceUp ? (spineXZ * -1.0F) : spineXZ;
        }
    }
    horizontalDir = SafeNormalize(horizontalDir, Math::Vector3{0.0F, 0.0F, 1.0F});
    const float yaw = std::atan2(horizontalDir.x, horizontalDir.z);
    Math::Quaternion alignedRot;
    if (!Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, yaw, alignedRot)) {
        alignedRot = Math::Quaternion{};
    }
    outState.alignedRootRotation = alignedRot;
    outState.valid = true;
    return true;
}

bool BeginBlendOutFromRagdoll(IEntityManager& entityManager, const Entity entity,
                                 const float blendDurationSeconds) {
    if (!std::isfinite(blendDurationSeconds) || !(blendDurationSeconds > 0.0F)) return false;
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) {
        return false;
    }
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    if (!CaptureRagdollPoseSnapshot(entityManager, entity, ragdoll.blendOutSnapshot)) return false;
    ragdoll.blendOutElapsedSeconds = 0.0F;
    ragdoll.blendOutDurationSeconds = blendDurationSeconds;
    ragdoll.mode = RagdollMode::BlendingOut;
    return true;
}

bool IsRagdollActive(const IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) return false;
    const Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    return ragdoll.enabled && ragdoll.initialized &&
           (ragdoll.mode == RagdollMode::Simulating || ragdoll.mode == RagdollMode::Active ||
            ragdoll.mode == RagdollMode::BlendingOut);
}

bool DeactivateRagdoll(IEntityManager& entityManager, const Entity entity) {
    if (!IsRagdollActive(entityManager, entity)) return false;
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    ragdoll.mode = RagdollMode::Kinematic;
    ragdoll.blendOutSnapshot.valid = false;
    ragdoll.restSeconds = 0.0F;
    ragdoll.staggering = false;
    return true;
}

bool StaggerRagdoll(IEntityManager& entityManager, const Entity entity, const float strength) {
    if (!std::isfinite(strength)) return false;
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) return false;
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    if (!ragdoll.enabled) return false;
    if (ragdoll.mode == RagdollMode::Simulating || ragdoll.mode == RagdollMode::BlendingOut) return false;
    if (!ActivateRagdoll(entityManager, entity, RagdollMode::Active)) return false;
    ragdoll.staggering = true;
    ragdoll.staggerElapsed = 0.0F;
    ragdoll.staggerPeakNow = std::clamp(ragdoll.staggerBlendPeak * std::clamp(strength, 0.0F, 1.0F), 0.0F, 1.0F);
    ragdoll.effectiveBlend = 0.0F;
    return true;
}

bool BeginRagdollRecovery(IEntityManager& entityManager, const Entity entity) {
    if (!IsRagdollActive(entityManager, entity)) return false;
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    if (ragdoll.mode == RagdollMode::BlendingOut) return true; // already standing up
    ragdoll.restSeconds = 0.0F;
    return BeginBlendOutFromRagdoll(entityManager, entity, ragdoll.recoverDurationSeconds);
}

bool StartRagdollEditorPreview(IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) {
        return false;
    }
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    if (ragdoll.editorPreviewSimulating) return true;
    if (!ragdoll.enabled || entityManager.HasComponent<DisabledComponent>(entity)) {
        ragdoll.lastSetupError = "This ragdoll is disabled. Enable it to preview.";
        return false;
    }
    if (ragdoll.physicsBlendWeight <= 0.0F) {
        ragdoll.lastSetupError = "Physics Blend Weight is 0, so the simulated pose would not show. Raise it above 0.";
        return false;
    }
    if (!entityManager.IsAlive(ragdoll.skeleton) ||
        !entityManager.HasComponent<Skeleton3DComponent>(ragdoll.skeleton)) {
        ragdoll.lastSetupError = "No Skeleton3D is linked to this ragdoll.";
        return false;
    }
    const Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(ragdoll.skeleton);
    ragdoll.editorPreviewBackupPose = skeleton.pose;
    ragdoll.editorPreviewBackupMode = ragdoll.mode;
    // A preview has no animation under it, so standing back up goes to the pose it started from.
    ragdoll.recoverTargetPose = GetSkeletonCurrentPose(skeleton);
    ragdoll.recoverToCapturedPose = true;
    ragdoll.restSeconds = 0.0F;
    ragdoll.initialized = false;
    if (!InitializeRagdoll3D(entityManager, entity)) {
        ragdoll.editorPreviewBackupPose.clear();
        return false;
    }
    ragdoll.mode = RagdollMode::Simulating;
    ragdoll.editorPreviewSimulating = true;
    ragdoll.solver.Wake();
    return true;
}

bool StopRagdollEditorPreview(IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity)) {
        return false;
    }
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    if (!ragdoll.editorPreviewSimulating) return false;
    if (entityManager.IsAlive(ragdoll.skeleton) &&
        entityManager.HasComponent<Skeleton3DComponent>(ragdoll.skeleton)) {
        Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(ragdoll.skeleton);
        skeleton.pose = std::move(ragdoll.editorPreviewBackupPose);
    }
    ragdoll.editorPreviewBackupPose.clear();
    ragdoll.mode = ragdoll.editorPreviewBackupMode;
    ragdoll.editorPreviewSimulating = false;
    ragdoll.initialized = false;
    ragdoll.pendingSeconds = 0.0F;
    return true;
}

bool ApplyRagdollImpact(IEntityManager& entityManager, const Entity entity,
                           const Math::Vector3& worldPoint, const Math::Vector3& impulse,
                           const float searchRadius) {
    if (!std::isfinite(searchRadius) || searchRadius < 0.0F) return false;
    if (!IsFinite3DObjectVector(worldPoint) || !IsFinite3DObjectVector(impulse)) return false;
    if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity) ||
        entityManager.HasComponent<DisabledComponent>(entity)) {
        return false;
    }
    Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
    if (!ragdoll.enabled) return false;
    if (!ragdoll.initialized && !InitializeRagdoll3D(entityManager, entity)) return false;

    std::size_t bodyCount = ragdoll.solver.GetBodyCount();
    if (bodyCount == 0U) return false;

    // A hit on a standing body reacts on its own when asked to: a light hit staggers it, a strong one knocks it
    // down. The bodies follow the pose first, so the animation does not overwrite the impulse that follows.
    if (ragdoll.automaticReactions && (ragdoll.mode == RagdollMode::Kinematic || ragdoll.staggering)) {
        const float magnitude = std::sqrt(Math::LengthSquared(impulse));
        if (magnitude >= ragdoll.fallImpulseThreshold) {
            static_cast<void>(SyncRagdollBodiesToSkeletonPose(entityManager, entity));
            static_cast<void>(ActivateRagdoll(entityManager, entity, RagdollMode::Simulating));
        } else if (magnitude >= ragdoll.staggerImpulseThreshold && !ragdoll.staggering) {
            const float span = ragdoll.fallImpulseThreshold - ragdoll.staggerImpulseThreshold;
            const float strength =
                span > 1.0e-4F ? std::clamp((magnitude - ragdoll.staggerImpulseThreshold) / span, 0.35F, 1.0F) : 1.0F;
            static_cast<void>(SyncRagdollBodiesToSkeletonPose(entityManager, entity));
            static_cast<void>(StaggerRagdoll(entityManager, entity, strength));
        }
        bodyCount = ragdoll.solver.GetBodyCount();
    }

    std::size_t nearestBody = bodyCount;
    float nearestDistance = searchRadius;
    Math::Vector3 nearestPoint{};
    for (std::size_t index = 0U; index < bodyCount; ++index) {
        const Physics::RagdollBody& body = ragdoll.solver.GetBody(index);
        const Math::Vector3 axisPoint = ClosestAxisPoint(body, worldPoint);
        const float distance = std::sqrt(Math::LengthSquared(worldPoint - axisPoint)) - body.radius;
        if (!std::isfinite(distance)) continue;
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestBody = index;
            nearestPoint = axisPoint;
        }
    }
    if (nearestBody == bodyCount) return false;

    ragdoll.solver.ApplyImpulse(static_cast<std::uint32_t>(nearestBody), impulse, nearestPoint);
    return true;
}

namespace {

/// The character controller that moves the skeleton: on the skeleton itself or on its nearest ancestor.
[[nodiscard]] const CharacterControllerComponent* FindDrivingCharacterController(const IEntityManager& entities,
                                                                                    const Entity skeleton) {
    Entity cursor = skeleton;
    for (std::uint32_t hops = 0U; hops < 8U && cursor != kInvalidEntity && entities.IsAlive(cursor); ++hops) {
        if (entities.HasComponent<CharacterControllerComponent>(cursor)) {
            return &entities.GetComponent<CharacterControllerComponent>(cursor);
        }
        if (!entities.HasComponent<HierarchyComponent>(cursor)) break;
        const Entity parent = entities.GetComponent<HierarchyComponent>(cursor).parent;
        if (parent == cursor) break;
        cursor = parent;
    }
    return nullptr;
}

/// Tracks the fall of the character that carries a standing ragdoll; a landing at or above `fallLandingSpeed`
/// knocks the body down. True when this frame was spent starting the fall.
[[nodiscard]] bool UpdateLandingReaction(IEntityManager& entities, const Entity entity, Ragdoll3DComponent& ragdoll) {
    const CharacterControllerComponent* const controller = FindDrivingCharacterController(entities, ragdoll.skeleton);
    if (controller == nullptr) return false;
    if (!controller->grounded) {
        ragdoll.wasAirborne = true;
        ragdoll.airborneMaxDownSpeed = std::max(ragdoll.airborneMaxDownSpeed, -controller->velocity.y);
        return false;
    }
    const bool hardLanding = ragdoll.wasAirborne && ragdoll.airborneMaxDownSpeed >= ragdoll.fallLandingSpeed;
    ragdoll.wasAirborne = false;
    ragdoll.airborneMaxDownSpeed = 0.0F;
    if (!hardLanding) return false;
    return ActivateRagdoll(entities, entity, RagdollMode::Simulating, controller->realVelocity);
}

/// Advances a stagger: the physics blend rises to its peak then fades, the body returns to the animation when it
/// has faded, and a body pushed too far from its animated pose falls. True when the frame is finished here.
[[nodiscard]] bool UpdateStagger(IEntityManager& entities, const Entity entity, Ragdoll3DComponent& ragdoll,
                                   const float deltaTimeSeconds) {
    ragdoll.staggerElapsed += deltaTimeSeconds;
    const float rise = std::max(1.0e-4F, ragdoll.staggerRiseSeconds);
    const float decay = std::max(1.0e-4F, ragdoll.staggerDecaySeconds);
    float envelope = 0.0F;
    if (ragdoll.staggerElapsed < rise) {
        envelope = ragdoll.staggerPeakNow * (ragdoll.staggerElapsed / rise);
    } else {
        const float t = (ragdoll.staggerElapsed - rise) / decay;
        if (t >= 1.0F) {
            ragdoll.staggering = false;
            ragdoll.mode = RagdollMode::Kinematic;
            ragdoll.effectiveBlend = 0.0F;
            return true; // the animation drives again from the next frame
        }
        envelope = ragdoll.staggerPeakNow * (1.0F - t * t * (3.0F - 2.0F * t));
    }
    ragdoll.effectiveBlend = envelope;

    // Too far from where the animation wants the pelvis: it is falling, not staggering.
    const float limitRadians = ragdoll.staggerFallTiltDegrees * 0.017453292F;
    for (const RagdollBoneBinding& binding : ragdoll.bindings) {
        if (!binding.isRootBody || binding.bodyIndex >= ragdoll.solver.GetBodyCount() ||
            !binding.hasPreviousWorldPose) {
            continue;
        }
        const Math::Quaternion& body = ragdoll.solver.GetBody(binding.bodyIndex).rotation;
        const Math::Quaternion& target = binding.prevWorldRotation;
        const float dot = std::clamp(std::fabs(body.x * target.x + body.y * target.y + body.z * target.z +
                                               body.w * target.w), 0.0F, 1.0F);
        if (2.0F * std::acos(dot) > limitRadians) {
            static_cast<void>(ActivateRagdoll(entities, entity, RagdollMode::Simulating));
        }
        break;
    }
    return false;
}

} // namespace

std::size_t UpdateRagdoll3DObjects(IEntityManager& entityManager, const float deltaTimeSeconds,
                                      const bool editorPreviewOnly,
                                      const std::optional<float> fallbackFloorPlaneY) {
    if (!std::isfinite(deltaTimeSeconds) || !(deltaTimeSeconds > 0.0F)) return 0U;

    std::vector<Entity> ragdollEntities;
    entityManager.ForEach<Ragdoll3DComponent>(
        [&ragdollEntities, editorPreviewOnly](const Entity entity, Ragdoll3DComponent& ragdoll) {
            if (!ragdoll.enabled) return;
            if (editorPreviewOnly && !ragdoll.editorPreviewSimulating) return;
            if (ragdollEntities.size() < kMaximumRagdollEntitiesPerUpdate) {
                ragdollEntities.push_back(entity);
            }
        });
    if (ragdollEntities.empty()) return 0U;

    const bool hasFloorPlane = fallbackFloorPlaneY.has_value() && std::isfinite(*fallbackFloorPlaneY);

    const std::vector<Physics::Detail::ColliderWorldAabb> colliders =
        Physics::Detail::BuildColliderWorldAabbCache(entityManager);

    // Collect dynamic Rigid3D motion & inverse mass once per frame for moving-body waking and two-way pushback.
    std::vector<Physics::RagdollColliderMotion> colliderMotions(colliders.size());
    for (std::size_t i = 0U; i < colliders.size(); ++i) {
        const Entity colEntity = colliders[i].entity;
        if (!entityManager.IsAlive(colEntity) || !entityManager.HasComponent<Rigid3DComponent>(colEntity)) {
            continue;
        }
        const Rigid3DComponent& rigid = entityManager.GetComponent<Rigid3DComponent>(colEntity);
        const float invMass = ResolveRigid3DInverseMass(rigid);
        Physics::RagdollColliderMotion motion;
        motion.linearVelocity = rigid.velocity;
        motion.angularVelocity = rigid.angularVelocity;
        motion.worldCenter = colliders[i].worldAabb.GetCenter();
        motion.inverseMass = invMass;
        motion.isDynamic = !rigid.isKinematic && invMass > 0.0F;
        colliderMotions[i] = motion;
    }

    std::vector<std::size_t> broadphaseCandidates;
    broadphaseCandidates.reserve(32U);
    std::size_t advancedCount = 0U;

    for (const Entity entity : ragdollEntities) {
        if (!entityManager.IsAlive(entity) || !entityManager.HasComponent<Ragdoll3DComponent>(entity) ||
            entityManager.HasComponent<DisabledComponent>(entity)) {
            continue;
        }
        Ragdoll3DComponent& ragdoll = entityManager.GetComponent<Ragdoll3DComponent>(entity);
        if (!ragdoll.enabled) continue;
        if (editorPreviewOnly && !ragdoll.editorPreviewSimulating) continue;
        if (!ragdoll.initialized && !InitializeRagdoll3D(entityManager, entity)) continue;
        if (!entityManager.IsAlive(ragdoll.skeleton) ||
            !entityManager.HasComponent<Skeleton3DComponent>(ragdoll.skeleton)) {
            ragdoll.initialized = false;
            continue;
        }
        Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(ragdoll.skeleton);
        ObjectWorldFrame skeletonFrame;
        if (!ResolveSkeletonWorldFrame(entityManager, ragdoll.skeleton, skeletonFrame)) continue;

        // ---- Mode 1: Kinematic (animation controls bodies; record velocities for activation) ----
        if (ragdoll.mode == RagdollMode::Kinematic) {
            SyncBodiesFromAnimatedPose(ragdoll, skeleton, skeletonFrame, deltaTimeSeconds,
                                          /*kinematicOnlyBodies=*/false);
            if (ragdoll.automaticReactions && !editorPreviewOnly) {
                static_cast<void>(UpdateLandingReaction(entityManager, entity, ragdoll));
            }
            continue;
        }

        // ---- Mode 2: BlendingOut: the fallen pose stands back up into the target pose ----
        if (ragdoll.mode == RagdollMode::BlendingOut) {
            EnsureSkeletonPose(skeleton);
            ragdoll.blendOutElapsedSeconds += deltaTimeSeconds;
            const float duration = std::max(1.0e-4F, ragdoll.blendOutDurationSeconds);
            const float t = std::clamp(ragdoll.blendOutElapsedSeconds / duration, 0.0F, 1.0F);
            // Smoothstep easing for a natural, organic get-up.
            const float smoothT = t * t * (3.0F - 2.0F * t);
            // The target is the pose the simulation started from (an editor preview has no animation under
            // it), or - for gameplay - whatever the animation system just wrote into the pose this frame.
            if (!ragdoll.recoverToCapturedPose && ragdoll.blendOutSnapshot.valid &&
                ragdoll.blendOutElapsedSeconds <= deltaTimeSeconds * 1.001F &&
                ragdoll.recoverTargetPose.size() == skeleton.pose.size() &&
                ragdoll.blendOutSnapshot.bonePoses.size() == skeleton.pose.size()) {
                // Still showing exactly the fallen pose on the first step: nothing is animating this skeleton,
                // so the body stands up into the pose it started from instead of staying where it fell.
                bool untouched = true;
                for (std::size_t i = 0U; i < skeleton.pose.size() && untouched; ++i) {
                    const Math::Vector3 d = skeleton.pose[i].position - ragdoll.blendOutSnapshot.bonePoses[i].position;
                    untouched = d.x * d.x + d.y * d.y + d.z * d.z < 1.0e-10F;
                }
                if (untouched) ragdoll.recoverToCapturedPose = true;
            }
            const bool toCaptured =
                ragdoll.recoverToCapturedPose && ragdoll.recoverTargetPose.size() == skeleton.pose.size();
            if (ragdoll.blendOutSnapshot.valid) {
                const std::size_t count = std::min(skeleton.pose.size(), ragdoll.blendOutSnapshot.bonePoses.size());
                for (std::size_t i = 0U; i < count; ++i) {
                    const SkeletonBonePose target = toCaptured ? ragdoll.recoverTargetPose[i] : skeleton.pose[i];
                    skeleton.pose[i].position =
                        LerpVector(ragdoll.blendOutSnapshot.bonePoses[i].position, target.position, smoothT);
                    skeleton.pose[i].rotation =
                        SlerpQuaternion(ragdoll.blendOutSnapshot.bonePoses[i].rotation, target.rotation, smoothT);
                    skeleton.pose[i].scale = target.scale;
                }
            }
            // The bodies stand up with the mesh, so colliders and debug capsules follow the pose.
            SyncBodiesFromAnimatedPose(ragdoll, skeleton, skeletonFrame, deltaTimeSeconds,
                                          /*kinematicOnlyBodies=*/false);
            ++advancedCount;
            if (t >= 1.0F) {
                ragdoll.mode = RagdollMode::Kinematic;
                ragdoll.blendOutSnapshot.valid = false;
                ragdoll.recoverToCapturedPose = false;
                ragdoll.restSeconds = 0.0F;
                if (ragdoll.editorPreviewSimulating) {
                    // The preview has done its job: it ends standing, so Simulate can be pressed again.
                    ragdoll.editorPreviewSimulating = false;
                    ragdoll.mode = ragdoll.editorPreviewBackupMode;
                    ragdoll.editorPreviewBackupPose.clear();
                    ragdoll.initialized = false;
                    ragdoll.pendingSeconds = 0.0F;
                }
            }
            continue;
        }

        // ---- Mode 3 & 4: Simulating (Passive) or Active (PD Motors) ----
        // A body that has come to rest stands back up when asked to (autoRecover).
        if (ragdoll.autoRecover && ragdoll.mode == RagdollMode::Simulating) {
            ragdoll.restSeconds = ragdoll.solver.IsSleeping() ? ragdoll.restSeconds + deltaTimeSeconds : 0.0F;
            if (ragdoll.restSeconds >= ragdoll.recoverDelaySeconds &&
                BeginRagdollRecovery(entityManager, entity)) {
                continue; // the next frame blends
            }
        }
        if (ragdoll.staggering && ragdoll.mode == RagdollMode::Active &&
            UpdateStagger(entityManager, entity, ragdoll, deltaTimeSeconds)) {
            continue;
        }
        bool animatedTargetChanged = false;
        UpdatePartialAndMotorTargets(ragdoll, skeleton, skeletonFrame, deltaTimeSeconds, animatedTargetChanged);

        Physics::RagdollStepSettings effectiveStepSettings{};
        std::uint32_t maxSubsteps = Physics::kMaximumRagdollSubsteps;
        bool dynamicPushback = ragdoll.enableDynamicPushback;
        ResolveEffectiveStepSettings(ragdoll, effectiveStepSettings, maxSubsteps, dynamicPushback);

        Physics::RagdollCollisionFilter filter = ragdoll.filter;
        if (filter.ignoreEntity == kInvalidEntity) filter.ignoreEntity = ragdoll.skeleton;
        if (filter.alsoIgnoreEntity == kInvalidEntity &&
            entityManager.HasComponent<HierarchyComponent>(ragdoll.skeleton)) {
            Entity ancestor = entityManager.GetComponent<HierarchyComponent>(ragdoll.skeleton).parent;
            for (std::uint32_t hops = 0U;
                 hops < 8U && ancestor != kInvalidEntity && entityManager.IsAlive(ancestor); ++hops) {
                if (ancestor != filter.ignoreEntity && entityManager.HasComponent<ColliderComponent>(ancestor)) {
                    filter.alsoIgnoreEntity = ancestor;
                    break;
                }
                if (!entityManager.HasComponent<HierarchyComponent>(ancestor)) {
                    break;
                }
                const Entity next = entityManager.GetComponent<HierarchyComponent>(ancestor).parent;
                if (next == ancestor) break;
                ancestor = next;
            }
        }

        // Broadphase pre-filter once per frame (expanded by max expected displacement during frame).
        const float expandMargin = std::clamp(deltaTimeSeconds * 15.0F + 0.15F, 0.15F, 1.5F);
        Physics::BuildRagdollBroadphaseCandidates(ragdoll.solver, colliders, filter, expandMargin,
                                                     broadphaseCandidates);
        const std::span<const Physics::Detail::ColliderWorldAabb> activeColliders =
            broadphaseCandidates.empty()
                ? std::span<const Physics::Detail::ColliderWorldAabb>{}
                : std::span<const Physics::Detail::ColliderWorldAabb>{colliders};

        std::array<Physics::RagdollContact, Physics::kMaximumRagdollContacts> contacts{};

        // Smart Sleep: never wake a sleeping corpse on resting static floor contacts.
        // Wake only on moving dynamic/kinematic collider hits, lost floor support, or active motor target change.
        if (ragdoll.solver.IsSleeping()) {
            const Physics::RagdollContactStats probeStats =
                (broadphaseCandidates.empty() && !hasFloorPlane)
                    ? Physics::RagdollContactStats{}
                    : Physics::BuildRagdollContacts(ragdoll.solver, activeColliders, filter,
                                                       std::span<Physics::RagdollContact>{contacts},
                                                       colliderMotions, broadphaseCandidates,
                                                       fallbackFloorPlaneY);
            const bool lostFloor = ragdoll.hadFloorSupport && !probeStats.hasFloorSupport;
            const bool activeMotorMoved = ragdoll.mode == RagdollMode::Active && animatedTargetChanged;
            if (probeStats.hasMovingColliderContact || lostFloor || activeMotorMoved) {
                ragdoll.solver.Wake();
            } else {
                ragdoll.hadFloorSupport = probeStats.hasFloorSupport;
                WriteRagdollPoseToSkeleton(entityManager, ragdoll, skeleton, skeletonFrame);
                continue;
            }
        }

        ragdoll.pendingSeconds += deltaTimeSeconds;
        std::size_t substeps = 0U;
        const float stepSeconds = Physics::kRagdollFixedStepSeconds;
        bool lastHadFloorSupport = ragdoll.hadFloorSupport;

        while (ragdoll.pendingSeconds >= stepSeconds && substeps < maxSubsteps) {
            // Refresh world contacts per substep using the fast broadphase candidate list.
            Physics::RagdollContactStats substepStats{};
            if (!broadphaseCandidates.empty() || hasFloorPlane) {
                substepStats = Physics::BuildRagdollContacts(ragdoll.solver, activeColliders, filter,
                                                                std::span<Physics::RagdollContact>{contacts},
                                                                colliderMotions, broadphaseCandidates,
                                                                fallbackFloorPlaneY);
            }
            lastHadFloorSupport = substepStats.hasFloorSupport;

            const std::span<const Physics::RagdollContact> contactSpan{
                contacts.data(), substepStats.generatedContactCount};
            ragdoll.lastReport =
                ragdoll.solver.Step(ragdoll.gravity, stepSeconds, contactSpan, effectiveStepSettings);

            if (dynamicPushback && !ragdoll.solver.GetLastContactFeedback().empty()) {
                ApplyDynamicRigid3DPushback(entityManager, colliders, colliderMotions,
                                               ragdoll.solver.GetLastContactFeedback());
            }

            ragdoll.pendingSeconds -= stepSeconds;
            ++substeps;
        }
        ragdoll.hadFloorSupport = lastHadFloorSupport;
        if (substeps > 0U) ++advancedCount;
        ragdoll.pendingSeconds =
            std::min(ragdoll.pendingSeconds, stepSeconds * static_cast<float>(maxSubsteps));

        WriteRagdollPoseToSkeleton(entityManager, ragdoll, skeleton, skeletonFrame);
    }
    return advancedCount;
}

} // namespace UVE::Scene
