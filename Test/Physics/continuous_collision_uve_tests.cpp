// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/physics/continuous_collision_uve.h"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

[[nodiscard]] Math::AabbUVE MakeBoxUVE(const Math::Vector3UVE& center,
                                       const Math::Vector3UVE& halfExtents) {
    return Math::AabbUVE::FromCenterExtentsUVE(center, halfExtents);
}

[[nodiscard]] ContinuousCollisionSettingsUVE MakeEnabledSettingsUVE(const float threshold = 0.0F) {
    ContinuousCollisionSettingsUVE settings;
    settings.enabled = true;
    settings.motionThreshold = threshold;
    settings.contactOffset = 0.0F;
    return settings;
}

// ---------------------------------------------------------------------------------------------
// Sphere vs AABB
// ---------------------------------------------------------------------------------------------

TEST(ContinuousCollisionUVETest, SweepAgainstAabb_StopsAtTheInflatedFace) {
    SweptSphereUVE mover{};
    mover.center = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};

    const auto contact = SweepSphereAgainstAabbUVE(
        mover, MakeBoxUVE(Math::Vector3UVE{4.5F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 1.0F, 1.0F}));

    ASSERT_TRUE(contact.has_value());
    // The box's -X face sits at x = 4, inflated by the 0.5 radius to 3.5, over a 10 m sweep.
    EXPECT_NEAR(contact->timeOfImpact, 0.35F, 1.0e-5F);
    EXPECT_NEAR(contact->position.x, 3.5F, 1.0e-4F);
    EXPECT_NEAR(contact->normal.x, -1.0F, 1.0e-6F);
    EXPECT_NEAR(contact->normal.y, 0.0F, 1.0e-6F);
    EXPECT_NEAR(contact->normal.z, 0.0F, 1.0e-6F);
}

TEST(ContinuousCollisionUVETest, SweepAgainstAabb_MissesWhenTheSweepPassesBeside) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};

    // 2 m above a box only 1 m tall: the sphere never touches it.
    EXPECT_FALSE(SweepSphereAgainstAabbUVE(
                     mover, MakeBoxUVE(Math::Vector3UVE{4.5F, 2.0F, 0.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F}))
                     .has_value());
}

TEST(ContinuousCollisionUVETest, SweepAgainstAabb_IgnoresGeometryBeyondTheSweep) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{2.0F, 0.0F, 0.0F};

    EXPECT_FALSE(SweepSphereAgainstAabbUVE(
                     mover, MakeBoxUVE(Math::Vector3UVE{10.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 1.0F, 1.0F}))
                     .has_value());
}

TEST(ContinuousCollisionUVETest, SweepAgainstAabb_ReportsAnImmediateContactWhenAlreadyOverlapping) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{5.0F, 0.0F, 0.0F};

    // The mover's centre starts inside the (inflated) box, which a slab test cannot express.
    const auto contact = SweepSphereAgainstAabbUVE(
        mover, MakeBoxUVE(Math::Vector3UVE{0.2F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F}));
    ASSERT_TRUE(contact.has_value());
    EXPECT_FLOAT_EQ(contact->timeOfImpact, 0.0F);
    // ... and the normal is a unit axis pointing out of the shallowest face.
    const float lengthSquared = contact->normal.x * contact->normal.x +
                                contact->normal.y * contact->normal.y +
                                contact->normal.z * contact->normal.z;
    EXPECT_NEAR(lengthSquared, 1.0F, 1.0e-5F);
}

TEST(ContinuousCollisionUVETest, SweepAgainstAabb_RejectsDegenerateTargets) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    // max < min is not a box; treating it as one would invent an impact plane.
    EXPECT_FALSE(SweepSphereAgainstAabbUVE(
                     mover, Math::AabbUVE{Math::Vector3UVE{5.0F, 1.0F, 1.0F},
                                          Math::Vector3UVE{4.0F, -1.0F, -1.0F}})
                     .has_value());
}

// ---------------------------------------------------------------------------------------------
// Sphere vs sphere
// ---------------------------------------------------------------------------------------------

