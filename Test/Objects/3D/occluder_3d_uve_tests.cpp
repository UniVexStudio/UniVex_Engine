// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/occluder_3d_uve.h"

#include <vector>

#include <gtest/gtest.h>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class Occluder3DUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    EntityUVE PlaceOccluderUVE(Occluder3DComponentUVE config, const TransformComponentUVE& transform = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<Occluder3DComponentUVE>(entity, config);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }
};

TEST(Occluder3DAabbUVETest, ABoxWhollyBehindTheWallIsHiddenAndAPeekingBoxIsNot) {
    Occluder3DComponentUVE wall;
    wall.halfExtents = Math::Vector3UVE{2.0F, 1.0F, 2.0F};
    const Math::Vector3UVE wallOrigin{};
    const Math::Vector3UVE viewer{0.0F, 0.0F, 5.0F};

    const Math::AabbUVE hidden =
        Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F});
    EXPECT_TRUE(ResolveOccluder3DFullyHidesAabbUVE(wall, wallOrigin, viewer, hidden));

    const Math::AabbUVE peeking =
        Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, Math::Vector3UVE{8.0F, 0.5F, 0.5F});
    EXPECT_FALSE(ResolveOccluder3DFullyHidesAabbUVE(wall, wallOrigin, viewer, peeking))
        << "a corner that clears the wall must keep the whole drawable";

    const Math::AabbUVE unordered{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{-1.0F, 0.0F, 0.0F}};
    EXPECT_FALSE(ResolveOccluder3DFullyHidesAabbUVE(wall, wallOrigin, viewer, unordered));
}

TEST_F(Occluder3DUVETest, CollectSnapshotsSkipsDisabledInvalidAndEditorInternal) {
    Occluder3DComponentUVE wall;
    TransformComponentUVE transform{};
    transform.localPosition = Math::Vector3UVE{0.0F, 0.0F, -5.0F};
    PlaceOccluderUVE(wall, transform);

    Occluder3DComponentUVE disabled = wall;
    disabled.enabled = false;
    TransformComponentUVE disabledTransform{};
    disabledTransform.localPosition = Math::Vector3UVE{10.0F, 0.0F, -5.0F};
    PlaceOccluderUVE(disabled, disabledTransform);

    const EntityUVE hidden = PlaceOccluderUVE(wall);
    entityManager.AddComponentUVE<EditorInternalEntityComponentUVE>(hidden);

    std::vector<Occluder3DSnapshotUVE> snapshots;
    CollectOccluder3DSnapshotsUVE(entityManager, snapshots);
    ASSERT_EQ(snapshots.size(), 1U);
    EXPECT_NEAR(snapshots[0U].worldPosition.z, -5.0F, 1.0e-4F);

    const Math::Vector3UVE viewer{};
    const Math::Vector3UVE behind{0.0F, 0.0F, -9.0F};
    EXPECT_TRUE(IsOccluder3DPointDrawHiddenUVE(snapshots, viewer, behind));
    EXPECT_FALSE(IsOccluder3DPointDrawHiddenUVE(snapshots, viewer, Math::Vector3UVE{8.0F, 0.0F, -9.0F}));
}

TEST_F(Occluder3DUVETest, CollectGizmosWritesTheCenteredBox) {
    Occluder3DComponentUVE wall;
    wall.halfExtents = Math::Vector3UVE{2.0F, 3.0F, 4.0F};
    TransformComponentUVE transform{};
    transform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    PlaceOccluderUVE(wall, transform);

    std::vector<Occluder3DGizmoUVE> gizmos;
    CollectOccluder3DGizmosUVE(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 1U);
    EXPECT_NEAR(gizmos[0U].origin.x, 1.0F, 1.0e-4F);
    EXPECT_FLOAT_EQ(gizmos[0U].halfExtents.y, 3.0F);
    EXPECT_TRUE(gizmos[0U].enabled);
}

} // namespace
} // namespace UVE::Scene::Tests
