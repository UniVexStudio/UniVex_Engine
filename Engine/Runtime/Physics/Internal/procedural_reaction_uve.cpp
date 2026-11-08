// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/procedural_reaction_uve.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "uve/math/quaternion_uve.h"

namespace UVE::Physics {
namespace {

constexpr float kTwoPi = 6.28318530717958647692F;
constexpr float kDegToRad = 0.01745329251994329577F;

[[nodiscard]] Math::QuaternionUVE FromAxisAngle(const Math::Vector3UVE& axis, const float degrees) noexcept {
    const float radians = degrees * kDegToRad;
    const float halfRad = radians * 0.5F;
    const float s = std::sin(halfRad);
    return Math::QuaternionUVE{axis.x * s, axis.y * s, axis.z * s, std::cos(halfRad)};
}

[[nodiscard]] float HermiteBlend(const float t) noexcept {
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

} // namespace

void GenerateProceduralIdlePoseUVE(
    const float timeSeconds,
    const float intensity,
    const std::span<const Scene::SkeletonBoneUVE> bones,
    std::span<Scene::SkeletonBonePoseUVE> outPose) noexcept {
    if (bones.empty() || outPose.size() < bones.size() || intensity <= 0.0F) return;

    // Respiration: ~0.25 Hz (1 breath every 4 seconds)
    const float breathPhase = std::sin(timeSeconds * (0.25F * kTwoPi));
    const float spinePitch = breathPhase * 1.5F * intensity;
    const float chestPitch = breathPhase * 2.2F * intensity;

    // Pelvic micro-balance sway: ~0.12 Hz (1 sway every 8 seconds)
    const float swayPhase = std::sin(timeSeconds * (0.12F * kTwoPi));
    const float swayCos = std::cos(timeSeconds * (0.12F * kTwoPi));
    const float pelvisRoll = swayPhase * 0.8F * intensity;
    const float pelvisYaw = swayCos * 0.5F * intensity;

    const Math::QuaternionUVE spineRot = FromAxisAngle({1.0F, 0.0F, 0.0F}, spinePitch);
    const Math::QuaternionUVE chestRot = FromAxisAngle({1.0F, 0.0F, 0.0F}, chestPitch);
    const Math::QuaternionUVE pelvisRot =
        Math::MultiplyUVE(FromAxisAngle({0.0F, 0.0F, 1.0F}, pelvisRoll),
                          FromAxisAngle({0.0F, 1.0F, 0.0F}, pelvisYaw));

    for (std::size_t i = 0; i < bones.size(); ++i) {
        const std::string_view name = bones[i].name;
        if (name.find("pelvis") != std::string_view::npos || name.find("hips") != std::string_view::npos) {
            outPose[i].rotation = Math::MultiplyUVE(outPose[i].rotation, pelvisRot);
        } else if (name.find("chest") != std::string_view::npos || name.find("spine1") != std::string_view::npos ||
                   name.find("spine2") != std::string_view::npos) {
            outPose[i].rotation = Math::MultiplyUVE(outPose[i].rotation, chestRot);
        } else if (name.find("spine") != std::string_view::npos) {
            outPose[i].rotation = Math::MultiplyUVE(outPose[i].rotation, spineRot);
        }
    }
}

ProceduralReactionTypeUVE ClassifyImpactReactionUVE(
    const Math::Vector3UVE& contactWorldPoint,
    const Math::Vector3UVE& impulse,
    const Scene::ObjectWorldFrameUVE& characterFrame,
    const float characterHeight) noexcept {
    const float height = std::max(0.5F, characterHeight);

    Math::QuaternionUVE invRot;
    if (!Math::TryInverseUVE(characterFrame.rotation, invRot)) {
        invRot = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F};
    }
    const Math::Vector3UVE localPoint = Math::RotateVectorUVE(invRot, contactWorldPoint - characterFrame.position);
    const Math::Vector3UVE localImpulse = Math::RotateVectorUVE(invRot, impulse);

    const float relativeHeight = localPoint.y / height;

    // Lower body (below 45% height): Leg & Foot strike
    if (relativeHeight < 0.45F) {
        return ProceduralReactionTypeUVE::LegFootHit;
    }

    // Upper body (above 82% height): Head strike
    if (relativeHeight > 0.82F) {
        return ProceduralReactionTypeUVE::HeadHit;
    }

    // Mid-body: Flank vs Chest vs Back
    // Lateral offset threshold: |X| > 0.16m is a flank strike
    if (std::abs(localPoint.x) > 0.16F) {
        return (localPoint.x < 0.0F) ? ProceduralReactionTypeUVE::FlankLeftHit
                                     : ProceduralReactionTypeUVE::FlankRightHit;
    }

