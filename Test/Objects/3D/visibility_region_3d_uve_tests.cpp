// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/visibility_region_3d_uve.h"

#include <vector>

#include <gtest/gtest.h>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/fog_volume_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class VisibilityRegion3DUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    EntityUVE PlaceRegionUVE(VisibilityRegion3DComponentUVE config, const TransformComponentUVE& transform = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<VisibilityRegion3DComponentUVE>(entity, config);
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

TEST_F(VisibilityRegion3DUVETest, DrawableKindsCarryMembershipAndLightsDoNot) {
    const EntityUVE mesh = PlaceEmptyUVE();
    MeshComponentUVE meshComponent{};
    meshComponent.visibilityLayers = 0x00000004U;
    entityManager.AddComponentUVE<MeshComponentUVE>(mesh, meshComponent);
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

    EXPECT_TRUE(CarriesVisibilityRegion3DDrawableUVE(entityManager, mesh));
    EXPECT_TRUE(CarriesVisibilityRegion3DDrawableUVE(entityManager, primitive));
    EXPECT_TRUE(CarriesVisibilityRegion3DDrawableUVE(entityManager, decal));
    EXPECT_TRUE(CarriesVisibilityRegion3DDrawableUVE(entityManager, particles));
    EXPECT_TRUE(CarriesVisibilityRegion3DDrawableUVE(entityManager, fog));
    EXPECT_FALSE(CarriesVisibilityRegion3DDrawableUVE(entityManager, light));
    EXPECT_FALSE(CarriesVisibilityRegion3DDrawableUVE(entityManager, empty));

    EXPECT_EQ(ResolveVisibilityRegion3DDrawableLayersUVE(entityManager, mesh), 0x00000004U);
    EXPECT_EQ(ResolveVisibilityRegion3DDrawableLayersUVE(entityManager, primitive),
              kDefaultVisibilityRegionDrawableLayersUVE);
    EXPECT_EQ(ResolveVisibilityRegion3DDrawableLayersUVE(entityManager, empty), 0U);
}

TEST_F(VisibilityRegion3DUVETest, DrawHiddenTrustsLiveFlagAndFailsOpenWhenTheOwnerDies) {
    const EntityUVE region = PlaceRegionUVE(VisibilityRegion3DComponentUVE{});
    const EntityUVE faded = PlaceEmptyUVE();
    entityManager.AddComponentUVE<VisibilityRegion3DMembershipComponentUVE>(
        faded, VisibilityRegion3DMembershipComponentUVE{region, false});
    EXPECT_TRUE(IsVisibilityRegion3DDrawHiddenUVE(entityManager, faded));

    const EntityUVE live = PlaceEmptyUVE();
    entityManager.AddComponentUVE<VisibilityRegion3DMembershipComponentUVE>(
        live, VisibilityRegion3DMembershipComponentUVE{region, true});
    EXPECT_FALSE(IsVisibilityRegion3DDrawHiddenUVE(entityManager, live));
    EXPECT_FALSE(IsVisibilityRegion3DDrawHiddenUVE(entityManager, PlaceEmptyUVE()));

    entityManager.DestroyEntityUVE(region);
    EXPECT_FALSE(IsVisibilityRegion3DDrawHiddenUVE(entityManager, faded));
}

TEST_F(VisibilityRegion3DUVETest, CollectGizmosWritesTheCenteredBoxAndSkipsEditorInternal) {
    VisibilityRegion3DComponentUVE config;
    config.halfExtents = Math::Vector3UVE{2.0F, 3.0F, 4.0F};
    TransformComponentUVE transform{};
    transform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    PlaceRegionUVE(config, transform);

    VisibilityRegion3DComponentUVE disabled = config;
    disabled.enabled = false;
    TransformComponentUVE disabledTransform{};
    disabledTransform.localPosition = Math::Vector3UVE{40.0F, 0.0F, 0.0F};
    PlaceRegionUVE(disabled, disabledTransform);

    const EntityUVE hidden = PlaceRegionUVE(config);
    entityManager.AddComponentUVE<EditorInternalEntityComponentUVE>(hidden);

    std::vector<VisibilityRegion3DGizmoUVE> gizmos;
    CollectVisibilityRegion3DGizmosUVE(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 2U);
    const VisibilityRegion3DGizmoUVE* enabledGizmo = nullptr;
    const VisibilityRegion3DGizmoUVE* disabledGizmo = nullptr;
    for (const VisibilityRegion3DGizmoUVE& gizmo : gizmos) {
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
    EXPECT_FLOAT_EQ(enabledGizmo->halfExtents.x, 2.0F);
    EXPECT_FLOAT_EQ(enabledGizmo->halfExtents.y, 3.0F);
    EXPECT_FLOAT_EQ(enabledGizmo->halfExtents.z, 4.0F);
    EXPECT_NEAR(disabledGizmo->origin.x, 40.0F, 1.0e-4F);
}

} // namespace
} // namespace UVE::Scene::Tests
