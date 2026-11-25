// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "collider_component.h"
#include "quaternion.h"
#include "vector3.h"
#include "collider_debug_geometry.h"

namespace UVE::Physics::Tests {
namespace {

constexpr float kTolerance = 1.0e-4F;

[[nodiscard]] Scene::ColliderComponent MakeBox(const Math::Vector3 halfExtents) {
    Scene::ColliderComponent collider;
    collider.shapeType = Scene::ColliderShapeType::Box;
    collider.halfExtents = halfExtents;
    return collider;
}

[[nodiscard]] Scene::ColliderComponent MakeSphere(const float radius) {
    Scene::ColliderComponent collider;
    collider.shapeType = Scene::ColliderShapeType::Sphere;
    collider.radius = radius;
    return collider;
}

[[nodiscard]] Scene::ColliderComponent MakeCapsule(const float radius, const float height) {
    Scene::ColliderComponent collider;
    collider.shapeType = Scene::ColliderShapeType::Capsule;
    collider.radius = radius;
    collider.height = height;
    return collider;
}

[[nodiscard]] Scene::ColliderComponent MakeCylinder(const float radius, const float height) {
    Scene::ColliderComponent collider;
    collider.shapeType = Scene::ColliderShapeType::Cylinder;
    collider.radius = radius;
    collider.height = height;
    return collider;
}

/// Every line the builder produced for one collider, sized from the module's own capacity call -
/// which is also what proves that call is an upper bound rather than a guess.
[[nodiscard]] std::vector<ColliderDebugLine> Build(const Scene::ColliderComponent& collider,
                                                         const Math::Vector3& center = {},
                                                         const Math::Quaternion& rotation = {},
                                                         const ColliderDebugStyle& style = {}) {
    std::vector<ColliderDebugLine> lines(ComputeColliderDebugLineCapacity(collider, style));
    const ColliderDebugBuildResult result = BuildColliderDebugLines(collider, center, rotation, lines, style);
    EXPECT_FALSE(result.truncated) << "the capacity call must be an upper bound";
    lines.resize(result.lineCount);
    return lines;
}

struct Bounds final {
    Math::Vector3 min{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::infinity()};
    Math::Vector3 max{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()};

