// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "physics_material.h"

#include <limits>

#include <gtest/gtest.h>

#include "collider_component.h"

namespace UVE::Physics::Tests {
namespace {

constexpr float kEpsilon = 1e-5F;

TEST(ColliderComponentTest, IsColliderComponentValid_RejectsUnsafeValues) {
    EXPECT_TRUE(Scene::IsColliderComponentValid(Scene::ColliderComponent{}));

    Scene::ColliderComponent invalid = {};
    invalid.halfExtents.x = 0.0F;
    EXPECT_FALSE(Scene::IsColliderComponentValid(invalid));
    invalid = {};
    invalid.halfExtents.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Scene::IsColliderComponentValid(invalid));
    invalid = {};
    invalid.collisionLayer = 0U;
    EXPECT_FALSE(Scene::IsColliderComponentValid(invalid));
    invalid = {};
    invalid.friction = 1.1F;
    EXPECT_FALSE(Scene::IsColliderComponentValid(invalid));
    invalid = {};
    invalid.restitution = -0.1F;
    EXPECT_FALSE(Scene::IsColliderComponentValid(invalid));
    invalid = {};
    invalid.density = 0.0F;
    EXPECT_FALSE(Scene::IsColliderComponentValid(invalid));
    invalid = {};
    invalid.density = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Scene::IsColliderComponentValid(invalid));
}

TEST(PhysicsMaterialTest, CombineMaterials_KnownValues_AverageCorrectly) {
    constexpr PhysicsMaterial a{0.2F, 0.4F, 1.0F};
    constexpr PhysicsMaterial b{0.6F, 0.8F, 3.0F};

    constexpr PhysicsMaterial combined = CombineMaterials(a, b);

    EXPECT_NEAR(combined.friction, 0.4F, kEpsilon);
    EXPECT_NEAR(combined.restitution, 0.6F, kEpsilon);
    EXPECT_NEAR(combined.density, 2.0F, kEpsilon);
}

TEST(PhysicsMaterialTest, CombineMaterials_BothZero_StaysZero) {
    constexpr PhysicsMaterial a{0.0F, 0.0F, 0.0F};
    constexpr PhysicsMaterial b{0.0F, 0.0F, 0.0F};

    constexpr PhysicsMaterial combined = CombineMaterials(a, b);

    EXPECT_NEAR(combined.friction, 0.0F, kEpsilon);
    EXPECT_NEAR(combined.restitution, 0.0F, kEpsilon);
    EXPECT_NEAR(combined.density, 0.0F, kEpsilon);
}

TEST(PhysicsMaterialTest, MaterialOf_InRangeValues_PassThroughUnchanged) {
    Scene::ColliderComponent collider;
    collider.friction = 0.3F;
    collider.restitution = 0.7F;
    collider.density = 2.5F;

    const PhysicsMaterial material = MaterialOf(collider);

    EXPECT_NEAR(material.friction, 0.3F, kEpsilon);
    EXPECT_NEAR(material.restitution, 0.7F, kEpsilon);
    EXPECT_NEAR(material.density, 2.5F, kEpsilon);
}

TEST(PhysicsMaterialTest, MaterialOf_OutOfRangeRestitution_ClampsToOne) {
    Scene::ColliderComponent collider;
    collider.restitution = 5.0F;

    EXPECT_NEAR(MaterialOf(collider).restitution, 1.0F, kEpsilon);
}

TEST(PhysicsMaterialTest, MaterialOf_NonFiniteFrictionAndRestitutionFailClosed) {
    Scene::ColliderComponent collider;
    collider.friction = std::numeric_limits<float>::quiet_NaN();
    collider.restitution = std::numeric_limits<float>::infinity();
    const PhysicsMaterial material = MaterialOf(collider);
    EXPECT_FLOAT_EQ(material.friction, 0.0F);
    EXPECT_FLOAT_EQ(material.restitution, 0.0F);
}

TEST(PhysicsMaterialTest, MaterialOf_InvalidDensityFallsBackToUnitDensity) {
    Scene::ColliderComponent collider;
    collider.density = -1.0F;
    EXPECT_FLOAT_EQ(MaterialOf(collider).density, 1.0F);
    collider.density = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(MaterialOf(collider).density, 1.0F);
}

TEST(PhysicsMaterialTest, MaterialOf_NegativeFriction_ClampsToZero) {
    Scene::ColliderComponent collider;
    collider.friction = -1.0F;

    EXPECT_NEAR(MaterialOf(collider).friction, 0.0F, kEpsilon);
}

TEST(PhysicsMaterialTest, CombineModesOverridePrecedenceAndSurfaceTagWorkDeterministically) {
    EXPECT_NEAR(CombineScalarByMode(0.2F, 0.8F, PhysicsCombineMode::Average, PhysicsCombineMode::Minimum),
                0.2F, kEpsilon);
    EXPECT_NEAR(CombineScalarByMode(0.5F, 0.5F, PhysicsCombineMode::Minimum, PhysicsCombineMode::Multiply),
                0.25F, kEpsilon);
    EXPECT_NEAR(CombineScalarByMode(0.2F, 0.9F, PhysicsCombineMode::Multiply, PhysicsCombineMode::Maximum),
                0.9F, kEpsilon);

    Scene::ColliderComponent collider{};
    collider.friction = 0.35F;
    PhysicsMaterialAsset projectDefault{};
    projectDefault.friction = 0.6F;
    projectDefault.surfaceTypeTag = "concrete";
    PhysicsMaterialAsset perColliderOverride{};
    perColliderOverride.friction = 0.1F;
    perColliderOverride.rollingFriction = 0.002F;
    perColliderOverride.surfaceTypeTag = "ice";

    const PhysicsMaterialAsset resolvedOverride =
        ResolveColliderPhysicsMaterial(collider, &perColliderOverride, &projectDefault);
    EXPECT_NEAR(resolvedOverride.friction, 0.1F, kEpsilon);
    EXPECT_EQ(resolvedOverride.surfaceTypeTag, "ice");

    const PhysicsMaterialAsset resolvedDefault =
        ResolveColliderPhysicsMaterial(collider, nullptr, &projectDefault);
    EXPECT_NEAR(resolvedDefault.friction, 0.6F, kEpsilon);
    EXPECT_EQ(resolvedDefault.surfaceTypeTag, "concrete");
}

} // namespace
} // namespace UVE::Physics::Tests
