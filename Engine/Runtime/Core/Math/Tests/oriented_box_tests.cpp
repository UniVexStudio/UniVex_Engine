// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "oriented_box.h"

#include "scalar.h"

namespace UVE::Math::Tests {
namespace {

static_assert(
    OrientedBox{Vector3{}, Vector3{1.0F, 2.0F, 3.0F}, Quaternion{}}.IsValid());

TEST(OrientedBoxTest, Validity_RejectsNegativeExtentsAndNonFiniteFields) {
    const OrientedBox box{Vector3{}, Vector3{1.0F, 2.0F, 3.0F}, Quaternion{}};
    EXPECT_TRUE(box.IsValid());
    const OrientedBox negative{Vector3{}, Vector3{-1.0F, 2.0F, 3.0F}, Quaternion{}};
    const OrientedBox nanCenter{Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                                   Vector3{1.0F, 1.0F, 1.0F}, Quaternion{}};
    EXPECT_FALSE(negative.IsValid());
    EXPECT_FALSE(nanCenter.IsValid());
}

TEST(OrientedBoxTest, Conjugate_IsItsOwnInverse) {
    const Quaternion rotation{0.0F, 0.7071068F, 0.0F, 0.7071068F};
    EXPECT_EQ(Conjugate(Conjugate(rotation)), rotation);
    EXPECT_EQ(Conjugate(rotation), (Quaternion{0.0F, -0.7071068F, 0.0F, 0.7071068F}));
}

TEST(OrientedBoxTest, ToLocal_RoundTripsThroughTheRotation) {
    Quaternion rotation{};
    ASSERT_TRUE(TryMakeAxisAngle(Vector3{0.0F, 1.0F, 0.0F}, 1.0F, rotation));
    const OrientedBox box{Vector3{10.0F, 0.0F, 0.0F}, Vector3{1.0F, 1.0F, 1.0F}, rotation};
    const Vector3 world{11.0F, 2.0F, 3.0F};
    const Vector3 back =
        Vector3{box.center.x + RotateVector(rotation, box.ToLocal(world)).x,
                   box.center.y + RotateVector(rotation, box.ToLocal(world)).y,
                   box.center.z + RotateVector(rotation, box.ToLocal(world)).z};
    EXPECT_NEAR(back.x, world.x, 1e-5F);
    EXPECT_NEAR(back.y, world.y, 1e-5F);
    EXPECT_NEAR(back.z, world.z, 1e-5F);
}

TEST(OrientedBoxTest, Contains_RespectsTheRotation) {
    Quaternion quarterTurn{};
    ASSERT_TRUE(TryMakeAxisAngle(Vector3{0.0F, 0.0F, 1.0F}, kPi * 0.5F, quarterTurn));
    // A 2-wide, 4-tall box lying down: world-x spans ±2, world-y spans ±1.
    const OrientedBox box{Vector3{}, Vector3{1.0F, 2.0F, 1.0F}, quarterTurn};
    EXPECT_TRUE(box.Contains(Vector3{1.9F, 0.0F, 0.0F}));
    EXPECT_FALSE(box.Contains(Vector3{0.0F, 1.9F, 0.0F}));
}

TEST(OrientedBoxTest, ToAabb_IdentityRotationMatchesCenterPlusMinusExtents) {
    const OrientedBox box{Vector3{1.0F, 2.0F, 3.0F}, Vector3{1.0F, 1.0F, 1.0F},
                             Quaternion{}};
    const Aabb bounds = box.ToAabb();
    EXPECT_EQ(bounds.min, (Vector3{0.0F, 1.0F, 2.0F}));
    EXPECT_EQ(bounds.max, (Vector3{2.0F, 3.0F, 4.0F}));
}

TEST(OrientedBoxTest, ToAabb_QuarterTurnSwapsTheFootprint) {
    Quaternion quarterTurn{};
    ASSERT_TRUE(TryMakeAxisAngle(Vector3{0.0F, 0.0F, 1.0F}, kPi * 0.5F, quarterTurn));
    const OrientedBox box{Vector3{}, Vector3{1.0F, 2.0F, 0.5F}, quarterTurn};
    const Aabb bounds = box.ToAabb();
    EXPECT_NEAR(bounds.min.x, -2.0F, 1e-5F);
    EXPECT_NEAR(bounds.max.x, 2.0F, 1e-5F);
    EXPECT_NEAR(bounds.min.y, -1.0F, 1e-5F);
    EXPECT_NEAR(bounds.max.y, 1.0F, 1e-5F);
}

} // namespace
} // namespace UVE::Math::Tests
