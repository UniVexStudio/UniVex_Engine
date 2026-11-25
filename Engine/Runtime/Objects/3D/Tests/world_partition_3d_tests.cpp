// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "world_partition_3d.h"

#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "editor_internal_entity_component.h"
#include "light_component.h"
#include "mesh_component.h"
#include "particle_emitter_component.h"
#include "primitive_mesh_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "decal_3d.h"
#include "fog_volume_3d.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class WorldPartition3DTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    Entity PlacePartition(WorldPartition3DComponent config, const TransformComponent& transform = {}) {
        const Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<WorldPartition3DComponent>(entity, config);
        sceneGraph.Update(entityManager);
        return entity;
    }

    Entity PlaceEmpty(const TransformComponent& transform = {}) {
        const Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        return entity;
    }
};

TEST(WorldPartition3DBudgetTest, NearestOccupiedCellWinsTheSingleSlot) {
    const std::array<std::uint32_t, 3U> counts{4U, 1U, 4U};
    std::vector<WorldPartition3DOccupiedCell> occupied{
        WorldPartition3DOccupiedCell{{1, 0, 0}, 16.0F},
        WorldPartition3DOccupiedCell{{0, 0, 0}, 4.0F},
    };
    SortWorldPartition3DOccupiedCells(occupied, counts);
    ASSERT_EQ(occupied.size(), 2U);
    EXPECT_EQ(occupied[0U].id, (WorldPartition3DCellId{0, 0, 0}));
    EXPECT_EQ(occupied[1U].id, (WorldPartition3DCellId{1, 0, 0}));
    EXPECT_EQ(CountWorldPartition3DAdmittedCells(occupied.size(), 1U), 1U);
    EXPECT_TRUE(IsWorldPartition3DCellAdmitted({0, 0, 0}, occupied, 1U));
    EXPECT_FALSE(IsWorldPartition3DCellAdmitted({1, 0, 0}, occupied, 1U));
    EXPECT_TRUE(IsWorldPartition3DCellAdmitted({1, 0, 0}, occupied, 2U));
}

TEST(WorldPartition3DBudgetTest, EqualDistancePrefersTheLowerLinearIndex) {
    const std::array<std::uint32_t, 3U> counts{4U, 1U, 4U};
    std::vector<WorldPartition3DOccupiedCell> occupied{
        WorldPartition3DOccupiedCell{{1, 0, 0}, 9.0F},
        WorldPartition3DOccupiedCell{{0, 0, 0}, 9.0F},
    };
    SortWorldPartition3DOccupiedCells(occupied, counts);
    EXPECT_EQ(occupied[0U].id, (WorldPartition3DCellId{0, 0, 0}))
        << "x-major index 0 beats index 1 at the same distance";
    EXPECT_TRUE(IsWorldPartition3DCellAdmitted({0, 0, 0}, occupied, 1U));
    EXPECT_FALSE(IsWorldPartition3DCellAdmitted({1, 0, 0}, occupied, 1U));
}

TEST(WorldPartition3DBudgetTest, EmptyOccupiedAdmitsNothingAndUnknownCellsStayOut) {
    std::vector<WorldPartition3DOccupiedCell> occupied;
    EXPECT_EQ(CountWorldPartition3DAdmittedCells(0U, 8U), 0U);
    EXPECT_FALSE(IsWorldPartition3DCellAdmitted({0, 0, 0}, occupied, 8U));
}

TEST(WorldPartition3DBudgetTest, VolumeSizeIsCellSizeTimesCounts) {
    WorldPartition3DComponent config;
    config.cellSize = 10.0F;
    config.cellCounts = {2U, 1U, 3U};
    const Math::Vector3 size = ResolveWorldPartition3DVolumeSize(config);
    EXPECT_FLOAT_EQ(size.x, 20.0F);
    EXPECT_FLOAT_EQ(size.y, 10.0F);
    EXPECT_FLOAT_EQ(size.z, 30.0F);
}

