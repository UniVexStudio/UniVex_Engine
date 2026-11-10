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

class WorldPartition3DUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    EntityUVE PlacePartitionUVE(WorldPartition3DComponentUVE config, const TransformComponentUVE& transform = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<WorldPartition3DComponentUVE>(entity, config);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    EntityUVE PlaceEmptyUVE(const TransformComponentUVE& transform = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }
};

TEST(WorldPartition3DBudgetUVETest, NearestOccupiedCellWinsTheSingleSlot) {
    const std::array<std::uint32_t, 3U> counts{4U, 1U, 4U};
    std::vector<WorldPartition3DOccupiedCellUVE> occupied{
        WorldPartition3DOccupiedCellUVE{{1, 0, 0}, 16.0F},
        WorldPartition3DOccupiedCellUVE{{0, 0, 0}, 4.0F},
    };
    SortWorldPartition3DOccupiedCellsUVE(occupied, counts);
    ASSERT_EQ(occupied.size(), 2U);
    EXPECT_EQ(occupied[0U].id, (WorldPartition3DCellIdUVE{0, 0, 0}));
    EXPECT_EQ(occupied[1U].id, (WorldPartition3DCellIdUVE{1, 0, 0}));
    EXPECT_EQ(CountWorldPartition3DAdmittedCellsUVE(occupied.size(), 1U), 1U);
    EXPECT_TRUE(IsWorldPartition3DCellAdmittedUVE({0, 0, 0}, occupied, 1U));
    EXPECT_FALSE(IsWorldPartition3DCellAdmittedUVE({1, 0, 0}, occupied, 1U));
    EXPECT_TRUE(IsWorldPartition3DCellAdmittedUVE({1, 0, 0}, occupied, 2U));
}

TEST(WorldPartition3DBudgetUVETest, EqualDistancePrefersTheLowerLinearIndex) {
    const std::array<std::uint32_t, 3U> counts{4U, 1U, 4U};
    std::vector<WorldPartition3DOccupiedCellUVE> occupied{
        WorldPartition3DOccupiedCellUVE{{1, 0, 0}, 9.0F},
        WorldPartition3DOccupiedCellUVE{{0, 0, 0}, 9.0F},
    };
    SortWorldPartition3DOccupiedCellsUVE(occupied, counts);
    EXPECT_EQ(occupied[0U].id, (WorldPartition3DCellIdUVE{0, 0, 0}))
        << "x-major index 0 beats index 1 at the same distance";
    EXPECT_TRUE(IsWorldPartition3DCellAdmittedUVE({0, 0, 0}, occupied, 1U));
    EXPECT_FALSE(IsWorldPartition3DCellAdmittedUVE({1, 0, 0}, occupied, 1U));
}

TEST(WorldPartition3DBudgetUVETest, EmptyOccupiedAdmitsNothingAndUnknownCellsStayOut) {
    std::vector<WorldPartition3DOccupiedCellUVE> occupied;
    EXPECT_EQ(CountWorldPartition3DAdmittedCellsUVE(0U, 8U), 0U);
    EXPECT_FALSE(IsWorldPartition3DCellAdmittedUVE({0, 0, 0}, occupied, 8U));
}

TEST(WorldPartition3DBudgetUVETest, VolumeSizeIsCellSizeTimesCounts) {
    WorldPartition3DComponentUVE config;
    config.cellSize = 10.0F;
    config.cellCounts = {2U, 1U, 3U};
    const Math::Vector3UVE size = ResolveWorldPartition3DVolumeSizeUVE(config);
    EXPECT_FLOAT_EQ(size.x, 20.0F);
    EXPECT_FLOAT_EQ(size.y, 10.0F);
    EXPECT_FLOAT_EQ(size.z, 30.0F);
}

