// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/directional_light_3d_uve.h"

#include <gtest/gtest.h>

#include <limits>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/light_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class DirectionalLight3DUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;
};

TEST(DirectionalLight3DComponentUVETest, ShadowDefaultsAreValidAndRejectOutOfRangeValues) {
    const DirectionalLight3DComponentUVE defaults;
    EXPECT_FLOAT_EQ(defaults.shadowSplitBlend, -1.0F);
    EXPECT_FLOAT_EQ(defaults.shadowDistanceFadeRange, 10.0F);
    EXPECT_TRUE(IsDirectionalLight3DComponentValidUVE(defaults));

    DirectionalLight3DComponentUVE invalid = defaults;
    invalid.shadowSplitBlend = -1.01F;
    EXPECT_FALSE(IsDirectionalLight3DComponentValidUVE(invalid));
    invalid = defaults;
    invalid.shadowSplitBlend = 1.01F;
    EXPECT_FALSE(IsDirectionalLight3DComponentValidUVE(invalid));
    invalid = defaults;
    invalid.shadowDistanceFadeRange = -0.1F;
    EXPECT_FALSE(IsDirectionalLight3DComponentValidUVE(invalid));
    invalid.shadowDistanceFadeRange = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsDirectionalLight3DComponentValidUVE(invalid));
}

TEST_F(DirectionalLight3DUVETest, IdentityRotationPointsAlongNegativeZ) {
    const EntityUVE sun = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, sun, TransformComponentUVE{});
    ApplyDirectionalLight3DObjectDefinitionUVE(entityManager, sun, DirectionalLight3DObjectDefinitionUVE{});
    sceneGraph.UpdateUVE(entityManager);

    std::vector<LightDirectionGizmoUVE> gizmos;
    CollectLightDirectionGizmosUVE(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_NEAR(gizmos[0].direction.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(gizmos[0].direction.y, 0.0F, 1.0e-5F);
    EXPECT_NEAR(gizmos[0].direction.z, -1.0F, 1.0e-5F);
}

TEST_F(DirectionalLight3DUVETest, YawTurnsTheGizmoDirection) {
    const EntityUVE sun = entityManager.CreateEntityUVE();
    TransformComponentUVE transform{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.57079632679F, transform.localRotation));
    sceneGraph.AttachTransformUVE(entityManager, sun, transform);
    ApplyDirectionalLight3DObjectDefinitionUVE(entityManager, sun, DirectionalLight3DObjectDefinitionUVE{});
    sceneGraph.UpdateUVE(entityManager);

    std::vector<LightDirectionGizmoUVE> gizmos;
    CollectLightDirectionGizmosUVE(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_NEAR(gizmos[0].direction.x, -1.0F, 1.0e-4F);
    EXPECT_NEAR(gizmos[0].direction.y, 0.0F, 1.0e-4F);
    EXPECT_NEAR(gizmos[0].direction.z, 0.0F, 1.0e-4F);
}

TEST_F(DirectionalLight3DUVETest, ZeroEnergyStillDrawsAGizmo) {
    const EntityUVE sun = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, sun, TransformComponentUVE{});
    ApplyDirectionalLight3DObjectDefinitionUVE(entityManager, sun, DirectionalLight3DObjectDefinitionUVE{});
    entityManager.GetComponentUVE<LightEmitterComponentUVE>(sun).energy = 0.0F;
    sceneGraph.UpdateUVE(entityManager);

    std::vector<LightDirectionGizmoUVE> gizmos;
    CollectLightDirectionGizmosUVE(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_GT(gizmos[0].color.x, 0.0F);
}

TEST_F(DirectionalLight3DUVETest, EditorInternalLightsAreHiddenAndLight3DDirectionalIsCollected) {
    const EntityUVE hidden = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, hidden, TransformComponentUVE{});
    ApplyDirectionalLight3DObjectDefinitionUVE(entityManager, hidden, DirectionalLight3DObjectDefinitionUVE{});
    entityManager.AddComponentUVE<EditorInternalEntityComponentUVE>(hidden);

    const EntityUVE lamp = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, lamp, TransformComponentUVE{});
    ApplyLight3DObjectDefinitionUVE(entityManager, lamp, Light3DObjectDefinitionUVE{});
    sceneGraph.UpdateUVE(entityManager);

    std::vector<LightDirectionGizmoUVE> gizmos;
    CollectLightDirectionGizmosUVE(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_NEAR(gizmos[0].direction.z, -1.0F, 1.0e-5F);
}

} // namespace
} // namespace UVE::Scene::Tests