TEST(ContinuousCollisionUVETest, SweepAgainstSphere_StopsWhenTheSurfacesMeet) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};

    const auto contact =
        SweepSphereAgainstSphereUVE(mover, Math::Vector3UVE{5.0F, 0.0F, 0.0F}, 0.5F);

    ASSERT_TRUE(contact.has_value());
    // Contact when the centres are 1.0 apart: 4 m of travel over a 10 m sweep.
    EXPECT_NEAR(contact->timeOfImpact, 0.4F, 1.0e-5F);
    EXPECT_NEAR(contact->position.x, 4.0F, 1.0e-4F);
    EXPECT_NEAR(contact->normal.x, -1.0F, 1.0e-6F);
}

TEST(ContinuousCollisionUVETest, SweepAgainstSphere_MissesWhenPassingAbove) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    EXPECT_FALSE(
        SweepSphereAgainstSphereUVE(mover, Math::Vector3UVE{5.0F, 3.0F, 0.0F}, 0.5F).has_value());
}

TEST(ContinuousCollisionUVETest, SweepAgainstSphere_ReportsImmediateContactWhenOverlapped) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    const auto contact = SweepSphereAgainstSphereUVE(mover, Math::Vector3UVE{0.25F, 0.0F, 0.0F}, 0.5F);
    ASSERT_TRUE(contact.has_value());
    EXPECT_FLOAT_EQ(contact->timeOfImpact, 0.0F);
    EXPECT_NEAR(contact->normal.x, -1.0F, 1.0e-5F);
}

// ---------------------------------------------------------------------------------------------
// Multi-target determinism
// ---------------------------------------------------------------------------------------------

TEST(ContinuousCollisionUVETest, SweepAgainstAabbs_PicksTheNearestTarget) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    const std::vector<Math::AabbUVE> targets{
        MakeBoxUVE(Math::Vector3UVE{8.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 1.0F, 1.0F}),
        MakeBoxUVE(Math::Vector3UVE{3.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 1.0F, 1.0F})};

    const auto contact = SweepSphereAgainstAabbsUVE(mover, targets);
    ASSERT_TRUE(contact.has_value());
    // Nearest is the second entry; the sweep must not report the first one it happens to test.
    EXPECT_NEAR(contact->position.x, 2.0F, 1.0e-4F);
}

TEST(ContinuousCollisionUVETest, SweepAgainstAabbs_BreaksTiesByEnumerationOrder) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    // Two boxes whose near faces are the same distance away, on opposite sides of the path.
    const std::vector<Math::AabbUVE> targets{
        MakeBoxUVE(Math::Vector3UVE{4.5F, 1.5F, 0.0F}, Math::Vector3UVE{0.5F, 1.0F, 1.0F}),
        MakeBoxUVE(Math::Vector3UVE{4.5F, -1.5F, 0.0F}, Math::Vector3UVE{0.5F, 1.0F, 1.0F})};

    const auto contact = SweepSphereAgainstAabbsUVE(mover, targets);
    ASSERT_TRUE(contact.has_value());
    EXPECT_NEAR(contact->timeOfImpact, 0.35F, 1.0e-5F);
    // Two identical impacts means the tie is real, and the contract is that the FIRST target in
    // enumeration order wins - so repeating the query can never alternate between them.
    for (int repeat = 0; repeat < 4; ++repeat) {
        const auto again = SweepSphereAgainstAabbsUVE(mover, targets);
        ASSERT_TRUE(again.has_value());
        EXPECT_FLOAT_EQ(again->timeOfImpact, contact->timeOfImpact);
        EXPECT_EQ(again->position, contact->position);
        EXPECT_EQ(again->normal, contact->normal);
    }
}

// ---------------------------------------------------------------------------------------------
// Resolved motion
// ---------------------------------------------------------------------------------------------

