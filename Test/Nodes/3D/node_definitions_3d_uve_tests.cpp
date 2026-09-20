// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <type_traits>

#include <gtest/gtest.h>

#include "uve/component/camera_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"
#include "uve/scene/nodes/scene_root_uve.h"

namespace UVE::Scene::Tests {
namespace {

// Every one of the 17 component-backed node kinds' definition is reachable from the aggregate
// header, mirroring the registry test's guarantee for the 21 data-carrying node components:
// no node kind's home file can be silently dropped without breaking this compile.
static_assert(std::is_class_v<Node3DNodeDefinitionUVE>);            // Node3D
static_assert(std::is_class_v<Area3DNodeDefinitionUVE>);           // Area3D
static_assert(std::is_class_v<StaticBody3DNodeDefinitionUVE>);     // StaticBody3D
static_assert(std::is_class_v<CharacterBody3DNodeDefinitionUVE>);  // CharacterBody3D
static_assert(std::is_class_v<Camera3DNodeDefinitionUVE>);         // Camera3D
static_assert(std::is_class_v<MeshInstance3DNodeDefinitionUVE>);   // MeshInstance3D
static_assert(std::is_class_v<BoxMesh3DNodeDefinitionUVE>);        // BoxMesh3D
static_assert(std::is_class_v<SphereMesh3DNodeDefinitionUVE>);     // SphereMesh3D
static_assert(std::is_class_v<PlaneMesh3DNodeDefinitionUVE>);      // PlaneMesh3D
static_assert(std::is_class_v<Light3DNodeDefinitionUVE>);          // Light3D
static_assert(std::is_class_v<Collider3DNodeDefinitionUVE>);       // Collider3D
static_assert(std::is_class_v<RigidBody3DNodeDefinitionUVE>);      // RigidBody3D
static_assert(std::is_class_v<AudioSource3DNodeDefinitionUVE>);        // AudioSource3D
static_assert(std::is_class_v<ParticleEmitter3DNodeDefinitionUVE>); // ParticleEmitter3D
static_assert(std::is_class_v<ScriptNodeDefinitionUVE>);           // Script
static_assert(std::is_class_v<AnimationPlayerNodeDefinitionUVE>);  // AnimationPlayer
static_assert(std::is_class_v<AnimationTreeNodeDefinitionUVE>);    // AnimationTree

class Node3DDefinitionsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    [[nodiscard]] EntityUVE CreateEntityUVE() {
        return entityManager.CreateEntityUVE();
    }
};

TEST_F(Node3DDefinitionsUVETest, AllDefinitionDefaultsAreValid) {
    EXPECT_TRUE(IsNode3DNodeDefinitionValidUVE(Node3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsArea3DNodeDefinitionValidUVE(Area3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsStaticBody3DNodeDefinitionValidUVE(StaticBody3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsCharacterBody3DNodeDefinitionValidUVE(CharacterBody3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsCamera3DNodeDefinitionValidUVE(Camera3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsMeshInstance3DNodeDefinitionValidUVE(MeshInstance3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsBoxMesh3DNodeDefinitionValidUVE(BoxMesh3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsSphereMesh3DNodeDefinitionValidUVE(SphereMesh3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsPlaneMesh3DNodeDefinitionValidUVE(PlaneMesh3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsLight3DNodeDefinitionValidUVE(Light3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsCollider3DNodeDefinitionValidUVE(Collider3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsRigidBody3DNodeDefinitionValidUVE(RigidBody3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsAudioSource3DNodeDefinitionValidUVE(AudioSource3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsParticleEmitter3DNodeDefinitionValidUVE(ParticleEmitter3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsScriptNodeDefinitionValidUVE(ScriptNodeDefinitionUVE{}));
    EXPECT_TRUE(IsAnimationPlayerNodeDefinitionValidUVE(AnimationPlayerNodeDefinitionUVE{}));
    EXPECT_TRUE(IsAnimationTreeNodeDefinitionValidUVE(AnimationTreeNodeDefinitionUVE{}));
    EXPECT_TRUE(IsAnimatableBody3DNodeDefinitionValidUVE(AnimatableBody3DNodeDefinitionUVE{}));
}

// The seven names the editor's legacy EditorEntityKindUVE path surfaces are locked at compile
// time: the editor now sources every default name from these definitions, so any drift here
// would silently rename what legacy creation produces.
static_assert(Node3DNodeDefinitionUVE::defaultName == "Node3D");
static_assert(Camera3DNodeDefinitionUVE::defaultName == "Camera");
static_assert(Light3DNodeDefinitionUVE::defaultName == "Directional Light");
static_assert(Collider3DNodeDefinitionUVE::defaultName == "Collision Box");
static_assert(BoxMesh3DNodeDefinitionUVE::defaultName == "Cube");
static_assert(SphereMesh3DNodeDefinitionUVE::defaultName == "UV Sphere");
static_assert(PlaneMesh3DNodeDefinitionUVE::defaultName == "Plane");
static_assert(AnimatableBody3DNodeDefinitionUVE::defaultName == "AnimatableBody3D");

TEST_F(Node3DDefinitionsUVETest, DefaultNamesAreAuthoredPerKindNotGeneric) {
    // The six kinds that previously lived behind legacy EditorEntityKindUVE values keep their
    // exact historical names; the kinds the editor used to name "Empty" now carry their own.
    //
    // The transform-only base is Node3D every way an author can meet it: display name, kind
    // enumerator, and the "node_3d" on-disk id. The legacy "empty" id still resolves to it at
    // load (covered below) so the rename broke no saved file.
    EXPECT_EQ(Node3DNodeDefinitionUVE::defaultName, "Node3D");
    EXPECT_EQ(Camera3DNodeDefinitionUVE::defaultName, "Camera");
    EXPECT_EQ(Light3DNodeDefinitionUVE::defaultName, "Directional Light");
    EXPECT_EQ(Collider3DNodeDefinitionUVE::defaultName, "Collision Box");
    EXPECT_EQ(BoxMesh3DNodeDefinitionUVE::defaultName, "Cube");
    EXPECT_EQ(SphereMesh3DNodeDefinitionUVE::defaultName, "UV Sphere");
    EXPECT_EQ(PlaneMesh3DNodeDefinitionUVE::defaultName, "Plane");
    EXPECT_EQ(Area3DNodeDefinitionUVE::defaultName, "Area3D");
    EXPECT_EQ(StaticBody3DNodeDefinitionUVE::defaultName, "StaticBody3D");
    EXPECT_EQ(CharacterBody3DNodeDefinitionUVE::defaultName, "CharacterBody3D");
    EXPECT_EQ(MeshInstance3DNodeDefinitionUVE::defaultName, "MeshInstance3D");
    EXPECT_EQ(RigidBody3DNodeDefinitionUVE::defaultName, "RigidBody3D");
    EXPECT_EQ(AudioSource3DNodeDefinitionUVE::defaultName, "AudioSource3D");
    EXPECT_EQ(ParticleEmitter3DNodeDefinitionUVE::defaultName, "ParticleEmitter3D");
    EXPECT_EQ(ScriptNodeDefinitionUVE::defaultName, "Script");
    EXPECT_EQ(AnimationPlayerNodeDefinitionUVE::defaultName, "AnimationPlayer");
    EXPECT_EQ(AnimationTreeNodeDefinitionUVE::defaultName, "AnimationTree");
    EXPECT_EQ(AnimatableBody3DNodeDefinitionUVE::defaultName, "AnimatableBody3D");
}

TEST_F(Node3DDefinitionsUVETest, ApplyAttachesEachKindsExactComponentRecipe) {
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyNode3DNodeDefinitionUVE(entityManager, entity, Node3DNodeDefinitionUVE{});
        // Node3D's recipe is the baseline itself, not "nothing": the guarantee is pinned
        // behaviour-for-behaviour in the dedicated tests below.
        EXPECT_TRUE(entityManager.HasComponentUVE<TransformComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<HierarchyComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<NameComponentUVE>(entity));
        EXPECT_FALSE(entityManager.HasComponentUVE<CameraComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyArea3DNodeDefinitionUVE(entityManager, entity, Area3DNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<AreaComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyStaticBody3DNodeDefinitionUVE(entityManager, entity, StaticBody3DNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
        EXPECT_FALSE(entityManager.HasComponentUVE<RigidBodyComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyCamera3DNodeDefinitionUVE(entityManager, entity, Camera3DNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<CameraComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyMeshInstance3DNodeDefinitionUVE(entityManager, entity, MeshInstance3DNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<MeshComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyCollider3DNodeDefinitionUVE(entityManager, entity, Collider3DNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyRigidBody3DNodeDefinitionUVE(entityManager, entity, RigidBody3DNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<RigidBodyComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyAudioSource3DNodeDefinitionUVE(entityManager, entity, AudioSource3DNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<AudioSourceComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyParticleEmitter3DNodeDefinitionUVE(entityManager, entity, ParticleEmitter3DNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<ParticleEmitterComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyScriptNodeDefinitionUVE(entityManager, entity, ScriptNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<ScriptComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyAnimationPlayerNodeDefinitionUVE(entityManager, entity, AnimationPlayerNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<AnimationPlayerComponentUVE>(entity));
    }
}

TEST_F(Node3DDefinitionsUVETest, PrimitiveMeshRecipesKeepTheirDistinctShapesColorsAndColliders) {
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyBoxMesh3DNodeDefinitionUVE(entityManager, entity, BoxMesh3DNodeDefinitionUVE{});
        ASSERT_TRUE(entityManager.HasComponentUVE<PrimitiveMeshComponentUVE>(entity));
        ASSERT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
        const PrimitiveMeshComponentUVE& mesh = entityManager.GetComponentUVE<PrimitiveMeshComponentUVE>(entity);
        EXPECT_EQ(mesh.kind, PrimitiveMeshKindUVE::Cube);
        EXPECT_EQ(mesh.baseColor, (Math::Vector3UVE{0.73F, 0.48F, 0.21F}));
        EXPECT_EQ(entityManager.GetComponentUVE<ColliderComponentUVE>(entity).halfExtents,
                  (Math::Vector3UVE{0.5F, 0.5F, 0.5F}));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplySphereMesh3DNodeDefinitionUVE(entityManager, entity, SphereMesh3DNodeDefinitionUVE{});
        ASSERT_TRUE(entityManager.HasComponentUVE<PrimitiveMeshComponentUVE>(entity));
        ASSERT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
        const PrimitiveMeshComponentUVE& mesh = entityManager.GetComponentUVE<PrimitiveMeshComponentUVE>(entity);
        EXPECT_EQ(mesh.kind, PrimitiveMeshKindUVE::UVSphere);
        EXPECT_EQ(mesh.baseColor, (Math::Vector3UVE{0.22F, 0.55F, 0.88F}));
        EXPECT_EQ(entityManager.GetComponentUVE<ColliderComponentUVE>(entity).halfExtents,
                  (Math::Vector3UVE{0.5F, 0.5F, 0.5F}));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyPlaneMesh3DNodeDefinitionUVE(entityManager, entity, PlaneMesh3DNodeDefinitionUVE{});
        ASSERT_TRUE(entityManager.HasComponentUVE<PrimitiveMeshComponentUVE>(entity));
        ASSERT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
        const PrimitiveMeshComponentUVE& mesh = entityManager.GetComponentUVE<PrimitiveMeshComponentUVE>(entity);
        EXPECT_EQ(mesh.kind, PrimitiveMeshKindUVE::Plane);
        EXPECT_EQ(mesh.baseColor, (Math::Vector3UVE{0.32F, 0.38F, 0.30F}));
        // The plane's collider is a thin floor slab, not a full-height box.
        EXPECT_EQ(entityManager.GetComponentUVE<ColliderComponentUVE>(entity).halfExtents,
                  (Math::Vector3UVE{0.5F, 0.025F, 0.5F}));
    }
}

TEST_F(Node3DDefinitionsUVETest, CharacterBodyRecipeIsKinematicByContract) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyCharacterBody3DNodeDefinitionUVE(entityManager, entity, CharacterBody3DNodeDefinitionUVE{});
    ASSERT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<RigidBodyComponentUVE>(entity));
    EXPECT_TRUE(entityManager.GetComponentUVE<RigidBodyComponentUVE>(entity).isKinematic);

    CharacterBody3DNodeDefinitionUVE nonKinematic{};
    nonKinematic.body.isKinematic = false;
    EXPECT_FALSE(IsCharacterBody3DNodeDefinitionValidUVE(nonKinematic));
}

TEST_F(Node3DDefinitionsUVETest, AnimatableBodyRecipeMatchesTheFormerInlineEditorRecipe) {
    // The last inline multi-component recipe the editor's creation switch used to hardcode:
    // collider + kinematic body + the animatable body's own component, in that spirit unchanged.
    const EntityUVE entity = CreateEntityUVE();
    ApplyAnimatableBody3DNodeDefinitionUVE(entityManager, entity, AnimatableBody3DNodeDefinitionUVE{});
    ASSERT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<RigidBodyComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<AnimatableBody3DNodeComponentUVE>(entity));
    EXPECT_TRUE(entityManager.GetComponentUVE<RigidBodyComponentUVE>(entity).isKinematic);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<AnimatableBody3DNodeComponentUVE>(entity).interpolation, 1.0F);

    AnimatableBody3DNodeDefinitionUVE nonKinematic{};
    nonKinematic.body.isKinematic = false;
    EXPECT_FALSE(IsAnimatableBody3DNodeDefinitionValidUVE(nonKinematic));
}

TEST_F(Node3DDefinitionsUVETest, LightRecipeDefaultsToDirectionalSunLight) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyLight3DNodeDefinitionUVE(entityManager, entity, Light3DNodeDefinitionUVE{});
    ASSERT_TRUE(entityManager.HasComponentUVE<LightComponentUVE>(entity));
    EXPECT_EQ(entityManager.GetComponentUVE<LightComponentUVE>(entity).type, LightTypeUVE::Directional);
}

TEST_F(Node3DDefinitionsUVETest, Node3DIsReachableUnderBothItsNewAndLegacyTypeIds) {
    const Nodes::SceneNodeDescriptorUVE* descriptor =
        Nodes::FindSceneNodeDescriptorUVE(Nodes::SceneNodeKindUVE::Node3D);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->typeId, "node_3d");
    EXPECT_EQ(descriptor->displayName, "Node3D");
    EXPECT_TRUE(descriptor->libraryCreatable);

    // New writes use the canonical id; the legacy id from before the rename must resolve to the
    // very same node, since saved documents and layouts carrying "empty" have no way to upgrade
    // themselves.
    EXPECT_EQ(Nodes::FindSceneNodeDescriptorUVE("node_3d"), descriptor);
    EXPECT_EQ(Nodes::FindSceneNodeDescriptorUVE("empty"), descriptor);
    EXPECT_EQ(Nodes::GetSceneNodeTypeIdUVE(Nodes::SceneNodeKindUVE::Node3D), "node_3d");
}

TEST_F(Node3DDefinitionsUVETest, Node3DApplyAttachesALocalTransformToABareEntity) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyNode3DNodeDefinitionUVE(entityManager, entity, Node3DNodeDefinitionUVE{});

    // The meaningful part of the guarantee: a Node3D reached without the creation shell still
    // ends up with the transform every SceneNode3D needs, at identity and named after its kind.
    const TransformComponentUVE& local = entityManager.GetComponentUVE<TransformComponentUVE>(entity);
    EXPECT_EQ(local.localPosition.x, 0.0F);
    EXPECT_EQ(local.localPosition.y, 0.0F);
    EXPECT_EQ(local.localPosition.z, 0.0F);
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(entity).name,
              Node3DNodeDefinitionUVE::defaultName);
    EXPECT_EQ(entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent, kInvalidEntityUVE);
}

TEST_F(Node3DDefinitionsUVETest, Node3DApplyPreservesAuthoredValuesAndRepairsOnlyWhatIsMissing) {
    const EntityUVE entity = CreateEntityUVE();
    // A partially-baselined entity, as a partial deserialization leaves behind: transform is
    // present and authored, the rest of the baseline is not.
    TransformComponentUVE authored;
    authored.localPosition = Math::Vector3UVE{3.0F, -2.0F, 7.5F};
    entityManager.AddComponentUVE<TransformComponentUVE>(entity, authored);
    entityManager.AddComponentUVE<NameComponentUVE>(entity, NameComponentUVE{"AuthoredPivot"});

    ApplyNode3DNodeDefinitionUVE(entityManager, entity, Node3DNodeDefinitionUVE{});

    // Authored state is sacred: position and name survive application untouched, and the missing
    // half of the baseline is what got repaired.
    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition.x, 3.0F);
    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition.z, 7.5F);
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(entity).name, "AuthoredPivot");
    EXPECT_TRUE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<HierarchyComponentUVE>(entity));
}

TEST_F(Node3DDefinitionsUVETest, Node3DApplyIsIdempotent) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyNode3DNodeDefinitionUVE(entityManager, entity, Node3DNodeDefinitionUVE{});
    entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition =
        Math::Vector3UVE{1.0F, 2.0F, 3.0F};

    ApplyNode3DNodeDefinitionUVE(entityManager, entity, Node3DNodeDefinitionUVE{});
    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition.y, 2.0F);
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(entity).name,
              Node3DNodeDefinitionUVE::defaultName);
}

TEST_F(Node3DDefinitionsUVETest, Node3DApplyRefusesADestroyedEntity) {
    const EntityUVE entity = CreateEntityUVE();
    entityManager.DestroyEntityUVE(entity);
    // Same refusal as the scene-root apply: dead entities get nothing, and nothing crashes.
    ApplyNode3DNodeDefinitionUVE(entityManager, entity, Node3DNodeDefinitionUVE{});
    EXPECT_FALSE(entityManager.IsAliveUVE(entity));
}

TEST_F(Node3DDefinitionsUVETest, SceneRootStandsOnTheSameBaselineAsEveryNode3D) {
    const EntityUVE root = CreateEntityUVE();
    ApplySceneRootNodeDefinitionUVE(entityManager, root, SceneRootNodeDefinitionUVE{});

    // The root is "Node3D plus the marker" the same way the other kinds are "Node3D plus their
    // component": one shared baseline guarantee, one extra component on top.
    EXPECT_TRUE(entityManager.HasComponentUVE<TransformComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<HierarchyComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<NameComponentUVE>(root));
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(root).name,
              SceneRootNodeDefinitionUVE::defaultName);
    EXPECT_TRUE(entityManager.HasComponentUVE<SceneRootComponentUVE>(root));

    // And it is still the idempotent recipe the document lifecycle relies on: applying twice
    // adds nothing and changes nothing.
    ApplySceneRootNodeDefinitionUVE(entityManager, root, SceneRootNodeDefinitionUVE{});
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(root).name,
              SceneRootNodeDefinitionUVE::defaultName);
}

TEST_F(Node3DDefinitionsUVETest, AnimationTreeStaysHonestlyNonCreatable) {
    // An empty graph is a valid placeholder definition, but the registry keeps telling the
    // truth: AnimationTree is not library-creatable until the animation pipeline exists, and
    // the definition deliberately provides no Apply function to call even if someone tried.
    EXPECT_TRUE(IsAnimationTreeNodeDefinitionValidUVE(AnimationTreeNodeDefinitionUVE{}));
    const Nodes::SceneNodeDescriptorUVE* descriptor =
        Nodes::FindSceneNodeDescriptorUVE(Nodes::SceneNodeKindUVE::AnimationTree);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_FALSE(descriptor->libraryCreatable);
}

} // namespace
TEST(NodeDefinitions3DUVETest, PrimitiveCollidersMatchTheKindTheyBelongTo) {
    // The editor converts a primitive in place (Cube -> Plane and so on) and refreshes the
    // collider to match the new kind. It reads these same definitions to do it, so this pins the
    // pairing the conversion depends on: each primitive kind's authored collider must be the one
    // its own definition declares, and a flat primitive must not inherit a cube's depth.
    EXPECT_EQ(BoxMesh3DNodeDefinitionUVE{}.mesh.kind, PrimitiveMeshKindUVE::Cube);
    EXPECT_EQ(SphereMesh3DNodeDefinitionUVE{}.mesh.kind, PrimitiveMeshKindUVE::UVSphere);
    EXPECT_EQ(PlaneMesh3DNodeDefinitionUVE{}.mesh.kind, PrimitiveMeshKindUVE::Plane);

    // The plane is the one that actually differs, and the one a duplicated constant would get
    // wrong: it is thin on Y where the volumetric primitives are not.
    const Math::Vector3UVE planeExtents = PlaneMesh3DNodeDefinitionUVE{}.collider.halfExtents;
    const Math::Vector3UVE boxExtents = BoxMesh3DNodeDefinitionUVE{}.collider.halfExtents;
    EXPECT_LT(planeExtents.y, boxExtents.y) << "a plane's collider must be flatter than a cube's";
    EXPECT_FLOAT_EQ(planeExtents.x, boxExtents.x);
    EXPECT_FLOAT_EQ(planeExtents.z, boxExtents.z);

    // Every primitive definition must carry a collider that passes validation - an unauthored or
    // zeroed one would make the created entity fail IsColliderComponentValidUVE downstream.
    EXPECT_TRUE(IsBoxMesh3DNodeDefinitionValidUVE(BoxMesh3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsSphereMesh3DNodeDefinitionValidUVE(SphereMesh3DNodeDefinitionUVE{}));
    EXPECT_TRUE(IsPlaneMesh3DNodeDefinitionValidUVE(PlaneMesh3DNodeDefinitionUVE{}));
}

} // namespace UVE::Scene::Tests
