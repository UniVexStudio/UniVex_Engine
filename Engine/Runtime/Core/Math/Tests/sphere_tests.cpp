// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "sphere.h"

namespace UVE::Math::Tests {
namespace {

static_assert(Sphere{Vector3{1.0F, 2.0F, 3.0F}, 4.0F}.IsValid());
static_assert(!Sphere{Vector3{}, -1.0F}.IsValid());

TEST(SphereTest, Validity_RejectsNegativeAndNonFiniteRadius) {
    const Sphere healthy{Vector3{1.0F, 2.0F, 3.0F}, 4.0F};
    const Sphere point{Vector3{}, 0.0F}; // A point sphere is a sphere.
    const Sphere negative{Vector3{}, -1.0F};
    const Sphere nanRadius{Vector3{}, std::numeric_limits<float>::quiet_NaN()};
    const Sphere infCenter{Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, 1.0F};
    EXPECT_TRUE(healthy.IsValid());
    EXPECT_TRUE(point.IsValid());
    EXPECT_FALSE(negative.IsValid());
    EXPECT_FALSE(nanRadius.IsValid());
    EXPECT_FALSE(infCenter.IsValid());
}

TEST(SphereTest, Contains_AcceptsInteriorAndSurfaceRejectsExterior) {
    const Sphere sphere{Vector3{1.0F, 0.0F, 0.0F}, 2.0F};
    EXPECT_TRUE(sphere.Contains(Vector3{1.0F, 0.0F, 0.0F}));
    EXPECT_TRUE(sphere.Contains(Vector3{3.0F, 0.0F, 0.0F})); // On the surface.
    EXPECT_TRUE(sphere.Contains(Vector3{2.0F, 1.0F, 0.0F}));
    EXPECT_FALSE(sphere.Contains(Vector3{3.0F, 1.0F, 0.0F}));
}

TEST(SphereTest, Intersects_TouchingCountsSeparatedDoesNot) {
    const Sphere left{Vector3{0.0F, 0.0F, 0.0F}, 1.0F};
    EXPECT_TRUE(left.Intersects(Sphere{Vector3{1.0F, 0.0F, 0.0F}, 1.0F}));
    EXPECT_TRUE(left.Intersects(Sphere{Vector3{2.0F, 0.0F, 0.0F}, 1.0F})); // Touching.
    EXPECT_FALSE(left.Intersects(Sphere{Vector3{2.0F, 0.1F, 0.0F}, 1.0F}));
}

TEST(SphereTest, ToAabb_SpansCenterPlusMinusRadius) {
    const Aabb box = Sphere{Vector3{1.0F, 2.0F, 3.0F}, 4.0F}.ToAabb();
    EXPECT_EQ(box.min, (Vector3{-3.0F, -2.0F, -1.0F}));
    EXPECT_EQ(box.max, (Vector3{5.0F, 6.0F, 7.0F}));
}

} // namespace
} // namespace UVE::Math::Tests
