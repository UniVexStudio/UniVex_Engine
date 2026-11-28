// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <limits>

#include <gtest/gtest.h>

#include "character_controller_component.h"
#include "entity.h"
#include "rigid_3d_component.h"
#include "blueprint_object_host.h"
#include "entity_manager.h"
#include "event_system.h"
#include "vector3.h"
#include "memory_manager.h"
#include "rigid_3d.h"
#include "blueprint_value.h"

namespace UVE::Scene::Tests {
namespace {

class BlueprintPhysicsForcesTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    Entity MakeRigid(const float mass, const bool kinematic, const Math::Vector3 velocity) {
        const Entity entity = entityManager.CreateEntity();
        Rigid3DComponent body;
        body.mass = mass;
        body.isKinematic = kinematic;
        body.velocity = velocity;
        entityManager.AddComponent<Rigid3DComponent>(entity, body);
        return entity;
    }

    static Blueprint::Value Vec3Arg(const double x, const double y, const double z) {
        return Blueprint::Value{Blueprint::Vec3Value{x, y, z}};
    }
};

TEST_F(BlueprintPhysicsForcesTest, ApplyForce_StoresPersistentForceUntilChanged) {
    const Entity body = MakeRigid(2.0F, false, Math::Vector3{});
    EXPECT_TRUE(Rigid3D::ApplyForce(entityManager, body, Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(body).force,
              (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_TRUE(Rigid3D::ApplyForce(entityManager, body, Math::Vector3{}));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(body).force, (Math::Vector3{}));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Rigid3D::ApplyForce(entityManager, body, Math::Vector3{nan, 0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(body).force, (Math::Vector3{}));
    EXPECT_FALSE(Rigid3D::ApplyForce(entityManager, entityManager.CreateEntity(),
                                           Math::Vector3{1.0F, 0.0F, 0.0F}));
}

TEST_F(BlueprintPhysicsForcesTest, ApplyImpulse_ScalesByInverseMass) {
    const Entity body = MakeRigid(2.0F, false, Math::Vector3{1.0F, 1.0F, 1.0F});
    EXPECT_TRUE(Rigid3D::ApplyImpulse(entityManager, body, Math::Vector3{4.0F, -2.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(body).velocity,
              (Math::Vector3{3.0F, 0.0F, 1.0F}));

    const Entity frozen = MakeRigid(2.0F, true, Math::Vector3{1.0F, 0.0F, 0.0F});
    EXPECT_FALSE(Rigid3D::ApplyImpulse(entityManager, frozen, Math::Vector3{4.0F, 0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(frozen).velocity,
              (Math::Vector3{1.0F, 0.0F, 0.0F}));
    const Entity massless = MakeRigid(0.0F, false, Math::Vector3{});
    EXPECT_FALSE(Rigid3D::ApplyImpulse(entityManager, massless, Math::Vector3{4.0F, 0.0F, 0.0F}));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Rigid3D::ApplyImpulse(entityManager, body, Math::Vector3{nan, 0.0F, 0.0F}));
}

TEST_F(BlueprintPhysicsForcesTest, ApplyTorque_StoresPersistentTorque) {
    const Entity body = MakeRigid(1.0F, false, Math::Vector3{});
    EXPECT_TRUE(Rigid3D::ApplyTorque(entityManager, body, Math::Vector3{0.0F, 0.0F, 5.0F}));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(body).torque,
              (Math::Vector3{0.0F, 0.0F, 5.0F}));
    EXPECT_FALSE(Rigid3D::ApplyTorque(entityManager, entityManager.CreateEntity(),
                                            Math::Vector3{0.0F, 0.0F, 5.0F}));
}

TEST_F(BlueprintPhysicsForcesTest, IntegrateLinearVelocity_AddsUnscaledForceAcceleration) {
    const auto pushed = Rigid3D::IntegrateLinearVelocity(Math::Vector3{}, Math::Vector3{}, 1.0F, 0.0F,
                                                               0.5F, Math::Vector3{0.0F, 10.0F, 0.0F}, 0.5F);
    ASSERT_TRUE(pushed.has_value());
    EXPECT_FLOAT_EQ(pushed->y, 2.5F);

    // Gravity switched off, thruster on: the force term ignores gravityScale.
    const auto thrusting =
        Rigid3D::IntegrateLinearVelocity(Math::Vector3{}, Math::Vector3{0.0F, -10.0F, 0.0F}, 0.0F,
                                               0.0F, 1.0F, Math::Vector3{0.0F, 10.0F, 0.0F}, 0.5F);
    ASSERT_TRUE(thrusting.has_value());
    EXPECT_FLOAT_EQ(thrusting->y, 5.0F);

    EXPECT_FALSE(Rigid3D::IntegrateLinearVelocity(Math::Vector3{}, Math::Vector3{}, 1.0F, 0.0F,
                                                        0.5F, Math::Vector3{0.0F, 10.0F, 0.0F}, -0.5F)
                     .has_value());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Rigid3D::IntegrateLinearVelocity(Math::Vector3{}, Math::Vector3{}, 1.0F, 0.0F,
                                                        0.5F, Math::Vector3{nan, 0.0F, 0.0F}, 0.5F)
                     .has_value());
}

TEST_F(BlueprintPhysicsForcesTest, Host_DescribesPhysicsSurfaceOnlyOnRigidBodies) {
    const Entity body = MakeRigid(1.0F, false, Math::Vector3{});
    const Core::BlueprintObjectHost host{entityManager, nullptr, body};
    for (const char* name : {"physics.apply_force", "physics.apply_impulse", "physics.apply_torque"}) {
        const std::optional<Blueprint::HostFunction> described = host.DescribeFunction(name);
        ASSERT_TRUE(described.has_value()) << name;
        ASSERT_EQ(described->params.size(), 1U);
        EXPECT_EQ(described->params[0].kind, Blueprint::Type::Kind::Vec3);
        EXPECT_EQ(described->result.kind, Blueprint::Type::Kind::Bool);
    }
    const std::optional<Blueprint::HostProperty> velocity = host.DescribeProperty("velocity");
    ASSERT_TRUE(velocity.has_value());
    EXPECT_EQ(velocity->type.kind, Blueprint::Type::Kind::Vec3);
    EXPECT_TRUE(velocity->writable);

    const Core::BlueprintObjectHost bare{entityManager, nullptr, entityManager.CreateEntity()};
    EXPECT_FALSE(bare.DescribeFunction("physics.apply_force").has_value());
    EXPECT_FALSE(bare.DescribeFunction("physics.apply_impulse").has_value());
    EXPECT_FALSE(bare.DescribeFunction("physics.apply_torque").has_value());
    EXPECT_FALSE(bare.DescribeProperty("velocity").has_value());
}

TEST_F(BlueprintPhysicsForcesTest, Host_CallFunction_AppliesForcesWithoutInput) {
    const Entity body = MakeRigid(2.0F, false, Math::Vector3{});
    Core::BlueprintObjectHost host{entityManager, nullptr, body};

    std::array<Blueprint::Value, 1U> forceArgs{Vec3Arg(1.0, 0.0, 0.0)};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("physics.apply_force", forceArgs)));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(body).force,
              (Math::Vector3{1.0F, 0.0F, 0.0F}));

    std::array<Blueprint::Value, 1U> impulseArgs{Vec3Arg(4.0, 0.0, 0.0)};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("physics.apply_impulse", impulseArgs)));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(body).velocity,
              (Math::Vector3{2.0F, 0.0F, 0.0F}));

    std::array<Blueprint::Value, 1U> torqueArgs{Vec3Arg(0.0, 0.0, 5.0)};
    EXPECT_TRUE(std::get<bool>(host.CallFunction("physics.apply_torque", torqueArgs)));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(body).torque,
              (Math::Vector3{0.0F, 0.0F, 5.0F}));
}

