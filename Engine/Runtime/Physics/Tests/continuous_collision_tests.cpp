// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "continuous_collision.h"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

[[nodiscard]] Math::Aabb MakeBox(const Math::Vector3& center,
                                       const Math::Vector3& halfExtents) {
    return Math::Aabb::FromCenterExtents(center, halfExtents);
}

[[nodiscard]] ContinuousCollisionSettings MakeEnabledSettings(const float threshold = 0.0F) {
    ContinuousCollisionSettings settings;
    settings.enabled = true;
    settings.motionThreshold = threshold;
    settings.contactOffset = 0.0F;
    return settings;
}

// ---------------------------------------------------------------------------------------------
// Sphere vs AABB
// ---------------------------------------------------------------------------------------------

TEST(ContinuousCollisionTest, SweepAgainstAabb_StopsAtTheInflatedFace) {
    SweptSphere mover{};
    mover.center = Math::Vector3{0.0F, 0.0F, 0.0F};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};

    const auto contact = SweepSphereAgainstAabb(
        mover, MakeBox(Math::Vector3{4.5F, 0.0F, 0.0F}, Math::Vector3{0.5F, 1.0F, 1.0F}));

    ASSERT_TRUE(contact.has_value());
    // The box's -X face sits at x = 4, inflated by the 0.5 radius to 3.5, over a 10 m sweep.
    EXPECT_NEAR(contact->timeOfImpact, 0.35F, 1.0e-5F);
    EXPECT_NEAR(contact->position.x, 3.5F, 1.0e-4F);
    EXPECT_NEAR(contact->normal.x, -1.0F, 1.0e-6F);
    EXPECT_NEAR(contact->normal.y, 0.0F, 1.0e-6F);
    EXPECT_NEAR(contact->normal.z, 0.0F, 1.0e-6F);
}

TEST(ContinuousCollisionTest, SweepAgainstAabb_MissesWhenTheSweepPassesBeside) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};

    // 2 m above a box only 1 m tall: the sphere never touches it.
    EXPECT_FALSE(SweepSphereAgainstAabb(
                     mover, MakeBox(Math::Vector3{4.5F, 2.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F}))
                     .has_value());
}

TEST(ContinuousCollisionTest, SweepAgainstAabb_IgnoresGeometryBeyondTheSweep) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{2.0F, 0.0F, 0.0F};

    EXPECT_FALSE(SweepSphereAgainstAabb(
                     mover, MakeBox(Math::Vector3{10.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 1.0F, 1.0F}))
                     .has_value());
}

TEST(ContinuousCollisionTest, SweepAgainstAabb_ReportsAnImmediateContactWhenAlreadyOverlapping) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{5.0F, 0.0F, 0.0F};

    // The mover's centre starts inside the (inflated) box, which a slab test cannot express.
    const auto contact = SweepSphereAgainstAabb(
        mover, MakeBox(Math::Vector3{0.2F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F}));
    ASSERT_TRUE(contact.has_value());
    EXPECT_FLOAT_EQ(contact->timeOfImpact, 0.0F);
    // ... and the normal is a unit axis pointing out of the shallowest face.
    const float lengthSquared = contact->normal.x * contact->normal.x +
                                contact->normal.y * contact->normal.y +
                                contact->normal.z * contact->normal.z;
    EXPECT_NEAR(lengthSquared, 1.0F, 1.0e-5F);
}

TEST(ContinuousCollisionTest, SweepAgainstAabb_RejectsDegenerateTargets) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};
    // max < min is not a box; treating it as one would invent an impact plane.
    EXPECT_FALSE(SweepSphereAgainstAabb(
                     mover, Math::Aabb{Math::Vector3{5.0F, 1.0F, 1.0F},
                                          Math::Vector3{4.0F, -1.0F, -1.0F}})
                     .has_value());
}

// ---------------------------------------------------------------------------------------------
// Sphere vs sphere
// ---------------------------------------------------------------------------------------------

