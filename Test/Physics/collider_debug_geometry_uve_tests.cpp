// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/collider_debug_geometry_uve.h"

namespace UVE::Physics::Tests {
namespace {

constexpr float kToleranceUVE = 1.0e-4F;

[[nodiscard]] Scene::ColliderComponentUVE MakeBoxUVE(const Math::Vector3UVE halfExtents) {
    Scene::ColliderComponentUVE collider;
    collider.shapeType = Scene::ColliderShapeTypeUVE::Box;
    collider.halfExtents = halfExtents;
    return collider;
}

[[nodiscard]] Scene::ColliderComponentUVE MakeSphereUVE(const float radius) {
    Scene::ColliderComponentUVE collider;
    collider.shapeType = Scene::ColliderShapeTypeUVE::Sphere;
    collider.radius = radius;
    return collider;
}

[[nodiscard]] Scene::ColliderComponentUVE MakeCapsuleUVE(const float radius, const float height) {
    Scene::ColliderComponentUVE collider;
    collider.shapeType = Scene::ColliderShapeTypeUVE::Capsule;
    collider.radius = radius;
    collider.height = height;
    return collider;
}

[[nodiscard]] Scene::ColliderComponentUVE MakeCylinderUVE(const float radius, const float height) {
    Scene::ColliderComponentUVE collider;
    collider.shapeType = Scene::ColliderShapeTypeUVE::Cylinder;
    collider.radius = radius;
    collider.height = height;
    return collider;
}

/// Every line the builder produced for one collider, sized from the module's own capacity call -
/// which is also what proves that call is an upper bound rather than a guess.
[[nodiscard]] std::vector<ColliderDebugLineUVE> BuildUVE(const Scene::ColliderComponentUVE& collider,
                                                         const Math::Vector3UVE& center = {},
                                                         const Math::QuaternionUVE& rotation = {},
                                                         const ColliderDebugStyleUVE& style = {}) {
    std::vector<ColliderDebugLineUVE> lines(ComputeColliderDebugLineCapacityUVE(collider, style));
    const ColliderDebugBuildResultUVE result = BuildColliderDebugLinesUVE(collider, center, rotation, lines, style);
    EXPECT_FALSE(result.truncated) << "the capacity call must be an upper bound";
    lines.resize(result.lineCount);
    return lines;
}

struct BoundsUVE final {
    Math::Vector3UVE min{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::infinity()};
    Math::Vector3UVE max{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()};

    void Add(const Math::Vector3UVE& point) noexcept {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }
};

[[nodiscard]] BoundsUVE BoundsOfUVE(const std::vector<ColliderDebugLineUVE>& lines) {
    BoundsUVE bounds;
    for (const ColliderDebugLineUVE& line : lines) {
        bounds.Add(line.from);
        bounds.Add(line.to);
    }
    return bounds;
}

// ---- Box -------------------------------------------------------------------------------------

TEST(ColliderDebugGeometryUVETest, BoxIsTwelveEdgesOfTheAuthoredExtents) {
    const std::vector<ColliderDebugLineUVE> lines = BuildUVE(MakeBoxUVE({1.0F, 2.0F, 3.0F}));
    ASSERT_EQ(lines.size(), 12U);
    const BoundsUVE bounds = BoundsOfUVE(lines);
    EXPECT_NEAR(bounds.min.x, -1.0F, kToleranceUVE);
    EXPECT_NEAR(bounds.max.x, 1.0F, kToleranceUVE);
    EXPECT_NEAR(bounds.min.y, -2.0F, kToleranceUVE);
    EXPECT_NEAR(bounds.max.y, 2.0F, kToleranceUVE);
    EXPECT_NEAR(bounds.min.z, -3.0F, kToleranceUVE);
    EXPECT_NEAR(bounds.max.z, 3.0F, kToleranceUVE);

    // Every edge spans exactly one axis before any rotation: an edge with two non-zero components
    // would be a wireframe of something that is not a box.
    for (const ColliderDebugLineUVE& line : lines) {
        const Math::Vector3UVE delta{line.to.x - line.from.x, line.to.y - line.from.y, line.to.z - line.from.z};
        const int nonZero = (std::fabs(delta.x) > kToleranceUVE ? 1 : 0) +
                            (std::fabs(delta.y) > kToleranceUVE ? 1 : 0) +
                            (std::fabs(delta.z) > kToleranceUVE ? 1 : 0);
        EXPECT_EQ(nonZero, 1);
    }
}

TEST(ColliderDebugGeometryUVETest, BoxFollowsTheWorldTransform) {
    Math::QuaternionUVE rotation;
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({0.0F, 1.0F, 0.0F}, 1.57079633F, rotation));
    const std::vector<ColliderDebugLineUVE> lines =
        BuildUVE(MakeBoxUVE({1.0F, 1.0F, 2.0F}), Math::Vector3UVE{5.0F, 1.0F, 0.0F}, rotation);
    ASSERT_EQ(lines.size(), 12U);

