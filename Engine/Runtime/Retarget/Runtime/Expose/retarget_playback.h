// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "animation_clip_asset.h"
#include "quaternion.h"
#include "vector3.h"

namespace UVE::Retarget {

/// A bone of a skeleton at rest, as much as playback needs: what it is called, its parent (an index
/// into the same list, lower than its own, or -1) and its local position and rotation.
struct RestBoneView final {
    std::string_view name;
    std::int32_t parent = -1;
    Math::Vector3 position{};
    Math::Quaternion rotation{};
};

/// How hips-high a skeleton conformed to the humanoid stands: the hips' height over the lowest
/// foot or toe (0 when it has no hips).
[[nodiscard]] float ConformedHipsHeight(std::span<const RestBoneView> skeleton);

/// How a conformed clip is played on a conformed character.
///
/// A conformed clip stores every bone's translation for the rig it came from, whose limbs may be
/// longer or shorter than the character's. Taken literally that stretches the character. So only the
/// bones that place the body take the clip's translation: the root, the hips and the IK targets,
/// scaled by how tall the character stands against the clip's rig. Every other bone keeps the
/// character's own length and takes only the clip's rotation.
struct ConformedPlayback final {
    /// False for a clip that is not conformed: nothing is changed then.
    bool active = false;
    /// The character's hips height over the clip rig's.
    float heightScale = 1.0F;

    /// The humanoid bones whose translation comes from the clip: the root, the hips, the IK targets.
    [[nodiscard]] static bool DrivesTranslation(std::string_view boneName);

    /// The position a bone takes from a clip's sampled `sampled`: scaled to the character for a bone
    /// that places the body, else the character's own `rest`. Unchanged when not active.
    [[nodiscard]] Math::Vector3 Position(std::string_view boneName, const Math::Vector3& sampled,
                                               const Math::Vector3& rest) const;

    /// The scale a bone takes: the character's own when active (conformed clips carry none).
    [[nodiscard]] Math::Vector3 Scale(const Math::Vector3& sampled, const Math::Vector3& rest) const {
        return active ? rest : sampled;
    }
};

/// The plan for playing `clip` on a character whose skeleton at rest is `skeleton`. Inactive for a
/// clip that is not conformed.
[[nodiscard]] ConformedPlayback PlanConformedPlayback(const Asset::AnimationClipAsset& clip,
                                                           std::span<const RestBoneView> skeleton);

} // namespace UVE::Retarget
