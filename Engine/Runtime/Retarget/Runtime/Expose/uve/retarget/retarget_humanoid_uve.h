// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/retarget/retarget_skeleton_uve.h"

namespace UVE::Retarget {

/// What a humanoid bone is for; how retargeting treats it depends on this.
enum class HumanoidBoneKindUVE : std::uint8_t {
    /// The rig's origin on the ground.
    Root = 0,
    /// The body's main chain: hips, spine, neck, head, clavicles, limbs, hands, feet, toes.
    Body,
    Finger,
    /// Shares its parent's twist along the limb.
    Twist,
    /// Shape helpers the skin leans on: correctives, muscle bones, knee and ankle helpers.
    Corrective,
    /// Targets for leg and arm solvers; at rest they sit on the bone they follow.
    IK,
    /// Places things attach to: held props, a mount point, the centre of mass, interaction.
    Socket,
};

/// What the humanoid reference knows about each of its bones besides the rest pose.
struct HumanoidBoneInfoUVE final {
    /// MakeBoneKeyUVE of the bone's name: how bones of other rigs are recognised as this one.
    std::string key;
    HumanoidBoneKindUVE kind = HumanoidBoneKindUVE::Body;
    /// The bone this one points at (upper arm -> forearm), which fixes its direction; -1 for none.
    std::int32_t aim = -1;
    /// An IK bone rests exactly on this bone; -1 for none.
    std::int32_t follows = -1;

    [[nodiscard]] bool operator==(const HumanoidBoneInfoUVE&) const = default;
};

/// The UniVex humanoid: the one skeleton every character and animation is conformed to. Metres,
/// +Y up, facing +Z with its left side toward +X, standing in an A-pose with its feet on the ground
/// at the root: hips, spine and neck straight up, the head level; arms 45 degrees down and straight
/// to the fingertips, elbows bending toward +Z, palms toward the body with the thumb side forward;
/// legs straight down, feet flat as they stood.
struct HumanoidReferenceUVE final {
    RetargetSkeletonUVE skeleton;
    std::vector<HumanoidBoneInfoUVE> info;
    /// Hips above the ground: how tall the rig stands, for scaling hip travel between characters.
    float hipsHeight = 0.0F;

    [[nodiscard]] bool operator==(const HumanoidReferenceUVE&) const = default;
};

/// The reference shipped with the engine (built from the source rig by the reference tool and
/// embedded at build time). Parsed on first use.
[[nodiscard]] const HumanoidReferenceUVE& GetHumanoidReferenceUVE();

/// Makes the reference from a source rig in any pose: every bone renamed to the humanoid's own
/// names (same structure), the pose straightened to the A-pose (the elbows keep the way the source
/// bends them, the head and feet keep how they sat), IK bones set on the bones they follow, and
/// kinds and aims filled in. Every other bone keeps its length. Nothing when a name clashes, the
/// rig has no hips, thighs or shin, or it does not stand up +Y facing +Z.
[[nodiscard]] std::optional<HumanoidReferenceUVE> BuildHumanoidReferenceUVE(const RetargetSkeletonUVE& source,
                                                                           std::string* error = nullptr);

/// The reference as JSON ("uve-humanoid-v1"), and back. Parsing checks the skeleton and indices.
[[nodiscard]] std::string WriteHumanoidReferenceUVE(const HumanoidReferenceUVE& reference);
[[nodiscard]] std::optional<HumanoidReferenceUVE> ParseHumanoidReferenceUVE(std::string_view json,
                                                                           std::string* error = nullptr);

/// Hips height above the lowest foot or toe bone, for any rig; 0 when it has no hips or feet.
[[nodiscard]] float MeasureHipsHeightUVE(const RetargetSkeletonUVE& skeleton, const std::vector<WorldTransformUVE>& world);

[[nodiscard]] const char* HumanoidBoneKindNameUVE(HumanoidBoneKindUVE kind) noexcept;

} // namespace UVE::Retarget