    // A quarter turn about +Y sends the local +X axis onto world -Z, so the 2 m half extent that
    // was local Z now spans world X and the 1 m one spans world Z. That swap is the whole point:
    // the wireframe has to be the rotated shape, not its unrotated bounds.
    const BoundsUVE bounds = BoundsOfUVE(lines);
    EXPECT_NEAR(bounds.min.x, 3.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.x, 7.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.min.y, 0.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.y, 2.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.min.z, -1.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.z, 1.0F, 1.0e-3F);
}

TEST(ColliderDebugGeometryUVETest, BoxRefusesDegenerateInput) {
    const Scene::ColliderComponentUVE flat = MakeBoxUVE({1.0F, 0.0F, 1.0F});
    EXPECT_FALSE(Scene::IsColliderComponentValidUVE(flat));
    EXPECT_EQ(BuildUVE(flat).size(), 0U) << "a zero-thickness box is not a shape the solver has";

    EXPECT_EQ(BuildColliderDebugLinesUVE(MakeBoxUVE({1.0F, 1.0F, 1.0F}), {}, {}, {}).lineCount, 0U)
        << "an empty buffer is a refusal, not a crash";

    // A non-finite centre produces nothing rather than twelve NaN segments.
    std::array<ColliderDebugLineUVE, 16U> storage{};
    const ColliderDebugBuildResultUVE result = BuildColliderDebugLinesUVE(
        MakeBoxUVE({1.0F, 1.0F, 1.0F}), {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, {}, storage);
    EXPECT_EQ(result.lineCount, 0U);
    EXPECT_FALSE(result.truncated) << "a refusal is not a truncation";
}

// ---- Sphere -----------------------------------------------------------------------------------

TEST(ColliderDebugGeometryUVETest, SphereIsThreeCirclesOnTheRadius) {
    const std::vector<ColliderDebugLineUVE> lines = BuildUVE(MakeSphereUVE(2.0F));
    ASSERT_EQ(lines.size(), 72U) << "3 circles x 24 segments";

    for (const ColliderDebugLineUVE& line : lines) {
        EXPECT_NEAR(std::sqrt(Math::LengthSquaredUVE(line.from)), 2.0F, 1.0e-3F);
        EXPECT_NEAR(std::sqrt(Math::LengthSquaredUVE(line.to)), 2.0F, 1.0e-3F);
    }
    const BoundsUVE bounds = BoundsOfUVE(lines);
    EXPECT_NEAR(bounds.min.x, -2.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.y, 2.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.z, 2.0F, 1.0e-3F);
}

TEST(ColliderDebugGeometryUVETest, SegmentCountIsHonouredAndClamped) {
    ColliderDebugStyleUVE style;
    style.circleSegments = 8U;
    EXPECT_EQ(BuildUVE(MakeSphereUVE(1.0F), {}, {}, style).size(), 24U);

    style.circleSegments = 2U; // below the floor, clamped up to 8
    EXPECT_EQ(ResolveColliderDebugSegmentsUVE(style), 8U);
    style.circleSegments = 1000U; // above the ceiling, clamped down to 96
    EXPECT_EQ(ResolveColliderDebugSegmentsUVE(style), 96U);
    style.circleSegments = 25U; // odd counts round UP, so a capsule's fans land on ring vertices
    EXPECT_EQ(ResolveColliderDebugSegmentsUVE(style), 26U);
}

// ---- Capsule ----------------------------------------------------------------------------------

TEST(ColliderDebugGeometryUVETest, CapsuleStandsAlongLocalYAndSpansRadiusPlusBarrel) {
    const std::vector<ColliderDebugLineUVE> lines =
        BuildUVE(MakeCapsuleUVE(/*radius=*/0.5F, /*height=*/3.0F), Math::Vector3UVE{0.0F, 10.0F, 0.0F});
    ASSERT_EQ(lines.size(), 4U * 24U + 4U);

    const BoundsUVE bounds = BoundsOfUVE(lines);
    EXPECT_NEAR(bounds.min.y, 8.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.y, 11.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.min.x, -0.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.z, 0.5F, 1.0e-3F);
}

TEST(ColliderDebugGeometryUVETest, CapsuleDrawsASphereWhenThereIsNoBarrel) {
    // height == diameter: no barrel at all, so the shape IS a sphere at the centre.
    const std::vector<ColliderDebugLineUVE> lines = BuildUVE(MakeCapsuleUVE(1.0F, 2.0F));
    ASSERT_EQ(lines.size(), 4U * 24U + 4U);
    const BoundsUVE bounds = BoundsOfUVE(lines);
    EXPECT_NEAR(bounds.min.y, -1.0F, 1.0e-3F) << "the barrel must collapse, not invert";
    EXPECT_NEAR(bounds.max.y, 1.0F, 1.0e-3F);
}

TEST(ColliderDebugGeometryUVETest, CapsuleTurnsWithItsRotation) {
    Math::QuaternionUVE rotation;
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({1.0F, 0.0F, 0.0F}, 1.57079633F, rotation));
    // A quarter turn about X lays the local +Y barrel along world -Z.
    const std::vector<ColliderDebugLineUVE> lines = BuildUVE(MakeCapsuleUVE(0.5F, 3.0F), {}, rotation);
    const BoundsUVE bounds = BoundsOfUVE(lines);
    EXPECT_NEAR(bounds.max.z, 1.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.min.z, -1.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.y, 0.5F, 1.0e-3F);
}

// ---- Cylinder and the bounds view -------------------------------------------------------------

TEST(ColliderDebugGeometryUVETest, CylinderIsTwoRingsAndFourBarrels) {
    const std::vector<ColliderDebugLineUVE> lines = BuildUVE(MakeCylinderUVE(0.25F, 2.0F));
    ASSERT_EQ(lines.size(), 2U * 24U + 4U);
    const BoundsUVE bounds = BoundsOfUVE(lines);
    EXPECT_NEAR(bounds.max.y, 1.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.min.y, -1.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.x, 0.25F, 1.0e-3F);
}

TEST(ColliderDebugGeometryUVETest, BoundsOnlyDrawsTheBoxTheBroadphaseSees) {
    ColliderDebugStyleUVE style;
    style.drawBoundsOnly = true;
    // A capsule's AABB is its radius in X/Z and its half height in Y - the shape the collision cache
    // hands the broadphase, and the reason a capsule's corners can register a hit.
    const std::vector<ColliderDebugLineUVE> lines = BuildUVE(MakeCapsuleUVE(0.5F, 3.0F), {}, {}, style);
    ASSERT_EQ(lines.size(), 12U);
    const BoundsUVE bounds = BoundsOfUVE(lines);
    EXPECT_NEAR(bounds.max.y, 1.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.x, 0.5F, 1.0e-3F);
}

// ---- The module's own bounds ------------------------------------------------------------------

TEST(ColliderDebugGeometryUVETest, CapacityIsExactForEveryShape) {
    const std::array<Scene::ColliderComponentUVE, 4U> shapes{
        MakeBoxUVE({1.0F, 1.0F, 1.0F}), MakeSphereUVE(1.0F), MakeCapsuleUVE(0.5F, 2.0F),
        MakeCylinderUVE(0.5F, 2.0F)};
    for (const Scene::ColliderComponentUVE& shape : shapes) {
        std::vector<ColliderDebugLineUVE> storage(ComputeColliderDebugLineCapacityUVE(shape));
        const ColliderDebugBuildResultUVE result = BuildColliderDebugLinesUVE(shape, {}, {}, storage);
        EXPECT_EQ(result.lineCount, storage.size()) << "capacity must be exact, not merely large";
        EXPECT_FALSE(result.truncated);
    }
}

TEST(ColliderDebugGeometryUVETest, ShortBufferTruncatesCleanly) {
    std::array<ColliderDebugLineUVE, 5U> storage{};
    const ColliderDebugBuildResultUVE result = BuildColliderDebugLinesUVE(MakeSphereUVE(1.0F), {}, {}, storage);
    EXPECT_EQ(result.lineCount, 5U);
    EXPECT_TRUE(result.truncated);
    for (const ColliderDebugLineUVE& line : storage) {
        EXPECT_TRUE(Math::IsFiniteUVE(line.from));
        EXPECT_TRUE(Math::IsFiniteUVE(line.to));
    }
}

} // namespace
} // namespace UVE::Physics::Tests