TEST_F(BlueprintPhysicsForcesTest, Host_VelocityProperty_RoundTripsWithCharacterPriority) {
    const Entity body = MakeRigid(1.0F, false, Math::Vector3{});
    Core::BlueprintObjectHost host{entityManager, nullptr, body};
    host.SetProperty("velocity", Vec3Arg(1.0, 2.0, 3.0));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(body).velocity,
              (Math::Vector3{1.0F, 2.0F, 3.0F}));
    const Blueprint::Vec3Value readBack = std::get<Blueprint::Vec3Value>(host.GetProperty("velocity"));
    EXPECT_DOUBLE_EQ(readBack.x, 1.0);
    EXPECT_DOUBLE_EQ(readBack.y, 2.0);
    EXPECT_DOUBLE_EQ(readBack.z, 3.0);

    const Entity hybrid = MakeRigid(1.0F, false, Math::Vector3{1.0F, 1.0F, 1.0F});
    CharacterControllerComponent character;
    character.velocity = Math::Vector3{9.0F, 9.0F, 9.0F};
    entityManager.AddComponent<CharacterControllerComponent>(hybrid, character);
    Core::BlueprintObjectHost hybridHost{entityManager, nullptr, hybrid};
    const Blueprint::Vec3Value hybridRead =
        std::get<Blueprint::Vec3Value>(hybridHost.GetProperty("velocity"));
    EXPECT_DOUBLE_EQ(hybridRead.x, 9.0);
    hybridHost.SetProperty("velocity", Vec3Arg(7.0, 7.0, 7.0));
    EXPECT_EQ(entityManager.GetComponent<CharacterControllerComponent>(hybrid).velocity,
              (Math::Vector3{7.0F, 7.0F, 7.0F}));
    EXPECT_EQ(entityManager.GetComponent<Rigid3DComponent>(hybrid).velocity,
              (Math::Vector3{1.0F, 1.0F, 1.0F}));
}

} // namespace
} // namespace UVE::Scene::Tests
