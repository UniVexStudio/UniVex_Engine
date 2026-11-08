// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

namespace UVE::Physics {

/// Anatomical directional procedural hit reactions.
enum class ProceduralReactionTypeUVE : std::uint8_t {
    None = 0,
    LegFootHit,       // Stumble, pelvis drops, opposite leg extends for balance
    FlankLeftHit,     // Torso lateral curl, left arm clutches ribs, right arm balances
    FlankRightHit,    // Torso lateral curl, right arm clutches ribs, left arm balances
    ChestGutHit,      // Forward hunch, backward stumble step
    HeadHit,          // Neck extension/recoil, defensive high guard
    BackHit,          // Torso extension forward, forward stumble step
};

/// Joint target rotation adjustments synthesized for active procedural reaction.
struct ProceduralReactionTargetsUVE final {
    ProceduralReactionTypeUVE type = ProceduralReactionTypeUVE::None;
    float blendWeight = 0.0F; // 0.0 (idle/inactive) to 1.0 (peak hit reaction)

    Math::QuaternionUVE pelvisOffset{};
    Math::QuaternionUVE spineOffset{};
    Math::QuaternionUVE chestOffset{};
    Math::QuaternionUVE neckOffset{};
    Math::QuaternionUVE headOffset{};
    Math::QuaternionUVE leftArmOffset{};
    Math::QuaternionUVE rightArmOffset{};
    Math::QuaternionUVE leftLegOffset{};
    Math::QuaternionUVE rightLegOffset{};

    Math::Vector3UVE rootTranslationOffset{};
};

/// Synthesizes procedural idle pose (chest respiration at 0.25 Hz, pelvic micro-balance at 0.12 Hz).
void GenerateProceduralIdlePoseUVE(
    float timeSeconds,
    float intensity,
    std::span<const Scene::SkeletonBoneUVE> bones,
    std::span<Scene::SkeletonBonePoseUVE> outPose) noexcept;

/// Analyzes an incoming contact point and impulse vector relative to character world frame
/// and classifies it into an anatomical reaction type.
[[nodiscard]] ProceduralReactionTypeUVE ClassifyImpactReactionUVE(
    const Math::Vector3UVE& contactWorldPoint,
    const Math::Vector3UVE& impulse,
    const Scene::ObjectWorldFrameUVE& characterFrame,
    float characterHeight = 1.8F) noexcept;

/// Synthesizes joint target offsets for a specific reaction type and elapsed recovery time.
/// Uses smooth Hermite attack (0.08s) and exponential decay back to zero (1.2s).
[[nodiscard]] ProceduralReactionTargetsUVE SynthesizeHitReactionUVE(
    ProceduralReactionTypeUVE reactionType,
    float reactionTimeSeconds,
    float intensity = 1.0F) noexcept;

} // namespace UVE::Physics
