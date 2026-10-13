// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "uve/math/oriented_box_uve.h"

#include "uve/math/scalar_uve.h"

namespace UVE::Math::Tests {
namespace {

static_assert(
    OrientedBoxUVE{Vector3UVE{}, Vector3UVE{1.0F, 2.0F, 3.0F}, QuaternionUVE{}}.IsValidUVE());

TEST(OrientedBoxUVETest, Validity_RejectsNegativeExtentsAndNonFiniteFields) {
    const OrientedBoxUVE box{Vector3UVE{}, Vector3UVE{1.0F, 2.0F, 3.0F}, QuaternionUVE{}};
    EXPECT_TRUE(box.IsValidUVE());
    const OrientedBoxUVE negative{Vector3UVE{}, Vector3UVE{-1.0F, 2.0F, 3.0F}, QuaternionUVE{}};
    const OrientedBoxUVE nanCenter{Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                                   Vector3UVE{1.0F, 1.0F, 1.0F}, QuaternionUVE{}};
    EXPECT_FALSE(negative.IsValidUVE());
    EXPECT_FALSE(nanCenter.IsValidUVE());
}

TEST(OrientedBoxUVETest, Conjugate_IsItsOwnInverse) {
    const QuaternionUVE rotation{0.0F, 0.7071068F, 0.0F, 0.7071068F};
    EXPECT_EQ(ConjugateUVE(ConjugateUVE(rotation)), rotation);
    EXPECT_EQ(ConjugateUVE(rotation), (QuaternionUVE{0.0F, -0.7071068F, 0.0F, 0.7071068F}));
}

TEST(OrientedBoxUVETest, ToLocal_RoundTripsThroughTheRotation) {
    QuaternionUVE rotation{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(Vector3UVE{0.0F, 1.0F, 0.0F}, 1.0F, rotation));
    const OrientedBoxUVE box{Vector3UVE{10.0F, 0.0F, 0.0F}, Vector3UVE{1.0F, 1.0F, 1.0F}, rotation};
    const Vector3UVE world{11.0F, 2.0F, 3.0F};
    const Vector3UVE back =
        Vector3UVE{box.center.x + RotateVectorUVE(rotation, box.ToLocalUVE(world)).x,
                   box.center.y + RotateVectorUVE(rotation, box.ToLocalUVE(world)).y,
                   box.center.z + RotateVectorUVE(rotation, box.ToLocalUVE(world)).z};
    EXPECT_NEAR(back.x, world.x, 1e-5F);
    EXPECT_NEAR(back.y, world.y, 1e-5F);
    EXPECT_NEAR(back.z, world.z, 1e-5F);
}

TEST(OrientedBoxUVETest, Contains_RespectsTheRotation) {
    QuaternionUVE quarterTurn{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(Vector3UVE{0.0F, 0.0F, 1.0F}, kPiUVE * 0.5F, quarterTurn));
    // A 2-wide, 4-tall box lying down: world-x spans ±2, world-y spans ±1.
    const OrientedBoxUVE box{Vector3UVE{}, Vector3UVE{1.0F, 2.0F, 1.0F}, quarterTurn};
    EXPECT_TRUE(box.ContainsUVE(Vector3UVE{1.9F, 0.0F, 0.0F}));
    EXPECT_FALSE(box.ContainsUVE(Vector3UVE{0.0F, 1.9F, 0.0F}));
}

TEST(OrientedBoxUVETest, ToAabb_IdentityRotationMatchesCenterPlusMinusExtents) {
    const OrientedBoxUVE box{Vector3UVE{1.0F, 2.0F, 3.0F}, Vector3UVE{1.0F, 1.0F, 1.0F},
                             QuaternionUVE{}};
    const AabbUVE bounds = box.ToAabbUVE();
    EXPECT_EQ(bounds.min, (Vector3UVE{0.0F, 1.0F, 2.0F}));
    EXPECT_EQ(bounds.max, (Vector3UVE{2.0F, 3.0F, 4.0F}));
}

TEST(OrientedBoxUVETest, ToAabb_QuarterTurnSwapsTheFootprint) {
    QuaternionUVE quarterTurn{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(Vector3UVE{0.0F, 0.0F, 1.0F}, kPiUVE * 0.5F, quarterTurn));
    const OrientedBoxUVE box{Vector3UVE{}, Vector3UVE{1.0F, 2.0F, 0.5F}, quarterTurn};
    const AabbUVE bounds = box.ToAabbUVE();
    EXPECT_NEAR(bounds.min.x, -2.0F, 1e-5F);
    EXPECT_NEAR(bounds.max.x, 2.0F, 1e-5F);
    EXPECT_NEAR(bounds.min.y, -1.0F, 1e-5F);
    EXPECT_NEAR(bounds.max.y, 1.0F, 1e-5F);
}

} // namespace
} // namespace UVE::Math::Tests
