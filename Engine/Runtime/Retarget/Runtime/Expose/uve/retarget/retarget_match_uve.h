// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "uve/retarget/retarget_humanoid_uve.h"
#include "uve/retarget/retarget_skeleton_uve.h"

namespace UVE::Retarget {

/// How one humanoid bone stands on a rig, as the retarget window colours it.
enum class JointStatusUVE : std::uint8_t {
    /// Found, in the right place in the hierarchy, a sensible length. Green.
    Good = 0,
    /// Found, but something looks off (much longer or shorter than expected, left and right
    /// differ): it will conform, check the result. Yellow.
    Warning,
    /// Found but unusable as it is (under the wrong parent, no length). Red.
    Broken,
    /// The rig has no such bone; conforming adds it. Grey.
    Missing,
};

inline constexpr std::size_t kJointStatusCountUVE = 4U;

struct JointReportUVE final {
    /// The rig's bone standing for this humanoid bone, or -1.
    std::int32_t bone = -1;
    JointStatusUVE status = JointStatusUVE::Missing;
    /// Why, in a line, for anything but Good.
    std::string reason;
};

/// A rig's bones set against the humanoid reference.
struct HumanoidMatchUVE final {
    /// One report per reference bone, in the reference's order.
    std::vector<JointReportUVE> joints;
    /// The reference bone each of the rig's bones stands for, or -1 for a bone the humanoid has
    /// no place for (kept as it is when conforming).
    std::vector<std::int32_t> referenceOfBone;
    /// The rig's hips height over the reference's: how much bigger or smaller it stands.
    float heightScale = 1.0F;
    std::array<std::size_t, kJointStatusCountUVE> counts{};

    [[nodiscard]] std::size_t CountUVE(const JointStatusUVE status) const noexcept {
        return counts[static_cast<std::size_t>(status)];
    }
};

/// Matches a rig (at rest) to the reference by what its bone names mean (MakeBoneKeyUVE), fitting
/// spine and neck chains of any length onto the reference's, then checks each found bone's place
/// in the hierarchy and its length against the reference scaled to the rig's height.
[[nodiscard]] HumanoidMatchUVE MatchHumanoidUVE(const RetargetSkeletonUVE& rig, const HumanoidReferenceUVE& reference);

/// The same for bones known only by name, such as a clip's tracks: every named bone found is
/// Good, the rest Missing; there is no hierarchy or length to check.
[[nodiscard]] HumanoidMatchUVE MatchHumanoidNamesUVE(const std::vector<std::string>& names,
                                                     const HumanoidReferenceUVE& reference);

[[nodiscard]] const char* JointStatusNameUVE(JointStatusUVE status) noexcept;

} // namespace UVE::Retarget
