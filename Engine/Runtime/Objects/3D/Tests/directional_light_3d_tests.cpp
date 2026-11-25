// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "directional_light_3d.h"

#include <gtest/gtest.h>

#include <limits>

#include "editor_internal_entity_component.h"
#include "light_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "memory_manager.h"
#include "light_3d.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class DirectionalLight3DTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;
};

TEST(DirectionalLight3DComponentTest, ShadowDefaultsAreValidAndRejectOutOfRangeValues) {
    const DirectionalLight3DComponent defaults;
    EXPECT_FLOAT_EQ(defaults.shadowSplitBlend, -1.0F);
    EXPECT_FLOAT_EQ(defaults.shadowDistanceFadeRange, 10.0F);
    EXPECT_TRUE(IsDirectionalLight3DComponentValid(defaults));

    DirectionalLight3DComponent invalid = defaults;
    invalid.shadowSplitBlend = -1.01F;
    EXPECT_FALSE(IsDirectionalLight3DComponentValid(invalid));
    invalid = defaults;
    invalid.shadowSplitBlend = 1.01F;
    EXPECT_FALSE(IsDirectionalLight3DComponentValid(invalid));
    invalid = defaults;
    invalid.shadowDistanceFadeRange = -0.1F;
    EXPECT_FALSE(IsDirectionalLight3DComponentValid(invalid));
    invalid.shadowDistanceFadeRange = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsDirectionalLight3DComponentValid(invalid));
}

TEST_F(DirectionalLight3DTest, IdentityRotationPointsAlongNegativeZ) {
    const Entity sun = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, sun, TransformComponent{});
    ApplyDirectionalLight3DObjectDefinition(entityManager, sun, DirectionalLight3DObjectDefinition{});
    sceneGraph.Update(entityManager);

    std::vector<LightDirectionGizmo> gizmos;
    CollectLightDirectionGizmos(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_NEAR(gizmos[0].direction.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(gizmos[0].direction.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(gizmos[0].direction.z, -1.0F, 1.0e-5F);
}

TEST_F(DirectionalLight3DTest, YawTurnsTheGizmoDirection) {
    const Entity sun = entityManager.CreateEntity();
    TransformComponent transform{};
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, 1.57079632679F, transform.localRotation));
    sceneGraph.AttachTransform(entityManager, sun, transform);
    ApplyDirectionalLight3DObjectDefinition(entityManager, sun, DirectionalLight3DObjectDefinition{});
    sceneGraph.Update(entityManager);

    std::vector<LightDirectionGizmo> gizmos;
    CollectLightDirectionGizmos(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_NEAR(gizmos[0].direction.x, -1.0F, 1.0e-4F);
    EXPECT_NEAR(gizmos[0].direction.y, 0.0F, 1.0e-4F);
    EXPECT_NEAR(gizmos[0].direction.z, 0.0F, 1.0e-4F);
}

TEST_F(DirectionalLight3DTest, ZeroEnergyStillDrawsAGizmo) {
    const Entity sun = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, sun, TransformComponent{});
    ApplyDirectionalLight3DObjectDefinition(entityManager, sun, DirectionalLight3DObjectDefinition{});
    entityManager.GetComponent<LightEmitterComponent>(sun).energy = 0.0F;
    sceneGraph.Update(entityManager);

    std::vector<LightDirectionGizmo> gizmos;
    CollectLightDirectionGizmos(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_GT(gizmos[0].color.x, 0.0F);
}

TEST_F(DirectionalLight3DTest, EditorInternalLightsAreHiddenAndLight3DDirectionalIsCollected) {
    const Entity hidden = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, hidden, TransformComponent{});
    ApplyDirectionalLight3DObjectDefinition(entityManager, hidden, DirectionalLight3DObjectDefinition{});
    entityManager.AddComponent<EditorInternalEntityComponent>(hidden);

    const Entity lamp = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, lamp, TransformComponent{});
    ApplyLight3DObjectDefinition(entityManager, lamp, Light3DObjectDefinition{});
    sceneGraph.Update(entityManager);

    std::vector<LightDirectionGizmo> gizmos;
    CollectLightDirectionGizmos(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_NEAR(gizmos[0].direction.z, -1.0F, 1.0e-5F);
}

} // namespace
} // namespace UVE::Scene::Tests
