// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "uve/retarget/retarget_files_uve.h"
#include "uve/retarget/retarget_match_uve.h"

namespace UVE::Editor {

/// One bone of the humanoid as the Retarget window lists it: where it stands in the humanoid's
/// hierarchy and how the character's rig answers it.
struct RetargetJointRowUVE final {
    /// Index into the humanoid reference.
    std::int32_t referenceBone = -1;
    /// How deep in the hierarchy, for the tree's indent.
    std::int32_t depth = 0;
    Retarget::JointStatusUVE status = Retarget::JointStatusUVE::Missing;
    /// Why, in a line, for anything but Good.
    std::string reason;
    /// The character's own name for it, empty when it has none.
    std::string characterName;
};

struct RetargetAnimationRowUVE final {
    std::filesystem::path file;
    Retarget::RetargetAnimationStateUVE state = Retarget::RetargetAnimationStateUVE::Unreadable;
    std::string note;
};

/// What conforming a character and some animations would do, worked out without touching a file.
/// The Retarget window shows it; Generate carries it out.
struct RetargetPlanUVE final {
    std::filesystem::path model;
    bool modelReadable = false;
    /// Why the character cannot be conformed, when it cannot.
    std::string modelError;
    /// The character already has the humanoid's bones: conforming again changes little.
    bool modelAlreadyConformed = false;
    /// One row per humanoid bone, parents first.
    std::vector<RetargetJointRowUVE> joints;
    /// The character's bones the humanoid has no place for: kept as they are.
    std::vector<std::string> keptBones;
    /// How tall the character stands against the humanoid.
    float heightScale = 1.0F;
    std::array<std::size_t, Retarget::kJointStatusCountUVE> counts{};
    std::vector<RetargetAnimationRowUVE> animations;

    [[nodiscard]] std::size_t CountUVE(const Retarget::JointStatusUVE status) const noexcept {
        return counts[static_cast<std::size_t>(status)];
    }
    /// How many of the animations Generate will conform.
    [[nodiscard]] std::size_t ReadyAnimationsUVE() const noexcept;
    /// Generate needs a character it can conform and something to do.
    [[nodiscard]] bool CanGenerateUVE() const noexcept {
        return modelReadable && (ReadyAnimationsUVE() > 0U || !modelAlreadyConformed);
    }
};

/// Reads the character (a `.uvmodel` with a skin) and the animations and works out the plan.
[[nodiscard]] RetargetPlanUVE PlanRetargetUVE(const std::filesystem::path& modelFile,
                                              std::span<const std::filesystem::path> animations);

/// A joint colour's name for the window: "good", "check", "broken" or "missing" (grey).
[[nodiscard]] const char* GetRetargetStatusLabelUVE(Retarget::JointStatusUVE status) noexcept;

} // namespace UVE::Editor
