// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <limits>

#include <gtest/gtest.h>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/core/uvscript_object_host_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/rigid_3d_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Scene::Tests {
namespace {

class UVScriptPhysicsForcesUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    EntityUVE MakeRigidUVE(const float mass, const bool kinematic, const Math::Vector3UVE velocity) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Rigid3DComponentUVE body;
        body.mass = mass;
        body.isKinematic = kinematic;
        body.velocity = velocity;
        entityManager.AddComponentUVE<Rigid3DComponentUVE>(entity, body);
        return entity;
    }

    static UVScript::ValueUVE Vec3ArgUVE(const double x, const double y, const double z) {
        return UVScript::ValueUVE{UVScript::Vec3ValueUVE{x, y, z}};
    }
};

TEST_F(UVScriptPhysicsForcesUVETest, ApplyForce_StoresPersistentForceUntilChanged) {
    const EntityUVE body = MakeRigidUVE(2.0F, false, Math::Vector3UVE{});
    EXPECT_TRUE(Rigid3DUVE::ApplyForceUVE(entityManager, body, Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(body).force,
              (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_TRUE(Rigid3DUVE::ApplyForceUVE(entityManager, body, Math::Vector3UVE{}));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(body).force, (Math::Vector3UVE{}));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Rigid3DUVE::ApplyForceUVE(entityManager, body, Math::Vector3UVE{nan, 0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(body).force, (Math::Vector3UVE{}));
    EXPECT_FALSE(Rigid3DUVE::ApplyForceUVE(entityManager, entityManager.CreateEntityUVE(),
                                           Math::Vector3UVE{1.0F, 0.0F, 0.0F}));
}

TEST_F(UVScriptPhysicsForcesUVETest, ApplyImpulse_ScalesByInverseMass) {
    const EntityUVE body = MakeRigidUVE(2.0F, false, Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    EXPECT_TRUE(Rigid3DUVE::ApplyImpulseUVE(entityManager, body, Math::Vector3UVE{4.0F, -2.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(body).velocity,
              (Math::Vector3UVE{3.0F, 0.0F, 1.0F}));

    const EntityUVE frozen = MakeRigidUVE(2.0F, true, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    EXPECT_FALSE(Rigid3DUVE::ApplyImpulseUVE(entityManager, frozen, Math::Vector3UVE{4.0F, 0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(frozen).velocity,
              (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));
    const EntityUVE massless = MakeRigidUVE(0.0F, false, Math::Vector3UVE{});
    EXPECT_FALSE(Rigid3DUVE::ApplyImpulseUVE(entityManager, massless, Math::Vector3UVE{4.0F, 0.0F, 0.0F}));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Rigid3DUVE::ApplyImpulseUVE(entityManager, body, Math::Vector3UVE{nan, 0.0F, 0.0F}));
}

TEST_F(UVScriptPhysicsForcesUVETest, ApplyTorque_StoresPersistentTorque) {
    const EntityUVE body = MakeRigidUVE(1.0F, false, Math::Vector3UVE{});
    EXPECT_TRUE(Rigid3DUVE::ApplyTorqueUVE(entityManager, body, Math::Vector3UVE{0.0F, 0.0F, 5.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(body).torque,
              (Math::Vector3UVE{0.0F, 0.0F, 5.0F}));
    EXPECT_FALSE(Rigid3DUVE::ApplyTorqueUVE(entityManager, entityManager.CreateEntityUVE(),
                                            Math::Vector3UVE{0.0F, 0.0F, 5.0F}));
}

TEST_F(UVScriptPhysicsForcesUVETest, IntegrateLinearVelocityUVE_AddsUnscaledForceAcceleration) {
    const auto pushed = Rigid3DUVE::IntegrateLinearVelocityUVE(Math::Vector3UVE{}, Math::Vector3UVE{}, 1.0F, 0.0F,
                                                               0.5F, Math::Vector3UVE{0.0F, 10.0F, 0.0F}, 0.5F);
    ASSERT_TRUE(pushed.has_value());
    EXPECT_FLOAT_EQ(pushed->y, 2.5F);

    // Gravity switched off, thruster on: the force term ignores gravityScale.
    const auto thrusting =
        Rigid3DUVE::IntegrateLinearVelocityUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, -10.0F, 0.0F}, 0.0F,
                                               0.0F, 1.0F, Math::Vector3UVE{0.0F, 10.0F, 0.0F}, 0.5F);
    ASSERT_TRUE(thrusting.has_value());
    EXPECT_FLOAT_EQ(thrusting->y, 5.0F);

    EXPECT_FALSE(Rigid3DUVE::IntegrateLinearVelocityUVE(Math::Vector3UVE{}, Math::Vector3UVE{}, 1.0F, 0.0F,
                                                        0.5F, Math::Vector3UVE{0.0F, 10.0F, 0.0F}, -0.5F)
                     .has_value());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Rigid3DUVE::IntegrateLinearVelocityUVE(Math::Vector3UVE{}, Math::Vector3UVE{}, 1.0F, 0.0F,
                                                        0.5F, Math::Vector3UVE{nan, 0.0F, 0.0F}, 0.5F)
                     .has_value());
}

TEST_F(UVScriptPhysicsForcesUVETest, Host_DescribesPhysicsSurfaceOnlyOnRigidBodies) {
    const EntityUVE body = MakeRigidUVE(1.0F, false, Math::Vector3UVE{});
    const Core::UVScriptObjectHostUVE host{entityManager, nullptr, body};
    for (const char* name : {"physics.apply_force", "physics.apply_impulse", "physics.apply_torque"}) {
        const std::optional<UVScript::HostFunctionUVE> described = host.DescribeFunctionUVE(name);
        ASSERT_TRUE(described.has_value()) << name;
        ASSERT_EQ(described->params.size(), 1U);
        EXPECT_EQ(described->params[0].kind, UVScript::TypeUVE::KindUVE::Vec3);
        EXPECT_EQ(described->result.kind, UVScript::TypeUVE::KindUVE::Bool);
    }
    const std::optional<UVScript::HostPropertyUVE> velocity = host.DescribePropertyUVE("velocity");
    ASSERT_TRUE(velocity.has_value());
    EXPECT_EQ(velocity->type.kind, UVScript::TypeUVE::KindUVE::Vec3);
    EXPECT_TRUE(velocity->writable);

    const Core::UVScriptObjectHostUVE bare{entityManager, nullptr, entityManager.CreateEntityUVE()};
    EXPECT_FALSE(bare.DescribeFunctionUVE("physics.apply_force").has_value());
    EXPECT_FALSE(bare.DescribeFunctionUVE("physics.apply_impulse").has_value());
    EXPECT_FALSE(bare.DescribeFunctionUVE("physics.apply_torque").has_value());
    EXPECT_FALSE(bare.DescribePropertyUVE("velocity").has_value());
}

TEST_F(UVScriptPhysicsForcesUVETest, Host_CallFunction_AppliesForcesWithoutInput) {
    const EntityUVE body = MakeRigidUVE(2.0F, false, Math::Vector3UVE{});
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, body};

    std::array<UVScript::ValueUVE, 1U> forceArgs{Vec3ArgUVE(1.0, 0.0, 0.0)};
    EXPECT_TRUE(std::get<bool>(host.CallFunctionUVE("physics.apply_force", forceArgs)));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(body).force,
              (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));

    std::array<UVScript::ValueUVE, 1U> impulseArgs{Vec3ArgUVE(4.0, 0.0, 0.0)};
    EXPECT_TRUE(std::get<bool>(host.CallFunctionUVE("physics.apply_impulse", impulseArgs)));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(body).velocity,
              (Math::Vector3UVE{2.0F, 0.0F, 0.0F}));

    std::array<UVScript::ValueUVE, 1U> torqueArgs{Vec3ArgUVE(0.0, 0.0, 5.0)};
    EXPECT_TRUE(std::get<bool>(host.CallFunctionUVE("physics.apply_torque", torqueArgs)));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(body).torque,
              (Math::Vector3UVE{0.0F, 0.0F, 5.0F}));
}

TEST_F(UVScriptPhysicsForcesUVETest, Host_VelocityProperty_RoundTripsWithCharacterPriority) {
    const EntityUVE body = MakeRigidUVE(1.0F, false, Math::Vector3UVE{});
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, body};
    host.SetPropertyUVE("velocity", Vec3ArgUVE(1.0, 2.0, 3.0));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(body).velocity,
              (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    const UVScript::Vec3ValueUVE readBack = std::get<UVScript::Vec3ValueUVE>(host.GetPropertyUVE("velocity"));
    EXPECT_DOUBLE_EQ(readBack.x, 1.0);
    EXPECT_DOUBLE_EQ(readBack.y, 2.0);
    EXPECT_DOUBLE_EQ(readBack.z, 3.0);

    const EntityUVE hybrid = MakeRigidUVE(1.0F, false, Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    CharacterControllerComponentUVE character;
    character.velocity = Math::Vector3UVE{9.0F, 9.0F, 9.0F};
    entityManager.AddComponentUVE<CharacterControllerComponentUVE>(hybrid, character);
    Core::UVScriptObjectHostUVE hybridHost{entityManager, nullptr, hybrid};
    const UVScript::Vec3ValueUVE hybridRead =
        std::get<UVScript::Vec3ValueUVE>(hybridHost.GetPropertyUVE("velocity"));
    EXPECT_DOUBLE_EQ(hybridRead.x, 9.0);
    hybridHost.SetPropertyUVE("velocity", Vec3ArgUVE(7.0, 7.0, 7.0));
    EXPECT_EQ(entityManager.GetComponentUVE<CharacterControllerComponentUVE>(hybrid).velocity,
              (Math::Vector3UVE{7.0F, 7.0F, 7.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(hybrid).velocity,
              (Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
}

} // namespace
} // namespace UVE::Scene::Tests
