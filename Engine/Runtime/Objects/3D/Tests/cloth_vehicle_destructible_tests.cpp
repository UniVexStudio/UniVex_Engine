// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

#include "destructible_3d.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "soft_body_3d.h"
#include "transform_component.h"
#include "vehicle_3d.h"
#include "world_transform_component.h"

namespace UVE::Scene {
namespace {

TEST(XpbdClothSoftBodyTest, Simulates2DClothGridWithWindAndSphereCollision) {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager em{memoryManager.GetDefaultAllocator(), eventSystem};
    const Entity clothEntity = em.CreateEntity();
    SoftBody3DObjectDefinition def{};
    def.softBody = BuildClothGridSoftBody3D(5U, 5U, 0.25F, Math::Vector3{0.0F, 2.0F, 0.0F}, true);
    def.softBody.windVelocity = Math::Vector3{0.0F, 0.0F, 4.0F};
    def.softBody.windDrag = 1.5F;
    ApplySoftBody3DObjectDefinition(em, clothEntity, def);

    const SoftBodySphereCollider sphere{Math::Vector3{0.5F, 1.4F, 0.45F}, 0.25F};
    const std::vector<SoftBodySphereCollider> spheres{sphere};

    for (int step = 0; step < 30; ++step) {
        ASSERT_EQ(UpdateSoftBody3DObjects(em, Math::Vector3{0.0F, -9.81F, 0.0F}, 0.016F, spheres),
                  1U);
    }

    const auto& sb = em.GetComponent<SoftBody3DComponent>(clothEntity);
    ASSERT_EQ(sb.points.size(), 25U);
    // Top row stayed pinned at y == 2.0.
    for (std::uint32_t x = 0U; x < 5U; ++x) {
        EXPECT_FLOAT_EQ(sb.points[x].position.y, 2.0F);
    }
    // Bottom row pushed forward in +Z by wind and kept outside the sphere collider.
    EXPECT_GT(sb.points[22].position.z, 0.01F);
    for (const SoftBody3DPoint& pt : sb.points) {
        if (!pt.pinned) {
            EXPECT_GE(Math::Length(pt.position - sphere.center), sphere.radius - 1.0e-3F);
        }
    }
}

TEST(Vehicle6DofRaycastTest, AckermannSteeringBrakeClampAndRaycastSuspension) {
    const auto [leftAngle, rightAngle] = EvaluateAckermannSteering(0.35F, 2.6F, 1.6F);
    EXPECT_GT(leftAngle, rightAngle);
    EXPECT_GT(rightAngle, 0.0F);

    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager em{memoryManager.GetDefaultAllocator(), eventSystem};
    const Entity chassis = em.CreateEntity();
    VehicleBody3DObjectDefinition vehDef{};
    vehDef.vehicle.mass = 1200.0F;
    vehDef.vehicle.engineForce = 3600.0F;
    vehDef.vehicle.steeringAngleRadians = 0.25F;
    ApplyVehicleBody3DObjectDefinition(em, chassis, vehDef);
    em.GetComponent<TransformComponent>(chassis).localPosition = Math::Vector3{0.0F, 0.5F, 0.0F};

    const std::array<Math::Vector3, 4U> offsets{{
        {-0.8F, 0.0F, -1.3F}, {0.8F, 0.0F, -1.3F},
        {-0.8F, 0.0F,  1.3F}, {0.8F, 0.0F,  1.3F},
    }};
    std::vector<Entity> wheels;
    for (std::size_t i = 0U; i < 4U; ++i) {
        const Entity w = em.CreateEntity();
        VehicleWheel3DObjectDefinition wd{};
        wd.wheel.vehicleBodyEntity = chassis;
        wd.wheel.localMountOffset = offsets[i];
        wd.wheel.isSteerable = (i < 2U);
        wd.wheel.isPowered = (i >= 2U);
        wd.wheel.isBraking = true;
        ApplyVehicleWheel3DObjectDefinition(em, w, wd);
        wheels.push_back(w);
    }

    // Flat ground plane at y = 0.
    const VehicleRaycastCallback flatGround = [](const Math::Vector3& origin,
                                                    const Math::Vector3& dir,
                                                    float maxDist) -> VehicleGroundSample {
        VehicleGroundSample s{};
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
        ASSERT_EQ(UpdateVehicle3DObjects(em, 0.05F, flatGround), 1U);
    }
    auto& body = em.GetComponent<VehicleBody3DComponent>(chassis);
    EXPECT_EQ(body.groundedWheelCount, 4U);
    EXPECT_GT(body.currentSpeedMetresPerSecond, 1.0F);
    EXPECT_GT(em.GetComponent<VehicleWheel3DComponent>(wheels[0]).suspensionForce, 0.0F);

    // Braking with zero engine force must slow to 0 and NEVER accelerate backwards!
    body.engineForce = 0.0F;
    body.brakeForce = 20000.0F;
    body.steeringAngleRadians = 0.0F;
    for (int i = 0; i < 40; ++i) {
        ASSERT_EQ(UpdateVehicle3DObjects(em, 0.05F, flatGround), 1U);
    }
    EXPECT_NEAR(body.currentSpeedMetresPerSecond, 0.0F, 1.0e-3F);
}

TEST(Destructible3DTest, FracturesConservingVolumeAndExpiresDebris) {
    const Math::Aabb bounds{Math::Vector3{-1.0F, -1.0F, -1.0F}, Math::Vector3{1.0F, 1.0F, 1.0F}};
    const auto chunks =
        FractureBoundsIntoChunks(bounds, 8U, 16.0F, 42U, Math::Vector3{0.0F, 0.0F, 0.0F}, 8.0F);
    ASSERT_EQ(chunks.size(), 8U);

    float totalMass = 0.0F;
    for (const DestructibleChunk& c : chunks) {
        EXPECT_EQ(c.vertices.size(), 8U);
        EXPECT_EQ(c.indices.size(), 36U);
        totalMass += c.mass;
    }
    EXPECT_NEAR(totalMass, 16.0F, 0.05F);

    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager em{memoryManager.GetDefaultAllocator(), eventSystem};
    const Entity crate = em.CreateEntity();
    em.AddComponent<TransformComponent>(crate, TransformComponent{});
    Destructible3DComponent comp{};
    comp.chunkCount = 6U;
    comp.debrisLifetimeSeconds = 0.5F;
    em.AddComponent<Destructible3DComponent>(crate, comp);

    std::vector<Entity> debris;
    ASSERT_TRUE(ApplyDestructibleImpact(em, crate, 50.0F, Math::Vector3{0.0F, 0.0F, 0.0F}, &debris));
    EXPECT_EQ(debris.size(), 6U);
    EXPECT_TRUE(em.GetComponent<Destructible3DComponent>(crate).fractured);

    EXPECT_EQ(UpdateDestructibleDebris(em, Math::Vector3{0.0F, -9.81F, 0.0F}, 0.25F), 6U);
    EXPECT_EQ(UpdateDestructibleDebris(em, Math::Vector3{0.0F, -9.81F, 0.0F}, 0.35F), 0U);
    for (const Entity d : debris) {
        EXPECT_FALSE(em.IsAlive(d));
    }
}

} // namespace
} // namespace UVE::Scene
