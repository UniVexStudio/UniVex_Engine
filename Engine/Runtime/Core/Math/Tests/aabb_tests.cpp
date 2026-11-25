// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "aabb.h"

#include <cmath>
#include <optional>
#include <string>
#include <limits>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

TEST(AabbTest, FromCenterExtents_ComputesMinAndMax) {
    const Aabb box = Aabb::FromCenterExtents(Vector3{1.0F, 2.0F, 3.0F}, Vector3{1.0F, 2.0F, 3.0F});

    EXPECT_EQ(box.min, (Vector3{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(box.max, (Vector3{2.0F, 4.0F, 6.0F}));
}

TEST(AabbTest, GetCenter_And_GetExtents_RoundTripFromCenterExtents) {
    const Vector3 center{1.0F, 2.0F, 3.0F};
    const Vector3 extents{4.0F, 5.0F, 6.0F};
    const Aabb box = Aabb::FromCenterExtents(center, extents);

    EXPECT_EQ(box.GetCenter(), center);
    EXPECT_EQ(box.GetExtents(), extents);
}

TEST(AabbTest, GetCenter_PreservesFiniteExtremeOrderedBounds) {
    const float maximum = std::numeric_limits<float>::max();
    const Aabb box{Vector3{maximum * 0.5F, maximum * 0.5F, maximum * 0.5F},
                      Vector3{maximum, maximum, maximum}};

    const Vector3 center = box.GetCenter();

    EXPECT_TRUE(std::isfinite(center.x));
    EXPECT_TRUE(std::isfinite(center.y));
    EXPECT_TRUE(std::isfinite(center.z));
    EXPECT_FLOAT_EQ(center.x, maximum * 0.75F);
    EXPECT_FLOAT_EQ(center.y, maximum * 0.75F);
    EXPECT_FLOAT_EQ(center.z, maximum * 0.75F);
}

TEST(AabbTest, Union_CombinesTwoBoxesIntoTheirEnclosingBox) {
    const Aabb a{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Aabb b{Vector3{0.0F, 0.0F, 0.0F}, Vector3{5.0F, 5.0F, 5.0F}};

    const Aabb combined = a.Union(b);

    EXPECT_EQ(combined.min, (Vector3{-1.0F, -1.0F, -1.0F}));
    EXPECT_EQ(combined.max, (Vector3{5.0F, 5.0F, 5.0F}));
}

TEST(AabbTest, Transform_Translation_ShiftsBoxWithoutChangingSize) {
    const Aabb box{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Matrix4x4 translation =
        Matrix4x4::ComposeTrs(Vector3{10.0F, 0.0F, 0.0F}, Quaternion{}, Vector3{1.0F, 1.0F, 1.0F});

    const Aabb transformed = box.Transform(translation);

    EXPECT_NEAR(transformed.min.x, 9.0F, kEpsilon);
    EXPECT_NEAR(transformed.max.x, 11.0F, kEpsilon);
    EXPECT_NEAR(transformed.min.y, -1.0F, kEpsilon);
    EXPECT_NEAR(transformed.max.y, 1.0F, kEpsilon);
}

TEST(AabbTest, Transform_Scale_ScalesBoxAboutOrigin) {
    const Aabb box{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Matrix4x4 scale =
        Matrix4x4::ComposeTrs(Vector3{}, Quaternion{}, Vector3{2.0F, 2.0F, 2.0F});

    const Aabb transformed = box.Transform(scale);

    EXPECT_NEAR(transformed.min.x, -2.0F, kEpsilon);
    EXPECT_NEAR(transformed.max.x, 2.0F, kEpsilon);
}

TEST(AabbTest, Transform_NinetyDegreesAboutZ_SwapsXAndYExtents) {
    const Aabb box{Vector3{-1.0F, -2.0F, -3.0F}, Vector3{1.0F, 2.0F, 3.0F}};
    const float halfNinety = std::sqrt(2.0F) / 2.0F;
    const Quaternion ninetyAboutZ{0.0F, 0.0F, halfNinety, halfNinety};
    const Matrix4x4 rotation =
        Matrix4x4::ComposeTrs(Vector3{}, ninetyAboutZ, Vector3{1.0F, 1.0F, 1.0F});

    const Aabb transformed = box.Transform(rotation);

    EXPECT_NEAR(transformed.min.x, -2.0F, kEpsilon);
    EXPECT_NEAR(transformed.max.x, 2.0F, kEpsilon);
    EXPECT_NEAR(transformed.min.y, -1.0F, kEpsilon);
    EXPECT_NEAR(transformed.max.y, 1.0F, kEpsilon);
    EXPECT_NEAR(transformed.min.z, -3.0F, kEpsilon);
    EXPECT_NEAR(transformed.max.z, 3.0F, kEpsilon);
}

TEST(AabbTest, EqualityOperators_CompareMinAndMax) {
    const Aabb a{Vector3{0.0F, 0.0F, 0.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Aabb b{Vector3{0.0F, 0.0F, 0.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Aabb c{Vector3{0.0F, 0.0F, 0.0F}, Vector3{2.0F, 1.0F, 1.0F}};

    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(AabbTest, Intersects_OverlappingBoxes_ReturnsTrue) {
    const Aabb a{Vector3{0.0F, 0.0F, 0.0F}, Vector3{2.0F, 2.0F, 2.0F}};
    const Aabb b{Vector3{1.0F, 1.0F, 1.0F}, Vector3{3.0F, 3.0F, 3.0F}};

    EXPECT_TRUE(a.Intersects(b));
    EXPECT_TRUE(b.Intersects(a));
}

TEST(AabbTest, Intersects_DisjointBoxes_ReturnsFalse) {
    const Aabb a{Vector3{0.0F, 0.0F, 0.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Aabb b{Vector3{10.0F, 10.0F, 10.0F}, Vector3{11.0F, 11.0F, 11.0F}};

    EXPECT_FALSE(a.Intersects(b));
    EXPECT_FALSE(b.Intersects(a));
}

TEST(AabbTest, Intersects_TouchingEdges_ReturnsFalse) {
    const Aabb a{Vector3{0.0F, 0.0F, 0.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Aabb b{Vector3{1.0F, 0.0F, 0.0F}, Vector3{2.0F, 1.0F, 1.0F}};

    EXPECT_FALSE(a.Intersects(b));
}

TEST(AabbTest, ComputePenetration_DisjointBoxes_ReturnsNullopt) {
    const Aabb a{Vector3{0.0F, 0.0F, 0.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Aabb b{Vector3{10.0F, 10.0F, 10.0F}, Vector3{11.0F, 11.0F, 11.0F}};

    EXPECT_FALSE(ComputePenetration(a, b).has_value());
}

TEST(AabbTest, ComputePenetration_OverlappingAlongX_ReturnsXAxisAndCorrectDepth) {
    // a spans x[0,2], b spans x[1,3] (both y/z identical and much larger, so x is the
    // shallowest, correct MTV axis) — overlap along x is [1,2], depth 1.
    const Aabb a{Vector3{0.0F, 0.0F, 0.0F}, Vector3{2.0F, 10.0F, 10.0F}};
    const Aabb b{Vector3{1.0F, 0.0F, 0.0F}, Vector3{3.0F, 10.0F, 10.0F}};

    const std::optional<Penetration> penetration = ComputePenetration(a, b);
    ASSERT_TRUE(penetration.has_value());
    EXPECT_NEAR(penetration->depth, 1.0F, kEpsilon);
    EXPECT_NEAR(penetration->axis.x, 1.0F, kEpsilon); // points from a's center toward b's center
    EXPECT_NEAR(penetration->axis.y, 0.0F, kEpsilon);
    EXPECT_NEAR(penetration->axis.z, 0.0F, kEpsilon);
}

TEST(AabbTest, ComputePenetration_RejectsOverflowedFiniteDepth) {
    const float maximumFloat = std::numeric_limits<float>::max();
    const Aabb a{Vector3{-maximumFloat, -1.0F, -1.0F}, Vector3{maximumFloat, 1.0F, 1.0F}};
    const Aabb b{Vector3{-maximumFloat, -1.0F, -1.0F}, Vector3{maximumFloat, 1.0F, 1.0F}};

    EXPECT_FALSE(ComputePenetration(a, b).has_value());
}

TEST(AabbTest, ComputePenetration_RejectsNonFiniteOrInvertedInputs) {
    const Aabb validBox{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const float infinity = std::numeric_limits<float>::infinity();

    EXPECT_FALSE(ComputePenetration(
        Aabb{Vector3{infinity, -1.0F, -1.0F}, validBox.max}, validBox).has_value());
    EXPECT_FALSE(ComputePenetration(
        Aabb{Vector3{1.0F, -1.0F, -1.0F}, Vector3{-1.0F, 1.0F, 1.0F}}, validBox).has_value());
}

TEST(AabbTest, ComputePenetration_AxisPointsFromFirstTowardSecond) {
    const Aabb a{Vector3{1.0F, 0.0F, 0.0F}, Vector3{3.0F, 10.0F, 10.0F}};
    const Aabb b{Vector3{0.0F, 0.0F, 0.0F}, Vector3{2.0F, 10.0F, 10.0F}};

    const std::optional<Penetration> penetration = ComputePenetration(a, b);
    ASSERT_TRUE(penetration.has_value());
    // b's center.x < a's center.x, so the axis (from a toward b) points in -x.
    EXPECT_NEAR(penetration->axis.x, -1.0F, kEpsilon);
}

TEST(AabbTest, IntersectRay_DirectHitFromOutside_ReturnsDistanceAndNormal) {
    const Aabb box{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Ray ray{Vector3{-5.0F, 0.0F, 0.0F}, Vector3{1.0F, 0.0F, 0.0F}};

    const std::optional<RayHit> hit = IntersectRay(ray, box, 100.0F);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 4.0F, kEpsilon);
    EXPECT_NEAR(hit->normal.x, -1.0F, kEpsilon);
    EXPECT_NEAR(hit->normal.y, 0.0F, kEpsilon);
    EXPECT_NEAR(hit->normal.z, 0.0F, kEpsilon);
}

TEST(AabbTest, IntersectRay_HitFromBelow_ReturnsMinusYNormal) {
    const Aabb box{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Ray ray{Vector3{0.0F, -5.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F}};

    const std::optional<RayHit> hit = IntersectRay(ray, box, 100.0F);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 4.0F, kEpsilon);
    EXPECT_NEAR(hit->normal.y, -1.0F, kEpsilon);
}

TEST(AabbTest, IntersectRay_PointingAwayFromBox_ReturnsNullopt) {
    const Aabb box{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Ray ray{Vector3{-5.0F, 0.0F, 0.0F}, Vector3{-1.0F, 0.0F, 0.0F}};

    EXPECT_FALSE(IntersectRay(ray, box, 100.0F).has_value());
}

TEST(AabbTest, IntersectRay_OriginInsideBox_ReturnsZeroDistance) {
    const Aabb box{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Ray ray{Vector3{0.0F, 0.0F, 0.0F}, Vector3{1.0F, 0.0F, 0.0F}};

    const std::optional<RayHit> hit = IntersectRay(ray, box, 100.0F);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 0.0F, kEpsilon);
}

TEST(AabbTest, IntersectRay_ParallelToTwoSlabsWithinRange_StillHitsWithoutNaN) {
    // direction.x == 0 and direction.y == 0 — exercises the dir-is-zero branch for two axes.
    const Aabb box{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Ray ray{Vector3{0.0F, 0.0F, -10.0F}, Vector3{0.0F, 0.0F, 1.0F}};

    const std::optional<RayHit> hit = IntersectRay(ray, box, 100.0F);

    ASSERT_TRUE(hit.has_value());
    EXPECT_TRUE(std::isfinite(hit->distance));
    EXPECT_NEAR(hit->distance, 9.0F, kEpsilon);
    EXPECT_NEAR(hit->normal.z, -1.0F, kEpsilon);
}

TEST(AabbTest, IntersectRay_ParallelToSlabOutsideRange_ReturnsNulloptWithoutNaN) {
    // direction.x == 0, but origin.x is outside the box's x-slab entirely — must miss cleanly.
    const Aabb box{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Ray ray{Vector3{5.0F, 0.0F, -10.0F}, Vector3{0.0F, 0.0F, 1.0F}};

    EXPECT_FALSE(IntersectRay(ray, box, 100.0F).has_value());
}

TEST(AabbTest, IntersectRay_RejectsNonFiniteOrInvertedInputs) {
    const Aabb validBox{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Ray validRay{Vector3{-5.0F, 0.0F, 0.0F}, Vector3{1.0F, 0.0F, 0.0F}};
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    EXPECT_FALSE(IntersectRay(Ray{Vector3{nan, 0.0F, 0.0F}, validRay.direction}, validBox, 100.0F));
    EXPECT_FALSE(IntersectRay(validRay, validBox, infinity));
    EXPECT_FALSE(IntersectRay(validRay,
                                 Aabb{Vector3{1.0F, -1.0F, -1.0F}, Vector3{-1.0F, 1.0F, 1.0F}},
                                 100.0F));
}

TEST(AabbTest, IntersectRay_RejectsFiniteSlabSubtractionOverflow) {
    const float maximumFloat = std::numeric_limits<float>::max();
    const Aabb box{Vector3{maximumFloat, -1.0F, -1.0F}, Vector3{maximumFloat, 1.0F, 1.0F}};
    const Ray ray{Vector3{-maximumFloat, 0.0F, 0.0F}, Vector3{1.0F, 0.0F, 0.0F}};

    EXPECT_FALSE(IntersectRay(ray, box, maximumFloat));
}

TEST(AabbTest, IntersectRay_HitBeyondMaxDistance_ReturnsNullopt) {
    const Aabb box{Vector3{-1.0F, -1.0F, -1.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Ray ray{Vector3{-5.0F, 0.0F, 0.0F}, Vector3{1.0F, 0.0F, 0.0F}};

    EXPECT_FALSE(IntersectRay(ray, box, 2.0F).has_value()); // box starts at distance 4
}

TEST(AabbTest, SweepAabb_ReportsFirstImpactTimeAndMoverToTargetNormal) {
    const Aabb moving{Vector3{-0.5F, -0.5F, -0.5F}, Vector3{0.5F, 0.5F, 0.5F}};
    const Aabb target{Vector3{2.0F, -1.0F, -1.0F}, Vector3{3.0F, 1.0F, 1.0F}};

    const std::optional<SweptAabbHit> hit = SweepAabb(moving, Vector3{4.0F, 0.0F, 0.0F}, target);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->time, 0.375F, kEpsilon);
    EXPECT_EQ(hit->normal, (Vector3{1.0F, 0.0F, 0.0F}));
}

TEST(AabbTest, SweepAabb_MissesWhenMovingAwayOrAlreadyOverlapping) {
    const Aabb moving{Vector3{-0.5F, -0.5F, -0.5F}, Vector3{0.5F, 0.5F, 0.5F}};
    const Aabb target{Vector3{2.0F, -1.0F, -1.0F}, Vector3{3.0F, 1.0F, 1.0F}};
    EXPECT_FALSE(SweepAabb(moving, Vector3{-4.0F, 0.0F, 0.0F}, target).has_value());

    const Aabb overlapping{Vector3{1.75F, -0.5F, -0.5F}, Vector3{2.25F, 0.5F, 0.5F}};
    EXPECT_FALSE(SweepAabb(overlapping, Vector3{4.0F, 0.0F, 0.0F}, target).has_value());
}

TEST(AabbTest, SweepAabb_PreservesTangentialMotionAndRejectsParallelMiss) {
    const Aabb moving{Vector3{-0.5F, -0.5F, -0.5F}, Vector3{0.5F, 0.5F, 0.5F}};
    const Aabb target{Vector3{2.0F, -1.0F, -1.0F}, Vector3{3.0F, 1.0F, 1.0F}};
    const std::optional<SweptAabbHit> hit = SweepAabb(moving, Vector3{4.0F, 2.0F, 0.0F}, target);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->time, 0.375F, kEpsilon);
    EXPECT_EQ(hit->normal, (Vector3{1.0F, 0.0F, 0.0F}));

    const Aabb farTarget{Vector3{2.0F, 2.0F, -1.0F}, Vector3{3.0F, 3.0F, 1.0F}};
    EXPECT_FALSE(SweepAabb(moving, Vector3{4.0F, 0.0F, 0.0F}, farTarget).has_value());
}

TEST(AabbTest, SweepAabb_TouchingFaceMovingIntoTargetReportsZeroTimeNormal) {
    const Aabb moving{Vector3{0.0F, -0.5F, -0.5F}, Vector3{1.0F, 0.5F, 0.5F}};
    const Aabb target{Vector3{1.0F, -1.0F, -1.0F}, Vector3{2.0F, 1.0F, 1.0F}};

    const std::optional<SweptAabbHit> hit = SweepAabb(moving, Vector3{2.0F, 0.0F, 0.0F}, target);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->time, 0.0F, kEpsilon);
    EXPECT_EQ(hit->normal, (Vector3{1.0F, 0.0F, 0.0F}));
}

TEST(AabbTest, ToString_ContainsMinAndMax) {
    const Aabb box{Vector3{0.0F, 0.0F, 0.0F}, Vector3{1.0F, 2.0F, 3.0F}};
    const std::string text = ToString(box);

    EXPECT_NE(text.find("3.000000"), std::string::npos);
}

} // namespace
} // namespace UVE::Math::Tests
