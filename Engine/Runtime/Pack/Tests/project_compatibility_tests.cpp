// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_compatibility.h"

#include <gtest/gtest.h>

namespace UVE::Pack::Tests {
namespace {

inline constexpr Core::VersionUVE kEngineUVE{2U, 4U, 6U, 8U};

[[nodiscard]] ProjectCompatibilityVerdictUVE VerdictForUVE(const Platform::EditorProjectVersionUVE& project,
                                                           const ProjectCompatibilityPolicyUVE& policy =
                                                               kDefaultProjectCompatibilityPolicyUVE) {
    return EvaluateProjectCompatibilityUVE(project, kEngineUVE, policy).verdict;
}

TEST(ProjectCompatibilityUVETest, SameVersionIsSame) {
    EXPECT_EQ(VerdictForUVE({2U, 4U, 6U, 8U}), ProjectCompatibilityVerdictUVE::Same);
}

TEST(ProjectCompatibilityUVETest, NewerMajorIsIncompatible) {
    const ProjectCompatibilityUVE result = EvaluateProjectCompatibilityUVE({3U, 0U, 0U, 0U}, kEngineUVE);
    EXPECT_EQ(result.verdict, ProjectCompatibilityVerdictUVE::NewerIncompatible);
    EXPECT_FALSE(result.CanOpenUVE());
    EXPECT_NE(result.message.find("3.0.0+build.0"), std::string::npos);
    EXPECT_NE(result.message.find(kEngineUVE.ToStringUVE()), std::string::npos);
}

TEST(ProjectCompatibilityUVETest, NewerBelowMajorOpensWithWarning) {
    EXPECT_EQ(VerdictForUVE({2U, 5U, 0U, 0U}), ProjectCompatibilityVerdictUVE::NewerCompatible);
    EXPECT_EQ(VerdictForUVE({2U, 4U, 7U, 0U}), ProjectCompatibilityVerdictUVE::NewerCompatible);
    EXPECT_EQ(VerdictForUVE({2U, 4U, 6U, 9U}), ProjectCompatibilityVerdictUVE::NewerCompatible);
}

TEST(ProjectCompatibilityUVETest, OlderMajorOrMinorNeedsConfirmation) {
    const ProjectCompatibilityUVE major = EvaluateProjectCompatibilityUVE({1U, 9U, 9U, 9U}, kEngineUVE);
    EXPECT_EQ(major.verdict, ProjectCompatibilityVerdictUVE::OlderNeedsUpgrade);
    EXPECT_TRUE(major.NeedsUpgradeConfirmationUVE());
    EXPECT_TRUE(major.CanOpenUVE());
    EXPECT_EQ(VerdictForUVE({2U, 3U, 99U, 99U}), ProjectCompatibilityVerdictUVE::OlderNeedsUpgrade);
}

TEST(ProjectCompatibilityUVETest, OlderPatchOrBuildUpgradesSilently) {
    EXPECT_EQ(VerdictForUVE({2U, 4U, 5U, 99U}), ProjectCompatibilityVerdictUVE::OlderUpgradesSilently);
    EXPECT_EQ(VerdictForUVE({2U, 4U, 6U, 7U}), ProjectCompatibilityVerdictUVE::OlderUpgradesSilently);
}

TEST(ProjectCompatibilityUVETest, MostSignificantDifferenceDecides) {
    // An older minor outweighs a newer patch: the project is older, and needs confirmation.
    EXPECT_EQ(VerdictForUVE({2U, 3U, 99U, 0U}), ProjectCompatibilityVerdictUVE::OlderNeedsUpgrade);
    // A newer minor outweighs an older patch.
    EXPECT_EQ(VerdictForUVE({2U, 5U, 0U, 0U}), ProjectCompatibilityVerdictUVE::NewerCompatible);
}

TEST(ProjectCompatibilityUVETest, PolicyMovesTheBoundaries) {
    ProjectCompatibilityPolicyUVE strict;
    strict.newerRejectedFrom = VersionComponentUVE::Build;
    strict.olderUpgradePromptFrom = VersionComponentUVE::Build;
    EXPECT_EQ(VerdictForUVE({2U, 4U, 6U, 9U}, strict), ProjectCompatibilityVerdictUVE::NewerIncompatible);
    EXPECT_EQ(VerdictForUVE({2U, 4U, 6U, 7U}, strict), ProjectCompatibilityVerdictUVE::OlderNeedsUpgrade);
}

TEST(ProjectCompatibilityUVETest, RecordingFollowsVerdictAndAcceptance) {
    using VerdictUVE = ProjectCompatibilityVerdictUVE;
    EXPECT_TRUE(ShouldRecordEngineVersionUVE(VerdictUVE::Same, false));
    EXPECT_TRUE(ShouldRecordEngineVersionUVE(VerdictUVE::OlderUpgradesSilently, false));
    EXPECT_FALSE(ShouldRecordEngineVersionUVE(VerdictUVE::OlderNeedsUpgrade, false));
    EXPECT_TRUE(ShouldRecordEngineVersionUVE(VerdictUVE::OlderNeedsUpgrade, true));
    EXPECT_FALSE(ShouldRecordEngineVersionUVE(VerdictUVE::NewerCompatible, true));
    EXPECT_FALSE(ShouldRecordEngineVersionUVE(VerdictUVE::NewerIncompatible, true));
}

TEST(ProjectCompatibilityUVETest, FormatMatchesEngineVersionText) {
    EXPECT_EQ(FormatProjectVersionUVE({2U, 4U, 6U, 8U}), kEngineUVE.ToStringUVE());
}

} // namespace
} // namespace UVE::Pack::Tests