TEST(ContinuousCollisionTest, SweepAgainstSphere_StopsWhenTheSurfacesMeet) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};

    const auto contact =
        SweepSphereAgainstSphere(mover, Math::Vector3{5.0F, 0.0F, 0.0F}, 0.5F);

    ASSERT_TRUE(contact.has_value());
    // Contact when the centres are 1.0 apart: 4 m of travel over a 10 m sweep.
    EXPECT_NEAR(contact->timeOfImpact, 0.4F, 1.0e-5F);
    EXPECT_NEAR(contact->position.x, 4.0F, 1.0e-4F);
    EXPECT_NEAR(contact->normal.x, -1.0F, 1.0e-6F);
}

TEST(ContinuousCollisionTest, SweepAgainstSphere_MissesWhenPassingAbove) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};
    EXPECT_FALSE(
        SweepSphereAgainstSphere(mover, Math::Vector3{5.0F, 3.0F, 0.0F}, 0.5F).has_value());
}

TEST(ContinuousCollisionTest, SweepAgainstSphere_ReportsImmediateContactWhenOverlapped) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};
    const auto contact = SweepSphereAgainstSphere(mover, Math::Vector3{0.25F, 0.0F, 0.0F}, 0.5F);
    ASSERT_TRUE(contact.has_value());
    EXPECT_FLOAT_EQ(contact->timeOfImpact, 0.0F);
    EXPECT_NEAR(contact->normal.x, -1.0F, 1.0e-5F);
}

// ---------------------------------------------------------------------------------------------
// Multi-target determinism
// ---------------------------------------------------------------------------------------------

TEST(ContinuousCollisionTest, SweepAgainstAabbs_PicksTheNearestTarget) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};
    const std::vector<Math::Aabb> targets{
        MakeBox(Math::Vector3{8.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 1.0F, 1.0F}),
        MakeBox(Math::Vector3{3.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 1.0F, 1.0F})};

    const auto contact = SweepSphereAgainstAabbs(mover, targets);
    ASSERT_TRUE(contact.has_value());
    // Nearest is the second entry; the sweep must not report the first one it happens to test.
    EXPECT_NEAR(contact->position.x, 2.0F, 1.0e-4F);
}

TEST(ContinuousCollisionTest, SweepAgainstAabbs_BreaksTiesByEnumerationOrder) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};
    // Two boxes whose near faces are the same distance away, on opposite sides of the path.
    const std::vector<Math::Aabb> targets{
        MakeBox(Math::Vector3{4.5F, 1.5F, 0.0F}, Math::Vector3{0.5F, 1.0F, 1.0F}),
        MakeBox(Math::Vector3{4.5F, -1.5F, 0.0F}, Math::Vector3{0.5F, 1.0F, 1.0F})};

    const auto contact = SweepSphereAgainstAabbs(mover, targets);
    ASSERT_TRUE(contact.has_value());
    EXPECT_NEAR(contact->timeOfImpact, 0.35F, 1.0e-5F);
    // Two identical impacts means the tie is real, and the contract is that the FIRST target in
    // enumeration order wins - so repeating the query can never alternate between them.
    for (int repeat = 0; repeat < 4; ++repeat) {
        const auto again = SweepSphereAgainstAabbs(mover, targets);
        ASSERT_TRUE(again.has_value());
        EXPECT_FLOAT_EQ(again->timeOfImpact, contact->timeOfImpact);
        EXPECT_EQ(again->position, contact->position);
        EXPECT_EQ(again->normal, contact->normal);
    }
}

// ---------------------------------------------------------------------------------------------
// Resolved motion
// ---------------------------------------------------------------------------------------------

TEST(ContinuousCollisionTest, Resolve_LeavesMotionAloneWhenDisabled) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};
    const std::vector<Math::Aabb> targets{
        MakeBox(Math::Vector3{2.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 1.0F, 1.0F})};
    ContinuousCollisionSettings settings;  // disabled by default

    const ContinuousMotionResult result =
        ResolveContinuousMotion(mover, targets, settings);
    EXPECT_FLOAT_EQ(result.displacement.x, 10.0F);
    EXPECT_FALSE(result.contact.has_value());
}

