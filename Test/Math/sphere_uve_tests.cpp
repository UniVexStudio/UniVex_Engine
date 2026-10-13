// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "uve/math/sphere_uve.h"

namespace UVE::Math::Tests {
namespace {

static_assert(SphereUVE{Vector3UVE{1.0F, 2.0F, 3.0F}, 4.0F}.IsValidUVE());
static_assert(!SphereUVE{Vector3UVE{}, -1.0F}.IsValidUVE());

TEST(SphereUVETest, Validity_RejectsNegativeAndNonFiniteRadius) {
    const SphereUVE healthy{Vector3UVE{1.0F, 2.0F, 3.0F}, 4.0F};
    const SphereUVE point{Vector3UVE{}, 0.0F}; // A point sphere is a sphere.
    const SphereUVE negative{Vector3UVE{}, -1.0F};
    const SphereUVE nanRadius{Vector3UVE{}, std::numeric_limits<float>::quiet_NaN()};
    const SphereUVE infCenter{Vector3UVE{std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, 1.0F};
    EXPECT_TRUE(healthy.IsValidUVE());
    EXPECT_TRUE(point.IsValidUVE());
    EXPECT_FALSE(negative.IsValidUVE());
    EXPECT_FALSE(nanRadius.IsValidUVE());
    EXPECT_FALSE(infCenter.IsValidUVE());
}

TEST(SphereUVETest, Contains_AcceptsInteriorAndSurfaceRejectsExterior) {
    const SphereUVE sphere{Vector3UVE{1.0F, 0.0F, 0.0F}, 2.0F};
    EXPECT_TRUE(sphere.ContainsUVE(Vector3UVE{1.0F, 0.0F, 0.0F}));
    EXPECT_TRUE(sphere.ContainsUVE(Vector3UVE{3.0F, 0.0F, 0.0F})); // On the surface.
    EXPECT_TRUE(sphere.ContainsUVE(Vector3UVE{2.0F, 1.0F, 0.0F}));
    EXPECT_FALSE(sphere.ContainsUVE(Vector3UVE{3.0F, 1.0F, 0.0F}));
}

TEST(SphereUVETest, Intersects_TouchingCountsSeparatedDoesNot) {
    const SphereUVE left{Vector3UVE{0.0F, 0.0F, 0.0F}, 1.0F};
    EXPECT_TRUE(left.IntersectsUVE(SphereUVE{Vector3UVE{1.0F, 0.0F, 0.0F}, 1.0F}));
    EXPECT_TRUE(left.IntersectsUVE(SphereUVE{Vector3UVE{2.0F, 0.0F, 0.0F}, 1.0F})); // Touching.
    EXPECT_FALSE(left.IntersectsUVE(SphereUVE{Vector3UVE{2.0F, 0.1F, 0.0F}, 1.0F}));
}

TEST(SphereUVETest, ToAabb_SpansCenterPlusMinusRadius) {
    const AabbUVE box = SphereUVE{Vector3UVE{1.0F, 2.0F, 3.0F}, 4.0F}.ToAabbUVE();
    EXPECT_EQ(box.min, (Vector3UVE{-3.0F, -2.0F, -1.0F}));
    EXPECT_EQ(box.max, (Vector3UVE{5.0F, 6.0F, 7.0F}));
}

} // namespace
} // namespace UVE::Math::Tests
