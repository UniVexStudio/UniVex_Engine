// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_compatibility.h"

#include <gtest/gtest.h>

namespace UVE::Pack::Tests {
namespace {

inline constexpr Core::Version kEngine{2U, 4U, 6U, 8U};

[[nodiscard]] ProjectCompatibilityVerdict VerdictFor(const Platform::EditorProjectVersion& project,
                                                           const ProjectCompatibilityPolicy& policy =
                                                               kDefaultProjectCompatibilityPolicy) {
    return EvaluateProjectCompatibility(project, kEngine, policy).verdict;
}

TEST(ProjectCompatibilityTest, SameVersionIsSame) {
    EXPECT_EQ(VerdictFor({2U, 4U, 6U, 8U}), ProjectCompatibilityVerdict::Same);
}

TEST(ProjectCompatibilityTest, NewerMajorIsIncompatible) {
    const ProjectCompatibility result = EvaluateProjectCompatibility({3U, 0U, 0U, 0U}, kEngine);
    EXPECT_EQ(result.verdict, ProjectCompatibilityVerdict::NewerIncompatible);
    EXPECT_FALSE(result.CanOpen());
    EXPECT_NE(result.message.find("3.0.0+build.0"), std::string::npos);
    EXPECT_NE(result.message.find(kEngine.ToString()), std::string::npos);
}

TEST(ProjectCompatibilityTest, NewerBelowMajorOpensWithWarning) {
    EXPECT_EQ(VerdictFor({2U, 5U, 0U, 0U}), ProjectCompatibilityVerdict::NewerCompatible);
    EXPECT_EQ(VerdictFor({2U, 4U, 7U, 0U}), ProjectCompatibilityVerdict::NewerCompatible);
    EXPECT_EQ(VerdictFor({2U, 4U, 6U, 9U}), ProjectCompatibilityVerdict::NewerCompatible);
}

TEST(ProjectCompatibilityTest, OlderMajorOrMinorNeedsConfirmation) {
    const ProjectCompatibility major = EvaluateProjectCompatibility({1U, 9U, 9U, 9U}, kEngine);
    EXPECT_EQ(major.verdict, ProjectCompatibilityVerdict::OlderNeedsUpgrade);
    EXPECT_TRUE(major.NeedsUpgradeConfirmation());
    EXPECT_TRUE(major.CanOpen());
    EXPECT_EQ(VerdictFor({2U, 3U, 99U, 99U}), ProjectCompatibilityVerdict::OlderNeedsUpgrade);
}

TEST(ProjectCompatibilityTest, OlderPatchOrBuildUpgradesSilently) {
    EXPECT_EQ(VerdictFor({2U, 4U, 5U, 99U}), ProjectCompatibilityVerdict::OlderUpgradesSilently);
    EXPECT_EQ(VerdictFor({2U, 4U, 6U, 7U}), ProjectCompatibilityVerdict::OlderUpgradesSilently);
}

TEST(ProjectCompatibilityTest, MostSignificantDifferenceDecides) {
    // An older minor outweighs a newer patch: the project is older, and needs confirmation.
    EXPECT_EQ(VerdictFor({2U, 3U, 99U, 0U}), ProjectCompatibilityVerdict::OlderNeedsUpgrade);
    // A newer minor outweighs an older patch.
    EXPECT_EQ(VerdictFor({2U, 5U, 0U, 0U}), ProjectCompatibilityVerdict::NewerCompatible);
}

TEST(ProjectCompatibilityTest, PolicyMovesTheBoundaries) {
    ProjectCompatibilityPolicy strict;
    strict.newerRejectedFrom = VersionComponent::Build;
    strict.olderUpgradePromptFrom = VersionComponent::Build;
    EXPECT_EQ(VerdictFor({2U, 4U, 6U, 9U}, strict), ProjectCompatibilityVerdict::NewerIncompatible);
    EXPECT_EQ(VerdictFor({2U, 4U, 6U, 7U}, strict), ProjectCompatibilityVerdict::OlderNeedsUpgrade);
}

TEST(ProjectCompatibilityTest, RecordingFollowsVerdictAndAcceptance) {
    using Verdict = ProjectCompatibilityVerdict;
    EXPECT_TRUE(ShouldRecordEngineVersion(Verdict::Same, false));
    EXPECT_TRUE(ShouldRecordEngineVersion(Verdict::OlderUpgradesSilently, false));
    EXPECT_FALSE(ShouldRecordEngineVersion(Verdict::OlderNeedsUpgrade, false));
    EXPECT_TRUE(ShouldRecordEngineVersion(Verdict::OlderNeedsUpgrade, true));
    EXPECT_FALSE(ShouldRecordEngineVersion(Verdict::NewerCompatible, true));
    EXPECT_FALSE(ShouldRecordEngineVersion(Verdict::NewerIncompatible, true));
}

TEST(ProjectCompatibilityTest, FormatMatchesEngineVersionText) {
    EXPECT_EQ(FormatProjectVersion({2U, 4U, 6U, 8U}), kEngine.ToString());
}

} // namespace
} // namespace UVE::Pack::Tests