    // Front vs Back based on local Z and impulse direction
    if (localPoint.z < 0.0F || localImpulse.z > 0.0F) {
        return ProceduralReactionTypeUVE::ChestGutHit;
    }
    return ProceduralReactionTypeUVE::BackHit;
}

ProceduralReactionTargetsUVE SynthesizeHitReactionUVE(
    const ProceduralReactionTypeUVE reactionType,
    const float reactionTimeSeconds,
    const float intensity) noexcept {
    ProceduralReactionTargetsUVE targets;
    targets.type = reactionType;
    if (reactionType == ProceduralReactionTypeUVE::None || reactionTimeSeconds < 0.0F || intensity <= 0.0F) {
        return targets;
    }

    // Envelope: 0.08s attack to peak (1.0), then exponential/hermite decay over 1.2s to 0.0
    constexpr float kAttackTime = 0.08F;
    constexpr float kDecayDuration = 1.12F;
    constexpr float kTotalDuration = kAttackTime + kDecayDuration;

    if (reactionTimeSeconds >= kTotalDuration) {
        return targets;
    }

    float weight = 0.0F;
    if (reactionTimeSeconds <= kAttackTime) {
        weight = HermiteBlend(reactionTimeSeconds / kAttackTime);
    } else {
        const float decayFraction = (reactionTimeSeconds - kAttackTime) / kDecayDuration;
        weight = 1.0F - HermiteBlend(decayFraction);
    }
    weight *= std::clamp(intensity, 0.1F, 2.0F);
    targets.blendWeight = weight;

    switch (reactionType) {
        case ProceduralReactionTypeUVE::LegFootHit: {
            // Pelvis drops, spine leans slightly forward, opposite leg braces
            targets.pelvisOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, 12.0F * weight);
            targets.spineOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, -8.0F * weight);
            targets.leftLegOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, 25.0F * weight);
            targets.rightLegOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, -15.0F * weight);
            targets.rootTranslationOffset = Math::Vector3UVE{0.0F, -0.15F * weight, -0.10F * weight};
            break;
        }
        case ProceduralReactionTypeUVE::FlankLeftHit: {
            // Torso lateral curl left, left arm clutches ribs, right arm balances outwards
            targets.spineOffset = FromAxisAngle({0.0F, 0.0F, 1.0F}, 22.0F * weight);
            targets.chestOffset = FromAxisAngle({0.0F, 0.0F, 1.0F}, 15.0F * weight);
            targets.leftArmOffset = Math::MultiplyUVE(
                FromAxisAngle({1.0F, 0.0F, 0.0F}, 45.0F * weight),
                FromAxisAngle({0.0F, 1.0F, 0.0F}, -35.0F * weight));
            targets.rightArmOffset = FromAxisAngle({0.0F, 0.0F, 1.0F}, -30.0F * weight);
            targets.neckOffset = FromAxisAngle({0.0F, 0.0F, 1.0F}, -10.0F * weight);
            break;
        }
        case ProceduralReactionTypeUVE::FlankRightHit: {
            // Torso lateral curl right, right arm clutches ribs, left arm balances outwards
            targets.spineOffset = FromAxisAngle({0.0F, 0.0F, 1.0F}, -22.0F * weight);
            targets.chestOffset = FromAxisAngle({0.0F, 0.0F, 1.0F}, -15.0F * weight);
            targets.rightArmOffset = Math::MultiplyUVE(
                FromAxisAngle({1.0F, 0.0F, 0.0F}, 45.0F * weight),
                FromAxisAngle({0.0F, 1.0F, 0.0F}, 35.0F * weight));
            targets.leftArmOffset = FromAxisAngle({0.0F, 0.0F, 1.0F}, 30.0F * weight);
            targets.neckOffset = FromAxisAngle({0.0F, 0.0F, 1.0F}, 10.0F * weight);
            break;
        }
        case ProceduralReactionTypeUVE::ChestGutHit: {
            // Torso forward hunch, arms cross inwards defensively, backward step
            targets.spineOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, -26.0F * weight);
            targets.chestOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, -18.0F * weight);
            targets.headOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, 15.0F * weight);
            targets.leftArmOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, 30.0F * weight);
            targets.rightArmOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, 30.0F * weight);
            targets.rootTranslationOffset = Math::Vector3UVE{0.0F, -0.05F * weight, -0.12F * weight};
            break;
        }
        case ProceduralReactionTypeUVE::HeadHit: {
            // Neck and head snap back/recoil, defensive high guard with both arms
            targets.neckOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, 28.0F * weight);
            targets.headOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, 22.0F * weight);
            targets.leftArmOffset = Math::MultiplyUVE(
                FromAxisAngle({1.0F, 0.0F, 0.0F}, 60.0F * weight),
                FromAxisAngle({0.0F, 1.0F, 0.0F}, 20.0F * weight));
            targets.rightArmOffset = Math::MultiplyUVE(
                FromAxisAngle({1.0F, 0.0F, 0.0F}, 60.0F * weight),
                FromAxisAngle({0.0F, 1.0F, 0.0F}, -20.0F * weight));
            break;
        }
        case ProceduralReactionTypeUVE::BackHit: {
            // Torso arch forward, forward stumble
            targets.spineOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, 24.0F * weight);
            targets.chestOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, 16.0F * weight);
            targets.headOffset = FromAxisAngle({1.0F, 0.0F, 0.0F}, -15.0F * weight);
            targets.rootTranslationOffset = Math::Vector3UVE{0.0F, 0.0F, 0.15F * weight};
            break;
        }
        case ProceduralReactionTypeUVE::None:
            break;
    }

    return targets;
}

} // namespace UVE::Physics
