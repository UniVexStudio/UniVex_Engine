// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/core/script_gameplay_bindings_uve.h"

#include <vector>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/physics/collision_system_uve.h"
#include "uve/scene/components/collider_component_uve.h"
#include "uve/scene/components/transform_component_uve.h"
#include "uve/scene/entity_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Core::Tests {
namespace {

class ScriptGameplayBindingsUVETest : public ::testing::Test {
protected:
    Events::EventSystemUVE eventSystem;
    Input::InputSystemUVE inputSystem{eventSystem};
    ScriptGameplayBindingContextUVE context{&inputSystem};
    Scripting::ScriptEngineCallBindingsUVE bindings = MakeScriptGameplayBindingsUVE(context);
};

TEST_F(ScriptGameplayBindingsUVETest, InputKeyPressedReflectsRealInputSystemEdge) {
    ASSERT_NE(bindings.inputKeyPressed, nullptr);
    bool result = true;
    ASSERT_TRUE(bindings.inputKeyPressed(bindings.userData, static_cast<float>(Input::KeyCodeUVE::W), &result));
    EXPECT_FALSE(result);

    inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::W, true);
    inputSystem.UpdateUVE();
    ASSERT_TRUE(bindings.inputKeyPressed(bindings.userData, static_cast<float>(Input::KeyCodeUVE::W), &result));
    EXPECT_TRUE(result);

    // Still down on the next frame, but no longer a fresh press.
    inputSystem.UpdateUVE();
    ASSERT_TRUE(bindings.inputKeyPressed(bindings.userData, static_cast<float>(Input::KeyCodeUVE::W), &result));
    EXPECT_FALSE(result);
}

TEST_F(ScriptGameplayBindingsUVETest, InputKeyDownReflectsHeldState) {
    ASSERT_NE(bindings.inputKeyDown, nullptr);
    inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::Space, true);
    inputSystem.UpdateUVE();
    bool result = false;
    ASSERT_TRUE(bindings.inputKeyDown(bindings.userData, static_cast<float>(Input::KeyCodeUVE::Space), &result));
    EXPECT_TRUE(result);

    inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::Space, false);
    inputSystem.UpdateUVE();
    ASSERT_TRUE(bindings.inputKeyDown(bindings.userData, static_cast<float>(Input::KeyCodeUVE::Space), &result));
    EXPECT_FALSE(result);
}

TEST_F(ScriptGameplayBindingsUVETest, InputKeyReleasedReflectsRealInputSystemEdge) {
    ASSERT_NE(bindings.inputKeyReleased, nullptr);
    inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::A, true);
    inputSystem.UpdateUVE();
    inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::A, false);
    inputSystem.UpdateUVE();
    bool result = false;
    ASSERT_TRUE(bindings.inputKeyReleased(bindings.userData, static_cast<float>(Input::KeyCodeUVE::A), &result));
    EXPECT_TRUE(result);
}

TEST_F(ScriptGameplayBindingsUVETest, InputKeyBindingsRejectOutOfRangeTokensAndNullOutputs) {
    bool result = false;
    EXPECT_FALSE(bindings.inputKeyPressed(
        bindings.userData, static_cast<float>(Input::KeyCodeUVE::Count), &result));
    EXPECT_FALSE(bindings.inputKeyDown(bindings.userData, -1.0F, &result));
    EXPECT_FALSE(bindings.inputKeyDown(
        bindings.userData, static_cast<float>(Input::KeyCodeUVE::A), nullptr));
    EXPECT_FALSE(bindings.inputKeyDown(nullptr, static_cast<float>(Input::KeyCodeUVE::A), &result));
}

TEST_F(ScriptGameplayBindingsUVETest, InputMousePositionReflectsRealInputSystemState) {
    ASSERT_NE(bindings.inputMousePosition, nullptr);
    inputSystem.SetMousePositionUVE(Math::Vector2UVE{12.5F, -4.0F});
    inputSystem.UpdateUVE();
    Scripting::ScriptVector2ValueUVE position{};
    ASSERT_TRUE(bindings.inputMousePosition(bindings.userData, &position));
    EXPECT_FLOAT_EQ(position.value.x, 12.5F);
    EXPECT_FLOAT_EQ(position.value.y, -4.0F);

    EXPECT_FALSE(bindings.inputMousePosition(bindings.userData, nullptr));
}

TEST_F(ScriptGameplayBindingsUVETest, InputMouseButtonReflectsHeldStateAndRejectsOutOfRangeTokens) {
    ASSERT_NE(bindings.inputMouseButton, nullptr);
    inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Right, true);
    inputSystem.UpdateUVE();
    bool result = false;
    ASSERT_TRUE(bindings.inputMouseButton(
        bindings.userData, static_cast<float>(Input::MouseButtonUVE::Right), &result));
    EXPECT_TRUE(result);

    EXPECT_FALSE(bindings.inputMouseButton(
        bindings.userData, static_cast<float>(Input::MouseButtonUVE::Count), &result));
}

