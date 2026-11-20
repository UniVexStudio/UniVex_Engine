// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "destructible_3d.h"
#include "entity_manager.h"
#include "soft_body_3d.h"
#include "transform_component.h"
#include "vehicle_3d.h"
#include "world_transform_component.h"

namespace UVE::Scene {
namespace {

TEST(XpbdClothSoftBodyUVETest, Simulates2DClothGridWithWindAndSphereCollision) {
    EntityManagerUVE em;
    const EntityUVE clothEntity = em.CreateEntityUVE();
    SoftBody3DObjectDefinitionUVE def{};
    def.softBody = BuildClothGridSoftBody3DUVE(5U, 5U, 0.25F, Math::Vector3UVE{0.0F, 2.0F, 0.0F}, true);
    def.softBody.windVelocity = Math::Vector3UVE{0.0F, 0.0F, 4.0F};
    def.softBody.windDrag = 1.5F;
    ApplySoftBody3DObjectDefinitionUVE(em, clothEntity, def);

    const SoftBodySphereColliderUVE sphere{Math::Vector3UVE{0.5F, 1.4F, 0.1F}, 0.35F};
    const std::vector<SoftBodySphereColliderUVE> spheres{sphere};

    for (int step = 0; step < 30; ++step) {
        ASSERT_EQ(UpdateSoftBody3DObjectsUVE(em, Math::Vector3UVE{0.0F, -9.81F, 0.0F}, 0.016F, spheres),
                  1U);
    }

    const auto& sb = em.GetComponentUVE<SoftBody3DComponentUVE>(clothEntity);
    ASSERT_EQ(sb.points.size(), 25U);
    // Top row stayed pinned at y == 2.0.
    for (std::uint32_t x = 0U; x < 5U; ++x) {
        EXPECT_FLOAT_EQ(sb.points[x].position.y, 2.0F);
    }
    // Bottom row pushed forward in +Z by wind and kept outside the sphere collider.
    EXPECT_GT(sb.points[22].position.z, 0.01F);
    for (const SoftBody3DPointUVE& pt : sb.points) {
        if (!pt.pinned) {
            EXPECT_GE(Math::LengthUVE(pt.position - sphere.center), sphere.radius - 1.0e-3F);
        }
    }
}

TEST(Vehicle6DofRaycastUVETest, AckermannSteeringBrakeClampAndRaycastSuspension) {
    const auto [leftAngle, rightAngle] = EvaluateAckermannSteeringUVE(0.35F, 2.6F, 1.6F);
    EXPECT_GT(leftAngle, rightAngle);
    EXPECT_GT(rightAngle, 0.0F);

    EntityManagerUVE em;
    const EntityUVE chassis = em.CreateEntityUVE();
    VehicleBody3DObjectDefinitionUVE vehDef{};
    vehDef.vehicle.mass = 1200.0F;
    vehDef.vehicle.engineForce = 3600.0F;
    vehDef.vehicle.steeringAngleRadians = 0.25F;
    ApplyVehicleBody3DObjectDefinitionUVE(em, chassis, vehDef);
    em.GetComponentUVE<TransformComponentUVE>(chassis).localPosition = Math::Vector3UVE{0.0F, 0.5F, 0.0F};

    const std::array<Math::Vector3UVE, 4U> offsets{{
        {-0.8F, 0.0F, -1.3F}, {0.8F, 0.0F, -1.3F},
        {-0.8F, 0.0F,  1.3F}, {0.8F, 0.0F,  1.3F},
    }};
    std::vector<EntityUVE> wheels;
    for (std::size_t i = 0U; i < 4U; ++i) {
        const EntityUVE w = em.CreateEntityUVE();
        VehicleWheel3DObjectDefinitionUVE wd{};
        wd.wheel.vehicleBodyEntity = chassis;
        wd.wheel.localMountOffset = offsets[i];
        wd.wheel.isSteerable = (i < 2U);
        wd.wheel.isPowered = (i >= 2U);
        wd.wheel.isBraking = true;
        ApplyVehicleWheel3DObjectDefinitionUVE(em, w, wd);
        wheels.push_back(w);
    }

    // Flat ground plane at y = 0.
    const VehicleRaycastCallbackUVE flatGround = [](const Math::Vector3UVE& origin,
                                                    const Math::Vector3UVE& dir,
                                                    float maxDist) -> VehicleGroundSampleUVE {
        VehicleGroundSampleUVE s{};
        if (dir.y < -1.0e-4F) {
            const float t = -origin.y / dir.y;
            if (t >= 0.0F && t <= maxDist) {
                s.hit = true;
                s.hitDistance = t;
                s.surfaceFriction = 1.0F;
            }
        }
        return s;
    };

    for (int i = 0; i < 20; ++i) {
        ASSERT_EQ(UpdateVehicle3DObjectsUVE(em, 0.05F, flatGround), 1U);
    }
    auto& body = em.GetComponentUVE<VehicleBody3DComponentUVE>(chassis);
    EXPECT_EQ(body.groundedWheelCount, 4U);
    EXPECT_GT(body.currentSpeedMetresPerSecond, 1.0F);
    EXPECT_GT(em.GetComponentUVE<VehicleWheel3DComponentUVE>(wheels[0]).suspensionForce, 0.0F);

    // Braking with zero engine force must slow to 0 and NEVER accelerate backwards!
    body.engineForce = 0.0F;
    body.brakeForce = 20000.0F;
    body.steeringAngleRadians = 0.0F;
    for (int i = 0; i < 40; ++i) {
        ASSERT_EQ(UpdateVehicle3DObjectsUVE(em, 0.05F, flatGround), 1U);
    }
    EXPECT_NEAR(body.currentSpeedMetresPerSecond, 0.0F, 1.0e-3F);
}

TEST(Destructible3DUVETest, FracturesConservingVolumeAndExpiresDebris) {
    const Math::AabbUVE bounds{Math::Vector3UVE{-1.0F, -1.0F, -1.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
    const auto chunks =
        FractureBoundsIntoChunksUVE(bounds, 8U, 16.0F, 42U, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 8.0F);
    ASSERT_EQ(chunks.size(), 8U);

    float totalMass = 0.0F;
    for (const DestructibleChunkUVE& c : chunks) {
        EXPECT_EQ(c.vertices.size(), 8U);
        EXPECT_EQ(c.indices.size(), 36U);
        totalMass += c.mass;
    }
    EXPECT_NEAR(totalMass, 16.0F, 0.05F);

    EntityManagerUVE em;
    const EntityUVE crate = em.CreateEntityUVE();
    em.AddComponentUVE<TransformComponentUVE>(crate, TransformComponentUVE{});
    Destructible3DComponentUVE comp{};
    comp.chunkCount = 6U;
    comp.debrisLifetimeSeconds = 0.5F;
    em.AddComponentUVE<Destructible3DComponentUVE>(crate, comp);

    std::vector<EntityUVE> debris;
    ASSERT_TRUE(ApplyDestructibleImpactUVE(em, crate, 50.0F, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, &debris));
    EXPECT_EQ(debris.size(), 6U);
    EXPECT_TRUE(em.GetComponentUVE<Destructible3DComponentUVE>(crate).fractured);

    EXPECT_EQ(UpdateDestructibleDebrisUVE(em, Math::Vector3UVE{0.0F, -9.81F, 0.0F}, 0.25F), 6U);
    EXPECT_EQ(UpdateDestructibleDebrisUVE(em, Math::Vector3UVE{0.0F, -9.81F, 0.0F}, 0.35F), 0U);
    for (const EntityUVE d : debris) {
        EXPECT_FALSE(em.IsAliveUVE(d));
    }
}

} // namespace
} // namespace UVE::Scene