TEST(ContinuousCollisionUVETest, Resolve_LeavesMotionAloneWhenDisabled) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    const std::vector<Math::AabbUVE> targets{
        MakeBoxUVE(Math::Vector3UVE{2.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 1.0F, 1.0F})};
    ContinuousCollisionSettingsUVE settings;  // disabled by default

    const ContinuousMotionResultUVE result =
        ResolveContinuousMotionUVE(mover, targets, settings);
    EXPECT_FLOAT_EQ(result.displacement.x, 10.0F);
    EXPECT_FALSE(result.contact.has_value());
}

TEST(ContinuousCollisionUVETest, Resolve_SkipsTheSweepBelowTheMotionThreshold) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{0.1F, 0.0F, 0.0F};
    const std::vector<Math::AabbUVE> targets{
        MakeBoxUVE(Math::Vector3UVE{0.05F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 1.0F, 1.0F})};

    const ContinuousMotionResultUVE result =
        ResolveContinuousMotionUVE(mover, targets, MakeEnabledSettingsUVE(0.5F));
    EXPECT_FLOAT_EQ(result.displacement.x, 0.1F);
    EXPECT_FALSE(result.contact.has_value());
}

TEST(ContinuousCollisionUVETest, Resolve_ClampsTheStepToTheFirstContact) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    const std::vector<Math::AabbUVE> targets{
        MakeBoxUVE(Math::Vector3UVE{4.5F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 1.0F, 1.0F})};

    const ContinuousMotionResultUVE result =
        ResolveContinuousMotionUVE(mover, targets, MakeEnabledSettingsUVE());
    ASSERT_TRUE(result.contact.has_value());
    EXPECT_NEAR(result.displacement.x, 3.5F, 1.0e-4F);
    EXPECT_NEAR(result.contact->position.x, 3.5F, 1.0e-4F);
}

TEST(ContinuousCollisionUVETest, Resolve_ContactOffsetPullsTheMoverBackByExactlyThatGap) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    const std::vector<Math::AabbUVE> targets{
        MakeBoxUVE(Math::Vector3UVE{4.5F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 1.0F, 1.0F})};
    ContinuousCollisionSettingsUVE settings = MakeEnabledSettingsUVE();
    settings.contactOffset = 0.25F;

    const ContinuousMotionResultUVE result =
        ResolveContinuousMotionUVE(mover, targets, settings);
    ASSERT_TRUE(result.contact.has_value());
    // 3.5 m to the surface, minus the authored 0.25 m gap.
    EXPECT_NEAR(result.displacement.x, 3.25F, 1.0e-4F);
}

TEST(ContinuousCollisionUVETest, Resolve_AlreadyTouchingReportsContactWithoutFreezingMotion) {
    SweptSphereUVE mover{};
    mover.center = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    mover.radius = 0.5F;
    // Sliding along a surface the sphere already touches must not be clamped to zero.
    mover.displacement = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    const std::vector<Math::AabbUVE> targets{
        MakeBoxUVE(Math::Vector3UVE{0.0F, -0.75F, 0.0F}, Math::Vector3UVE{5.0F, 0.25F, 5.0F})};

    const ContinuousMotionResultUVE result =
        ResolveContinuousMotionUVE(mover, targets, MakeEnabledSettingsUVE());
    ASSERT_TRUE(result.contact.has_value());
    EXPECT_FLOAT_EQ(result.contact->timeOfImpact, 0.0F);
    // The contact is reported (so the caller can drop velocity into the floor) but the tangential
    // motion survives - freezing here would make every grounded body unable to move.
    EXPECT_FLOAT_EQ(result.displacement.x, 10.0F);
}

TEST(ContinuousCollisionUVETest, Resolve_ZeroDisplacementNeverReportsContact) {
    SweptSphereUVE mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    const std::vector<Math::AabbUVE> targets{
        MakeBoxUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F})};
    const ContinuousMotionResultUVE result =
        ResolveContinuousMotionUVE(mover, targets, MakeEnabledSettingsUVE());
    EXPECT_FALSE(result.contact.has_value());
    EXPECT_FLOAT_EQ(result.displacement.x, 0.0F);
}

} // namespace
} // namespace UVE::Physics::Tests