TEST_F(WorldPartition3DUVETest, DrawableKindsCarryMembershipAndLightsDoNot) {
    const EntityUVE mesh = PlaceEmptyUVE();
    entityManager.AddComponentUVE<MeshComponentUVE>(mesh, MeshComponentUVE{});
    const EntityUVE primitive = PlaceEmptyUVE();
    entityManager.AddComponentUVE<PrimitiveMeshComponentUVE>(primitive);
    const EntityUVE decal = PlaceEmptyUVE();
    entityManager.AddComponentUVE<Decal3DComponentUVE>(decal, Decal3DComponentUVE{});
    const EntityUVE particles = PlaceEmptyUVE();
    entityManager.AddComponentUVE<ParticleEmitterComponentUVE>(particles);
    const EntityUVE fog = PlaceEmptyUVE();
    entityManager.AddComponentUVE<FogVolume3DComponentUVE>(fog, FogVolume3DComponentUVE{});
    const EntityUVE light = PlaceEmptyUVE();
    entityManager.AddComponentUVE<LightComponentUVE>(light);
    const EntityUVE empty = PlaceEmptyUVE();

    EXPECT_TRUE(CarriesWorldPartition3DDrawableUVE(entityManager, mesh));
    EXPECT_TRUE(CarriesWorldPartition3DDrawableUVE(entityManager, primitive));
    EXPECT_TRUE(CarriesWorldPartition3DDrawableUVE(entityManager, decal));
    EXPECT_TRUE(CarriesWorldPartition3DDrawableUVE(entityManager, particles));
    EXPECT_TRUE(CarriesWorldPartition3DDrawableUVE(entityManager, fog));
    EXPECT_FALSE(CarriesWorldPartition3DDrawableUVE(entityManager, light));
    EXPECT_FALSE(CarriesWorldPartition3DDrawableUVE(entityManager, empty));
}

TEST_F(WorldPartition3DUVETest, DrawHiddenTrustsLiveFlagAndFailsOpenWhenTheOwnerDies) {
    const EntityUVE partition = PlacePartitionUVE(WorldPartition3DComponentUVE{});
    const EntityUVE faded = PlaceEmptyUVE();
    entityManager.AddComponentUVE<WorldPartition3DMembershipComponentUVE>(
        faded, WorldPartition3DMembershipComponentUVE{partition, false});
    EXPECT_TRUE(IsWorldPartition3DDrawHiddenUVE(entityManager, faded));

    const EntityUVE live = PlaceEmptyUVE();
    entityManager.AddComponentUVE<WorldPartition3DMembershipComponentUVE>(
        live, WorldPartition3DMembershipComponentUVE{partition, true});
    EXPECT_FALSE(IsWorldPartition3DDrawHiddenUVE(entityManager, live));
    EXPECT_FALSE(IsWorldPartition3DDrawHiddenUVE(entityManager, PlaceEmptyUVE()))
        << "no membership: unmanaged, always drawn";

    entityManager.DestroyEntityUVE(partition);
    EXPECT_FALSE(IsWorldPartition3DDrawHiddenUVE(entityManager, faded))
        << "dead owner fails open";
}

TEST_F(WorldPartition3DUVETest, CollectGizmosWritesTheAxisAlignedVolumeAndSkipsEditorInternal) {
    WorldPartition3DComponentUVE config;
    config.cellSize = 8.0F;
    config.cellCounts = {2U, 1U, 4U};
    TransformComponentUVE transform{};
    transform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    PlacePartitionUVE(config, transform);

    WorldPartition3DComponentUVE disabled = config;
    disabled.enabled = false;
    TransformComponentUVE disabledTransform{};
    disabledTransform.localPosition = Math::Vector3UVE{40.0F, 0.0F, 0.0F};
    PlacePartitionUVE(disabled, disabledTransform);

    const EntityUVE hidden = PlacePartitionUVE(config);
    entityManager.AddComponentUVE<EditorInternalEntityComponentUVE>(hidden);

    std::vector<WorldPartition3DGizmoUVE> gizmos;
    CollectWorldPartition3DGizmosUVE(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 2U);
    const WorldPartition3DGizmoUVE* enabledGizmo = nullptr;
    const WorldPartition3DGizmoUVE* disabledGizmo = nullptr;
    for (const WorldPartition3DGizmoUVE& gizmo : gizmos) {
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