TEST_F(ScriptGameplayBindingsUVETest, UnwiredBindingsStayNullThisIncrement) {
    // Documents the honest, explicit scope of this increment - see script_gameplay_bindings_uve.h's
    // own doc comment for why these stay unset rather than a partial/fake implementation.
    EXPECT_EQ(bindings.inputGamepadButton, nullptr);
    EXPECT_EQ(bindings.inputAxis, nullptr);
    EXPECT_EQ(bindings.inputAction, nullptr);
    EXPECT_EQ(bindings.spawnEntity, nullptr);
    EXPECT_EQ(bindings.cameraGet, nullptr);
    EXPECT_EQ(bindings.audioPlaySound, nullptr);
}

class ScriptGameplayBindingsCollisionUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Physics::CollisionSystemUVE collisionSystem;
    Physics::CollisionLifecycleTrackerUVE tracker;
    Physics::CollisionLifecycleReportUVE report;
    ScriptGameplayBindingContextUVE context{};
    Scripting::ScriptEngineCallBindingsUVE bindings = MakeScriptGameplayBindingsUVE(context);

    [[nodiscard]] Scene::EntityUVE MakeColliderEntityUVE(const Math::Vector3UVE& position) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(entity, Scene::ColliderComponentUVE{});
        return entity;
    }

    // Mirrors EngineCoreUVE::SyncCollisionLifecycleUVE() exactly: a fresh DetectCollisionsUVE()
    // snapshot diffed through the tracker, with the context re-pointed at this tick's transitions.
    void TickUVE() {
        sceneGraph.UpdateUVE(entityManager);
        const std::vector<Physics::CollisionPairUVE> pairs = collisionSystem.DetectCollisionsUVE(entityManager);
        report = tracker.UpdateUVE(pairs);
        context.collisionTransitionsThisTick = &report.transitions;
    }
};

TEST_F(ScriptGameplayBindingsCollisionUVETest, CollisionEnterReflectsRealOverlappingCollidersOnFirstTouchingFrame) {
    ASSERT_NE(bindings.physicsCollisionEnter, nullptr);
    const Scene::EntityUVE bodyA = MakeColliderEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE bodyB = MakeColliderEntityUVE(Math::Vector3UVE{10.0F, 0.0F, 0.0F});
    TickUVE();

    Scene::EntityUVE other = bodyB;
    bool result = true;
    ASSERT_TRUE(bindings.physicsCollisionEnter(bindings.userData, bodyA, &other, &result));
    EXPECT_FALSE(result);
    EXPECT_EQ(other, Scene::kInvalidEntityUVE);

    Scene::TransformComponentUVE moved;
    moved.localPosition = Math::Vector3UVE{0.2F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, bodyB, moved);
    TickUVE();

    ASSERT_TRUE(bindings.physicsCollisionEnter(bindings.userData, bodyA, &other, &result));
    EXPECT_TRUE(result);
    EXPECT_EQ(other, bodyB);

    // Stable overlap on the following tick is no longer a fresh "enter".
    TickUVE();
    ASSERT_TRUE(bindings.physicsCollisionEnter(bindings.userData, bodyA, &other, &result));
    EXPECT_FALSE(result);
}

TEST_F(ScriptGameplayBindingsCollisionUVETest, CollisionExitReflectsRealCollidersSeparatingOnLeavingFrame) {
    ASSERT_NE(bindings.physicsCollisionExit, nullptr);
    const Scene::EntityUVE bodyA = MakeColliderEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const Scene::EntityUVE bodyB = MakeColliderEntityUVE(Math::Vector3UVE{0.2F, 0.0F, 0.0F});
    TickUVE();

    Scene::TransformComponentUVE separated;
    separated.localPosition = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransformUVE(entityManager, bodyB, separated);
    TickUVE();

    Scene::EntityUVE other = Scene::kInvalidEntityUVE;
    bool result = false;
    ASSERT_TRUE(bindings.physicsCollisionExit(bindings.userData, bodyA, &other, &result));
    EXPECT_TRUE(result);
    EXPECT_EQ(other, bodyB);

    // Already apart on the following tick is no longer a fresh "exit".
    TickUVE();
    ASSERT_TRUE(bindings.physicsCollisionExit(bindings.userData, bodyA, &other, &result));
    EXPECT_FALSE(result);
}

TEST_F(ScriptGameplayBindingsCollisionUVETest, CollisionBindingsRejectInvalidEntityAndNullOutputs) {
    const Scene::EntityUVE bodyA = MakeColliderEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    TickUVE();

    Scene::EntityUVE other = Scene::kInvalidEntityUVE;
    bool result = false;
    EXPECT_FALSE(bindings.physicsCollisionEnter(bindings.userData, Scene::kInvalidEntityUVE, &other, &result));
    EXPECT_FALSE(bindings.physicsCollisionEnter(bindings.userData, bodyA, nullptr, &result));
    EXPECT_FALSE(bindings.physicsCollisionEnter(bindings.userData, bodyA, &other, nullptr));
    EXPECT_FALSE(bindings.physicsCollisionEnter(nullptr, bodyA, &other, &result));
}

} // namespace
} // namespace UVE::Core::Tests