TEST_F(WorldPartition3DTest, DrawableKindsCarryMembershipAndLightsDoNot) {
    const Entity mesh = PlaceEmpty();
    entityManager.AddComponent<MeshComponent>(mesh, MeshComponent{});
    const Entity primitive = PlaceEmpty();
    entityManager.AddComponent<PrimitiveMeshComponent>(primitive);
    const Entity decal = PlaceEmpty();
    entityManager.AddComponent<Decal3DComponent>(decal, Decal3DComponent{});
    const Entity particles = PlaceEmpty();
    entityManager.AddComponent<ParticleEmitterComponent>(particles);
    const Entity fog = PlaceEmpty();
    entityManager.AddComponent<FogVolume3DComponent>(fog, FogVolume3DComponent{});
    const Entity light = PlaceEmpty();
    entityManager.AddComponent<LightComponent>(light);
    const Entity empty = PlaceEmpty();

    EXPECT_TRUE(CarriesWorldPartition3DDrawable(entityManager, mesh));
    EXPECT_TRUE(CarriesWorldPartition3DDrawable(entityManager, primitive));
    EXPECT_TRUE(CarriesWorldPartition3DDrawable(entityManager, decal));
    EXPECT_TRUE(CarriesWorldPartition3DDrawable(entityManager, particles));
    EXPECT_TRUE(CarriesWorldPartition3DDrawable(entityManager, fog));
    EXPECT_FALSE(CarriesWorldPartition3DDrawable(entityManager, light));
    EXPECT_FALSE(CarriesWorldPartition3DDrawable(entityManager, empty));
}

TEST_F(WorldPartition3DTest, DrawHiddenTrustsLiveFlagAndFailsOpenWhenTheOwnerDies) {
    const Entity partition = PlacePartition(WorldPartition3DComponent{});
    const Entity faded = PlaceEmpty();
    entityManager.AddComponent<WorldPartition3DMembershipComponent>(
        faded, WorldPartition3DMembershipComponent{partition, false});
    EXPECT_TRUE(IsWorldPartition3DDrawHidden(entityManager, faded));

    const Entity live = PlaceEmpty();
    entityManager.AddComponent<WorldPartition3DMembershipComponent>(
        live, WorldPartition3DMembershipComponent{partition, true});
    EXPECT_FALSE(IsWorldPartition3DDrawHidden(entityManager, live));
    EXPECT_FALSE(IsWorldPartition3DDrawHidden(entityManager, PlaceEmpty()))
        << "no membership: unmanaged, always drawn";

    entityManager.DestroyEntity(partition);
    EXPECT_FALSE(IsWorldPartition3DDrawHidden(entityManager, faded))
        << "dead owner fails open";
}

TEST_F(WorldPartition3DTest, CollectGizmosWritesTheAxisAlignedVolumeAndSkipsEditorInternal) {
    WorldPartition3DComponent config;
    config.cellSize = 8.0F;
    config.cellCounts = {2U, 1U, 4U};
    TransformComponent transform{};
    transform.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    PlacePartition(config, transform);

    WorldPartition3DComponent disabled = config;
    disabled.enabled = false;
    TransformComponent disabledTransform{};
    disabledTransform.localPosition = Math::Vector3{40.0F, 0.0F, 0.0F};
    PlacePartition(disabled, disabledTransform);

    const Entity hidden = PlacePartition(config);
    entityManager.AddComponent<EditorInternalEntityComponent>(hidden);

    std::vector<WorldPartition3DGizmo> gizmos;
    CollectWorldPartition3DGizmos(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 2U);
    const WorldPartition3DGizmo* enabledGizmo = nullptr;
    const WorldPartition3DGizmo* disabledGizmo = nullptr;
    for (const WorldPartition3DGizmo& gizmo : gizmos) {
        if (gizmo.enabled) {
            enabledGizmo = &gizmo;
        } else {
            disabledGizmo = &gizmo;
        }
    }
    ASSERT_NE(enabledGizmo, nullptr);
    ASSERT_NE(disabledGizmo, nullptr);
    EXPECT_NEAR(enabledGizmo->origin.x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(enabledGizmo->origin.y, 2.0F, 1.0e-4F);
    EXPECT_NEAR(enabledGizmo->origin.z, 3.0F, 1.0e-4F);
    EXPECT_FLOAT_EQ(enabledGizmo->size.x, 16.0F);
    EXPECT_FLOAT_EQ(enabledGizmo->size.y, 8.0F);
    EXPECT_FLOAT_EQ(enabledGizmo->size.z, 32.0F);
    EXPECT_NEAR(disabledGizmo->origin.x, 40.0F, 1.0e-4F);
}

} // namespace
} // namespace UVE::Scene::Tests