    void Add(const Math::Vector3& point) noexcept {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }
};

[[nodiscard]] Bounds BoundsOf(const std::vector<ColliderDebugLine>& lines) {
    Bounds bounds;
    for (const ColliderDebugLine& line : lines) {
        bounds.Add(line.from);
        bounds.Add(line.to);
    }
    return bounds;
}

// ---- Box -------------------------------------------------------------------------------------

TEST(ColliderDebugGeometryTest, BoxIsTwelveEdgesOfTheAuthoredExtents) {
    const std::vector<ColliderDebugLine> lines = Build(MakeBox({1.0F, 2.0F, 3.0F}));
    ASSERT_EQ(lines.size(), 12U);
    const Bounds bounds = BoundsOf(lines);
    EXPECT_NEAR(bounds.min.x, -1.0F, kTolerance);
    EXPECT_NEAR(bounds.max.x, 1.0F, kTolerance);
    EXPECT_NEAR(bounds.min.y, -2.0F, kTolerance);
    EXPECT_NEAR(bounds.max.y, 2.0F, kTolerance);
    EXPECT_NEAR(bounds.min.z, -3.0F, kTolerance);
    EXPECT_NEAR(bounds.max.z, 3.0F, kTolerance);

    // Every edge spans exactly one axis before any rotation: an edge with two non-zero components
    // would be a wireframe of something that is not a box.
    for (const ColliderDebugLine& line : lines) {
        const Math::Vector3 delta{line.to.x - line.from.x, line.to.y - line.from.y, line.to.z - line.from.z};
        const int nonZero = (std::fabs(delta.x) > kTolerance ? 1 : 0) +
                            (std::fabs(delta.y) > kTolerance ? 1 : 0) +
                            (std::fabs(delta.z) > kTolerance ? 1 : 0);
        EXPECT_EQ(nonZero, 1);
    }
}

TEST(ColliderDebugGeometryTest, BoxFollowsTheWorldTransform) {
    Math::Quaternion rotation;
    ASSERT_TRUE(Math::TryMakeAxisAngle({0.0F, 1.0F, 0.0F}, 1.57079633F, rotation));
    const std::vector<ColliderDebugLine> lines =
        Build(MakeBox({1.0F, 1.0F, 2.0F}), Math::Vector3{5.0F, 1.0F, 0.0F}, rotation);
    ASSERT_EQ(lines.size(), 12U);

    // A quarter turn about +Y sends the local +X axis onto world -Z, so the 2 m half extent that
    // was local Z now spans world X and the 1 m one spans world Z. That swap is the whole point:
    // the wireframe has to be the rotated shape, not its unrotated bounds.
    const Bounds bounds = BoundsOf(lines);
    EXPECT_NEAR(bounds.min.x, 3.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.x, 7.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.min.y, 0.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.y, 2.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.min.z, -1.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.z, 1.0F, 1.0e-3F);
}

TEST(ColliderDebugGeometryTest, BoxRefusesDegenerateInput) {
    const Scene::ColliderComponent flat = MakeBox({1.0F, 0.0F, 1.0F});
    EXPECT_FALSE(Scene::IsColliderComponentValid(flat));
    EXPECT_EQ(Build(flat).size(), 0U) << "a zero-thickness box is not a shape the solver has";

    EXPECT_EQ(BuildColliderDebugLines(MakeBox({1.0F, 1.0F, 1.0F}), {}, {}, {}).lineCount, 0U)
        << "an empty buffer is a refusal, not a crash";

    // A non-finite centre produces nothing rather than twelve NaN segments.
    std::array<ColliderDebugLine, 16U> storage{};
    const ColliderDebugBuildResult result = BuildColliderDebugLines(
        MakeBox({1.0F, 1.0F, 1.0F}), {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, {}, storage);
    EXPECT_EQ(result.lineCount, 0U);
    EXPECT_FALSE(result.truncated) << "a refusal is not a truncation";
}

// ---- Sphere -----------------------------------------------------------------------------------

TEST(ColliderDebugGeometryTest, SphereIsThreeCirclesOnTheRadius) {
    const std::vector<ColliderDebugLine> lines = Build(MakeSphere(2.0F));
    ASSERT_EQ(lines.size(), 72U) << "3 circles x 24 segments";

    for (const ColliderDebugLine& line : lines) {
        EXPECT_NEAR(std::sqrt(Math::LengthSquared(line.from)), 2.0F, 1.0e-3F);
        EXPECT_NEAR(std::sqrt(Math::LengthSquared(line.to)), 2.0F, 1.0e-3F);
    }
    const Bounds bounds = BoundsOf(lines);
    EXPECT_NEAR(bounds.min.x, -2.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.y, 2.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.z, 2.0F, 1.0e-3F);
}

TEST(ColliderDebugGeometryTest, SegmentCountIsHonouredAndClamped) {
    ColliderDebugStyle style;
    style.circleSegments = 8U;
    EXPECT_EQ(Build(MakeSphere(1.0F), {}, {}, style).size(), 24U);

    style.circleSegments = 2U; // below the floor, clamped up to 8
    EXPECT_EQ(ResolveColliderDebugSegments(style), 8U);
    style.circleSegments = 1000U; // above the ceiling, clamped down to 96
    EXPECT_EQ(ResolveColliderDebugSegments(style), 96U);
    style.circleSegments = 25U; // odd counts round UP, so a capsule's fans land on ring vertices
    EXPECT_EQ(ResolveColliderDebugSegments(style), 26U);
}

// ---- Capsule ----------------------------------------------------------------------------------

TEST(ColliderDebugGeometryTest, CapsuleStandsAlongLocalYAndSpansRadiusPlusBarrel) {
    const std::vector<ColliderDebugLine> lines =
        Build(MakeCapsule(/*radius=*/0.5F, /*height=*/3.0F), Math::Vector3{0.0F, 10.0F, 0.0F});
    ASSERT_EQ(lines.size(), 4U * 24U + 4U);

    const Bounds bounds = BoundsOf(lines);
    EXPECT_NEAR(bounds.min.y, 8.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.y, 11.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.min.x, -0.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.z, 0.5F, 1.0e-3F);
}

TEST(ColliderDebugGeometryTest, CapsuleDrawsASphereWhenThereIsNoBarrel) {
    // height == diameter: no barrel at all, so the shape IS a sphere at the centre.
    const std::vector<ColliderDebugLine> lines = Build(MakeCapsule(1.0F, 2.0F));
    ASSERT_EQ(lines.size(), 4U * 24U + 4U);
    const Bounds bounds = BoundsOf(lines);
    EXPECT_NEAR(bounds.min.y, -1.0F, 1.0e-3F) << "the barrel must collapse, not invert";
    EXPECT_NEAR(bounds.max.y, 1.0F, 1.0e-3F);
}

TEST(ColliderDebugGeometryTest, CapsuleTurnsWithItsRotation) {
    Math::Quaternion rotation;
    ASSERT_TRUE(Math::TryMakeAxisAngle({1.0F, 0.0F, 0.0F}, 1.57079633F, rotation));
    // A quarter turn about X lays the local +Y barrel along world -Z.
    const std::vector<ColliderDebugLine> lines = Build(MakeCapsule(0.5F, 3.0F), {}, rotation);
    const Bounds bounds = BoundsOf(lines);
    EXPECT_NEAR(bounds.max.z, 1.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.min.z, -1.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.y, 0.5F, 1.0e-3F);
}

// ---- Cylinder and the bounds view -------------------------------------------------------------

TEST(ColliderDebugGeometryTest, CylinderIsTwoRingsAndFourBarrels) {
    const std::vector<ColliderDebugLine> lines = Build(MakeCylinder(0.25F, 2.0F));
    ASSERT_EQ(lines.size(), 2U * 24U + 4U);
    const Bounds bounds = BoundsOf(lines);
    EXPECT_NEAR(bounds.max.y, 1.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.min.y, -1.0F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.x, 0.25F, 1.0e-3F);
}

TEST(ColliderDebugGeometryTest, BoundsOnlyDrawsTheBoxTheBroadphaseSees) {
    ColliderDebugStyle style;
    style.drawBoundsOnly = true;
    // A capsule's AABB is its radius in X/Z and its half height in Y - the shape the collision cache
    // hands the broadphase, and the reason a capsule's corners can register a hit.
    const std::vector<ColliderDebugLine> lines = Build(MakeCapsule(0.5F, 3.0F), {}, {}, style);
    ASSERT_EQ(lines.size(), 12U);
    const Bounds bounds = BoundsOf(lines);
    EXPECT_NEAR(bounds.max.y, 1.5F, 1.0e-3F);
    EXPECT_NEAR(bounds.max.x, 0.5F, 1.0e-3F);
}

// ---- The module's own bounds ------------------------------------------------------------------

TEST(ColliderDebugGeometryTest, CapacityIsExactForEveryShape) {
    const std::array<Scene::ColliderComponent, 4U> shapes{
        MakeBox({1.0F, 1.0F, 1.0F}), MakeSphere(1.0F), MakeCapsule(0.5F, 2.0F),
        MakeCylinder(0.5F, 2.0F)};
    for (const Scene::ColliderComponent& shape : shapes) {
        std::vector<ColliderDebugLine> storage(ComputeColliderDebugLineCapacity(shape));
        const ColliderDebugBuildResult result = BuildColliderDebugLines(shape, {}, {}, storage);
        EXPECT_EQ(result.lineCount, storage.size()) << "capacity must be exact, not merely large";
        EXPECT_FALSE(result.truncated);
    }
}

TEST(ColliderDebugGeometryTest, ShortBufferTruncatesCleanly) {
    std::array<ColliderDebugLine, 5U> storage{};
    const ColliderDebugBuildResult result = BuildColliderDebugLines(MakeSphere(1.0F), {}, {}, storage);
    EXPECT_EQ(result.lineCount, 5U);
    EXPECT_TRUE(result.truncated);
    for (const ColliderDebugLine& line : storage) {
        EXPECT_TRUE(Math::IsFinite(line.from));
        EXPECT_TRUE(Math::IsFinite(line.to));
    }
}

} // namespace
} // namespace UVE::Physics::Tests
