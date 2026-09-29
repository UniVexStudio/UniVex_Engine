// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Retarget {

/// A bone of a skeleton at rest, as much as playback needs: what it is called, its parent (an index
/// into the same list, lower than its own, or -1) and its local position and rotation.
struct RestBoneViewUVE final {
    std::string_view name;
    std::int32_t parent = -1;
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
};

/// How hips-high a skeleton conformed to the humanoid stands: the hips' height over the lowest
/// foot or toe (0 when it has no hips).
[[nodiscard]] float ConformedHipsHeightUVE(std::span<const RestBoneViewUVE> skeleton);

/// How a conformed clip is played on a conformed character.
///
/// A conformed clip stores every bone's translation for the rig it came from, whose limbs may be
/// longer or shorter than the character's. Taken literally that stretches the character. So only the
/// bones that place the body take the clip's translation: the root, the hips and the IK targets,
/// scaled by how tall the character stands against the clip's rig. Every other bone keeps the
/// character's own length and takes only the clip's rotation.
struct ConformedPlaybackUVE final {
    /// False for a clip that is not conformed: nothing is changed then.
    bool active = false;
    /// The character's hips height over the clip rig's.
    float heightScale = 1.0F;

    /// The humanoid bones whose translation comes from the clip: the root, the hips, the IK targets.
    [[nodiscard]] static bool DrivesTranslationUVE(std::string_view boneName);

    /// The position a bone takes from a clip's sampled `sampled`: scaled to the character for a bone
    /// that places the body, else the character's own `rest`. Unchanged when not active.
    [[nodiscard]] Math::Vector3UVE PositionUVE(std::string_view boneName, const Math::Vector3UVE& sampled,
                                               const Math::Vector3UVE& rest) const;

    /// The scale a bone takes: the character's own when active (conformed clips carry none).
    [[nodiscard]] Math::Vector3UVE ScaleUVE(const Math::Vector3UVE& sampled, const Math::Vector3UVE& rest) const {
        return active ? rest : sampled;
    }
};

/// The plan for playing `clip` on a character whose skeleton at rest is `skeleton`. Inactive for a
/// clip that is not conformed.
[[nodiscard]] ConformedPlaybackUVE PlanConformedPlaybackUVE(const Asset::AnimationClipAssetUVE& clip,
                                                           std::span<const RestBoneViewUVE> skeleton);

} // namespace UVE::Retarget
