// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "visibility_region_3d.h"

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

class VisibilityRegion3DTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    Entity PlaceRegion(VisibilityRegion3DComponent config, const TransformComponent& transform = {}) {
        const Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<VisibilityRegion3DComponent>(entity, config);
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

TEST_F(VisibilityRegion3DTest, DrawableKindsCarryMembershipAndLightsDoNot) {
    const Entity mesh = PlaceEmpty();
    MeshComponent meshComponent{};
    meshComponent.visibilityLayers = 0x00000004U;
    entityManager.AddComponent<MeshComponent>(mesh, meshComponent);
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

    EXPECT_TRUE(CarriesVisibilityRegion3DDrawable(entityManager, mesh));
    EXPECT_TRUE(CarriesVisibilityRegion3DDrawable(entityManager, primitive));
    EXPECT_TRUE(CarriesVisibilityRegion3DDrawable(entityManager, decal));
    EXPECT_TRUE(CarriesVisibilityRegion3DDrawable(entityManager, particles));
    EXPECT_TRUE(CarriesVisibilityRegion3DDrawable(entityManager, fog));
    EXPECT_FALSE(CarriesVisibilityRegion3DDrawable(entityManager, light));
    EXPECT_FALSE(CarriesVisibilityRegion3DDrawable(entityManager, empty));

    EXPECT_EQ(ResolveVisibilityRegion3DDrawableLayers(entityManager, mesh), 0x00000004U);
    EXPECT_EQ(ResolveVisibilityRegion3DDrawableLayers(entityManager, primitive),
              kDefaultVisibilityRegionDrawableLayers);
    EXPECT_EQ(ResolveVisibilityRegion3DDrawableLayers(entityManager, empty), 0U);
}

TEST_F(VisibilityRegion3DTest, DrawHiddenTrustsLiveFlagAndFailsOpenWhenTheOwnerDies) {
    const Entity region = PlaceRegion(VisibilityRegion3DComponent{});
    const Entity faded = PlaceEmpty();
    entityManager.AddComponent<VisibilityRegion3DMembershipComponent>(
        faded, VisibilityRegion3DMembershipComponent{region, false});
    EXPECT_TRUE(IsVisibilityRegion3DDrawHidden(entityManager, faded));

    const Entity live = PlaceEmpty();
    entityManager.AddComponent<VisibilityRegion3DMembershipComponent>(
        live, VisibilityRegion3DMembershipComponent{region, true});
    EXPECT_FALSE(IsVisibilityRegion3DDrawHidden(entityManager, live));
    EXPECT_FALSE(IsVisibilityRegion3DDrawHidden(entityManager, PlaceEmpty()));

    entityManager.DestroyEntity(region);
    EXPECT_FALSE(IsVisibilityRegion3DDrawHidden(entityManager, faded));
}

TEST_F(VisibilityRegion3DTest, CollectGizmosWritesTheCenteredBoxAndSkipsEditorInternal) {
    VisibilityRegion3DComponent config;
    config.halfExtents = Math::Vector3{2.0F, 3.0F, 4.0F};
    TransformComponent transform{};
    transform.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    PlaceRegion(config, transform);

    VisibilityRegion3DComponent disabled = config;
    disabled.enabled = false;
    TransformComponent disabledTransform{};
    disabledTransform.localPosition = Math::Vector3{40.0F, 0.0F, 0.0F};
    PlaceRegion(disabled, disabledTransform);

    const Entity hidden = PlaceRegion(config);
    entityManager.AddComponent<EditorInternalEntityComponent>(hidden);

    std::vector<VisibilityRegion3DGizmo> gizmos;
    CollectVisibilityRegion3DGizmos(entityManager, gizmos);
    ASSERT_EQ(gizmos.size(), 2U);
    const VisibilityRegion3DGizmo* enabledGizmo = nullptr;
    const VisibilityRegion3DGizmo* disabledGizmo = nullptr;
    for (const VisibilityRegion3DGizmo& gizmo : gizmos) {
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
