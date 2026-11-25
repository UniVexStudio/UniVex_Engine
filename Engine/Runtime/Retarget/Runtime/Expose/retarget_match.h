// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "retarget_humanoid.h"
#include "retarget_skeleton.h"

namespace UVE::Retarget {

/// How one humanoid bone stands on a rig, as the retarget window colours it.
enum class JointStatus : std::uint8_t {
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

inline constexpr std::size_t kJointStatusCount = 4U;

struct JointReport final {
    /// The rig's bone standing for this humanoid bone, or -1.
    std::int32_t bone = -1;
    JointStatus status = JointStatus::Missing;
    /// Why, in a line, for anything but Good.
    std::string reason;
    /// Found by the Rig Brain rather than by its name, and how sure it was (0..1).
    bool byBrain = false;
    float confidence = 1.0F;
};

/// A rig's bones set against the humanoid reference.
struct HumanoidMatch final {
    /// One report per reference bone, in the reference's order.
    std::vector<JointReport> joints;
    /// The reference bone each of the rig's bones stands for, or -1 for a bone the humanoid has
    /// no place for (kept as it is when conforming).
    std::vector<std::int32_t> referenceOfBone;
    /// The rig's hips height over the reference's: how much bigger or smaller it stands.
    float heightScale = 1.0F;
    std::array<std::size_t, kJointStatusCount> counts{};

    [[nodiscard]] std::size_t Count(const JointStatus status) const noexcept {
        return counts[static_cast<std::size_t>(status)];
    }
};

/// Matches a rig (at rest) to the reference by what its bone names mean (MakeBoneKey), fitting
/// spine and neck chains of any length onto the reference's, then checks each found bone's place
/// in the hierarchy and its length against the reference scaled to the rig's height.
[[nodiscard]] HumanoidMatch MatchHumanoid(const RetargetSkeleton& rig, const HumanoidReference& reference);

/// The same for bones known only by name, such as a clip's tracks: every named bone found is
/// Good, the rest Missing; there is no hierarchy or length to check.
/// Checks the pairs in `match` against the rig's hierarchy and lengths (MatchHumanoid's second
/// half), for pairs made by other means: under the right parent, a sensible length, left and right
/// alike. Statuses, reasons, counts and the height scale are set again.
[[nodiscard]] HumanoidMatch CheckHumanoidMatch(const RetargetSkeleton& rig, const HumanoidReference& reference,
                                                    HumanoidMatch match);

[[nodiscard]] HumanoidMatch MatchHumanoidNames(const std::vector<std::string>& names,
                                                     const HumanoidReference& reference);

[[nodiscard]] const char* JointStatusName(JointStatus status) noexcept;

/// True when `names` are the humanoid's own (a rig that has been conformed): at least four in five
/// of the reference's bone names appear exactly, case included. A rig from another tool names its
/// bones its own way, so this is not true of one that has not been conformed.
[[nodiscard]] bool AreHumanoidNames(const std::vector<std::string>& names, const HumanoidReference& reference);

} // namespace UVE::Retarget
