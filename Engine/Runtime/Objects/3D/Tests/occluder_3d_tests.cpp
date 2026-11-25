// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "occluder_3d.h"

#include <vector>

#include <gtest/gtest.h>

#include "editor_internal_entity_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "aabb.h"
#include "memory_manager.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class Occluder3DTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    Entity PlaceOccluder(Occluder3DComponent config, const TransformComponent& transform = {}) {
        const Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<Occluder3DComponent>(entity, config);
        sceneGraph.Update(entityManager);
        return entity;
    }
};

TEST(Occluder3DAabbTest, ABoxWhollyBehindTheWallIsHiddenAndAPeekingBoxIsNot) {
    Occluder3DComponent wall;
    wall.halfExtents = Math::Vector3{2.0F, 1.0F, 2.0F};
    const Math::Vector3 wallOrigin{};
    const Math::Vector3 viewer{0.0F, 0.0F, 5.0F};

    const Math::Aabb hidden =
        Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, -5.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
    EXPECT_TRUE(ResolveOccluder3DFullyHidesAabb(wall, wallOrigin, viewer, hidden));

    const Math::Aabb peeking =
        Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, -5.0F}, Math::Vector3{8.0F, 0.5F, 0.5F});
    EXPECT_FALSE(ResolveOccluder3DFullyHidesAabb(wall, wallOrigin, viewer, peeking))
        << "a corner that clears the wall must keep the whole drawable";

    const Math::Aabb unordered{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{-1.0F, 0.0F, 0.0F}};
    EXPECT_FALSE(ResolveOccluder3DFullyHidesAabb(wall, wallOrigin, viewer, unordered));
}

TEST_F(Occluder3DTest, CollectSnapshotsSkipsDisabledInvalidAndEditorInternal) {
    Occluder3DComponent wall;
    TransformComponent transform{};
    transform.localPosition = Math::Vector3{0.0F, 0.0F, -5.0F};
    PlaceOccluder(wall, transform);

    Occluder3DComponent disabled = wall;
    disabled.enabled = false;
    TransformComponent disabledTransform{};
    disabledTransform.localPosition = Math::Vector3{10.0F, 0.0F, -5.0F};
    PlaceOccluder(disabled, disabledTransform);

    const Entity hidden = PlaceOccluder(wall);
    entityManager.AddComponent<EditorInternalEntityComponent>(hidden);

    std::vector<Occluder3DSnapshot> snapshots;
    CollectOccluder3DSnapshots(entityManager, snapshots);
    ASSERT_EQ(snapshots.size(), 1U);
    EXPECT_NEAR(snapshots[0U].worldPosition.z, -5.0F, 1.0e-4F);

    const Math::Vector3 viewer{};
    const Math::Vector3 behind{0.0F, 0.0F, -9.0F};
    EXPECT_TRUE(IsOccluder3DPointDrawHidden(snapshots, viewer, behind));
    EXPECT_FALSE(IsOccluder3DPointDrawHidden(snapshots, viewer, Math::Vector3{8.0F, 0.0F, -9.0F}));
}

TEST_F(Occluder3DTest, CollectGizmosWritesTheCenteredBox) {
    Occluder3DComponent wall;
    wall.halfExtents = Math::Vector3{2.0F, 3.0F, 4.0F};
    TransformComponent transform{};
    transform.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    PlaceOccluder(wall, transform);

    std::vector<Occluder3DGizmo> gizmos;
    CollectOccluder3DGizmos(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_NEAR(gizmos[0U].origin.x, 1.0F, 1.0e-4F);
    EXPECT_FLOAT_EQ(gizmos[0U].halfExtents.y, 3.0F);
    EXPECT_TRUE(gizmos[0U].enabled);
}

} // namespace
} // namespace UVE::Scene::Tests