TEST(ContinuousCollisionTest, Resolve_SkipsTheSweepBelowTheMotionThreshold) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{0.1F, 0.0F, 0.0F};
    const std::vector<Math::Aabb> targets{
        MakeBox(Math::Vector3{0.05F, 0.0F, 0.0F}, Math::Vector3{0.5F, 1.0F, 1.0F})};

    const ContinuousMotionResult result =
        ResolveContinuousMotion(mover, targets, MakeEnabledSettings(0.5F));
    EXPECT_FLOAT_EQ(result.displacement.x, 0.1F);
    EXPECT_FALSE(result.contact.has_value());
}

TEST(ContinuousCollisionTest, Resolve_ClampsTheStepToTheFirstContact) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};
    const std::vector<Math::Aabb> targets{
        MakeBox(Math::Vector3{4.5F, 0.0F, 0.0F}, Math::Vector3{0.5F, 1.0F, 1.0F})};

    const ContinuousMotionResult result =
        ResolveContinuousMotion(mover, targets, MakeEnabledSettings());
    ASSERT_TRUE(result.contact.has_value());
    EXPECT_NEAR(result.displacement.x, 3.5F, 1.0e-4F);
    EXPECT_NEAR(result.contact->position.x, 3.5F, 1.0e-4F);
}

TEST(ContinuousCollisionTest, Resolve_ContactOffsetPullsTheMoverBackByExactlyThatGap) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};
    const std::vector<Math::Aabb> targets{
        MakeBox(Math::Vector3{4.5F, 0.0F, 0.0F}, Math::Vector3{0.5F, 1.0F, 1.0F})};
    ContinuousCollisionSettings settings = MakeEnabledSettings();
    settings.contactOffset = 0.25F;

    const ContinuousMotionResult result =
        ResolveContinuousMotion(mover, targets, settings);
    ASSERT_TRUE(result.contact.has_value());
    // 3.5 m to the surface, minus the authored 0.25 m gap.
    EXPECT_NEAR(result.displacement.x, 3.25F, 1.0e-4F);
}

TEST(ContinuousCollisionTest, Resolve_AlreadyTouchingReportsContactWithoutFreezingMotion) {
    SweptSphere mover{};
    mover.center = Math::Vector3{0.0F, 0.0F, 0.0F};
    mover.radius = 0.5F;
    // Sliding along a surface the sphere already touches must not be clamped to zero.
    mover.displacement = Math::Vector3{10.0F, 0.0F, 0.0F};
    const std::vector<Math::Aabb> targets{
        MakeBox(Math::Vector3{0.0F, -0.75F, 0.0F}, Math::Vector3{5.0F, 0.25F, 5.0F})};

    const ContinuousMotionResult result =
        ResolveContinuousMotion(mover, targets, MakeEnabledSettings());
    ASSERT_TRUE(result.contact.has_value());
    EXPECT_FLOAT_EQ(result.contact->timeOfImpact, 0.0F);
    // The contact is reported (so the caller can drop velocity into the floor) but the tangential
    // motion survives - freezing here would make every grounded body unable to move.
    EXPECT_FLOAT_EQ(result.displacement.x, 10.0F);
}

TEST(ContinuousCollisionTest, Resolve_ZeroDisplacementNeverReportsContact) {
    SweptSphere mover{};
    mover.radius = 0.5F;
    mover.displacement = Math::Vector3{0.0F, 0.0F, 0.0F};
    const std::vector<Math::Aabb> targets{
        MakeBox(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F})};
    const ContinuousMotionResult result =
        ResolveContinuousMotion(mover, targets, MakeEnabledSettings());
    EXPECT_FALSE(result.contact.has_value());
    EXPECT_FLOAT_EQ(result.displacement.x, 0.0F);
}

} // namespace
} // namespace UVE::Physics::Tests
