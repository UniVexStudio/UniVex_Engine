// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/physics_object_component_uve.h"
#include "uve/component/render_instance_component_uve.h"
#include "uve/component/light_emitter_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/solid_body_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/node_metadata_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"
#include "uve/scene/nodes/scene_root_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

// Every one of the 18 component-backed node kinds' definition is reachable from the aggregate
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
static_assert(std::is_class_v<SpringArm3DNodeDefinitionUVE>);      // SpringArm3D
static_assert(std::is_class_v<AnimationPlayerNodeDefinitionUVE>);  // AnimationPlayer
static_assert(std::is_class_v<AnimationTreeNodeDefinitionUVE>);    // AnimationTree

class Node3DDefinitionsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    [[nodiscard]] EntityUVE CreateEntityUVE() {
        return entityManager.CreateEntityUVE();
    }
};

// Every 3D scene-node recipe stands on the Node3D baseline; asserting it once per kind (instead
// of re-typing four component checks in every block) pins the composition, not just its effect.
void ExpectNode3DBaselineUVE(EntityManagerUVE& entityManager, const EntityUVE entity,
                             const std::string_view expectedName) {
    EXPECT_TRUE(entityManager.HasComponentUVE<TransformComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<HierarchyComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<NameComponentUVE>(entity));
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(entity).name, expectedName);
}

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
    EXPECT_TRUE(IsSpringArm3DNodeDefinitionValidUVE(SpringArm3DNodeDefinitionUVE{}));
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
static_assert(SpringArm3DNodeDefinitionUVE::defaultName == "SpringArm3D");

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
        ExpectNode3DBaselineUVE(entityManager, entity, Node3DNodeDefinitionUVE::defaultName);
        EXPECT_FALSE(entityManager.HasComponentUVE<CameraComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyArea3DNodeDefinitionUVE(entityManager, entity, Area3DNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, Area3DNodeDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<AreaComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyStaticBody3DNodeDefinitionUVE(entityManager, entity, StaticBody3DNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, StaticBody3DNodeDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
        EXPECT_FALSE(entityManager.HasComponentUVE<RigidBodyComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyCamera3DNodeDefinitionUVE(entityManager, entity, Camera3DNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, Camera3DNodeDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<CameraComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyMeshInstance3DNodeDefinitionUVE(entityManager, entity, MeshInstance3DNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, MeshInstance3DNodeDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<MeshComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyCollider3DNodeDefinitionUVE(entityManager, entity, Collider3DNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, Collider3DNodeDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyRigidBody3DNodeDefinitionUVE(entityManager, entity, RigidBody3DNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, RigidBody3DNodeDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<RigidBodyComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyAudioSource3DNodeDefinitionUVE(entityManager, entity, AudioSource3DNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, AudioSource3DNodeDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<AudioSourceComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyParticleEmitter3DNodeDefinitionUVE(entityManager, entity, ParticleEmitter3DNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, ParticleEmitter3DNodeDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<ParticleEmitterComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyScriptNodeDefinitionUVE(entityManager, entity, ScriptNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, ScriptNodeDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<ScriptComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyAnimationPlayerNodeDefinitionUVE(entityManager, entity, AnimationPlayerNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, AnimationPlayerNodeDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<AnimationPlayerComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplySpringArm3DNodeDefinitionUVE(entityManager, entity, SpringArm3DNodeDefinitionUVE{});
        ExpectNode3DBaselineUVE(entityManager, entity, SpringArm3DNodeDefinitionUVE::defaultName);
        ASSERT_TRUE(entityManager.HasComponentUVE<SpringArm3DNodeComponentUVE>(entity));
        // The recipe seeds the runtime state exactly the way the deserializer does: an arm that
        // has never been simulated reads as fully extended, valid before the first step.
        const SpringArm3DNodeComponentUVE& arm =
            entityManager.GetComponentUVE<SpringArm3DNodeComponentUVE>(entity);
        EXPECT_EQ(arm.currentLength, arm.armLength);
        EXPECT_TRUE(IsSpringArm3DNodeComponentValidUVE(arm));
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

TEST_F(Node3DDefinitionsUVETest, CharacterBodyIsItsChainPlusAReadyToWalkCapsule) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyCharacterBody3DNodeDefinitionUVE(entityManager, entity, CharacterBody3DNodeDefinitionUVE{});
    // Node3D > PhysicsObject3D > SolidBody3D > CharacterBody3D, and nothing from another branch.
    ExpectNode3DBaselineUVE(entityManager, entity, CharacterBody3DNodeDefinitionUVE::defaultName);
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsObjectComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<SolidBodyComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<CharacterControllerComponentUVE>(entity));
    EXPECT_FALSE(entityManager.HasComponentUVE<RigidBodyComponentUVE>(entity));
    EXPECT_FALSE(entityManager.HasComponentUVE<RenderInstanceComponentUVE>(entity));
    // A person-sized capsule, so it walks the moment Play starts.
    ASSERT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
    const ColliderComponentUVE& shape = entityManager.GetComponentUVE<ColliderComponentUVE>(entity);
    EXPECT_EQ(shape.shapeType, ColliderShapeTypeUVE::Capsule);
    EXPECT_FLOAT_EQ(shape.height, 1.8F);
    EXPECT_FLOAT_EQ(shape.radius, 0.4F);
    EXPECT_TRUE(entityManager.GetComponentUVE<CharacterControllerComponentUVE>(entity).builtInMovement);

    // Applying again keeps what was authored.
    entityManager.GetComponentUVE<CharacterControllerComponentUVE>(entity).moveSpeed = 9.0F;
    ApplyCharacterBody3DNodeDefinitionUVE(entityManager, entity, CharacterBody3DNodeDefinitionUVE{});
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<CharacterControllerComponentUVE>(entity).moveSpeed, 9.0F);
}

TEST_F(Node3DDefinitionsUVETest, CharacterBodyRefusesSettingsItCannotRunWith) {
    EXPECT_TRUE(IsCharacterBody3DNodeDefinitionValidUVE(CharacterBody3DNodeDefinitionUVE{}));
    const auto invalid = [](auto change) {
        CharacterBody3DNodeDefinitionUVE definition{};
        change(definition.controller);
        return !IsCharacterBody3DNodeDefinitionValidUVE(definition);
    };
    EXPECT_TRUE(invalid([](CharacterControllerComponentUVE& c) { c.airControl = 1.5F; }));
    EXPECT_TRUE(invalid([](CharacterControllerComponentUVE& c) { c.moveSpeed = -1.0F; }));
    EXPECT_TRUE(invalid([](CharacterControllerComponentUVE& c) { c.maxSlides = 0U; }));
    EXPECT_TRUE(invalid([](CharacterControllerComponentUVE& c) { c.maxSlides = 33U; }));
    EXPECT_TRUE(invalid([](CharacterControllerComponentUVE& c) { c.coyoteTimeSeconds = -0.1F; }));
    EXPECT_TRUE(invalid([](CharacterControllerComponentUVE& c) { c.motionMode = static_cast<CharacterMotionModeUVE>(7); }));
    EXPECT_TRUE(invalid([](CharacterControllerComponentUVE& c) {
        c.velocity = Math::Vector3UVE{std::numeric_limits<float>::infinity(), 0.0F, 0.0F};
    }));
}

TEST_F(Node3DDefinitionsUVETest, SolidBodyBaseSitsOnPhysicsObject) {
    const EntityUVE entity = CreateEntityUVE();
    ApplySolidBody3DBaseUVE(entityManager, entity, "StaticBody3D");
    ExpectNode3DBaselineUVE(entityManager, entity, "StaticBody3D");
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsObjectComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<SolidBodyComponentUVE>(entity));
    entityManager.GetComponentUVE<SolidBodyComponentUVE>(entity).lockMotionZ = true;
    ApplySolidBody3DBaseUVE(entityManager, entity, "StaticBody3D");
    EXPECT_TRUE(entityManager.GetComponentUVE<SolidBodyComponentUVE>(entity).lockMotionZ);
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

TEST_F(Node3DDefinitionsUVETest, Node3DCarriesVisibilityAndTheCommonNodeSection) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyNode3DNodeDefinitionUVE(entityManager, entity, Node3DNodeDefinitionUVE{});
    // Its Inspector has no Add Component, so every section it shows is attached by the recipe.
    EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<ProcessComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<ThreadGroupComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsInterpolationComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<AutoTranslateComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<EditorDescriptionComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<ScriptComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<NodeMetadataComponentUVE>(entity));
}

TEST_F(Node3DDefinitionsUVETest, AbstractBasesAreNode3DPlusTheirOwnComponent) {
    const EntityUVE bone = CreateEntityUVE();
    const EntityUVE physics = CreateEntityUVE();
    const EntityUVE render = CreateEntityUVE();
    ApplyBoneModifier3DBaseUVE(entityManager, bone, "LookAtModifier3D");
    ApplyPhysicsObject3DBaseUVE(entityManager, physics, "Area3D");
    ApplyRenderInstance3DBaseUVE(entityManager, render, "MeshInstance3D");
    ExpectNode3DBaselineUVE(entityManager, bone, "LookAtModifier3D");
    ExpectNode3DBaselineUVE(entityManager, physics, "Area3D");
    ExpectNode3DBaselineUVE(entityManager, render, "MeshInstance3D");
    for (const EntityUVE entity : {bone, physics, render}) {
        EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<ProcessComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<NodeMetadataComponentUVE>(entity));
    }
    EXPECT_TRUE(entityManager.HasComponentUVE<BoneModifierComponentUVE>(bone));
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsObjectComponentUVE>(physics));
    EXPECT_TRUE(entityManager.HasComponentUVE<RenderInstanceComponentUVE>(render));
    // The child's name, never the abstract base's.
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(physics).name, "Area3D");
    // Authored values survive a second apply.
    entityManager.GetComponentUVE<BoneModifierComponentUVE>(bone).influence = 0.5F;
    ApplyBoneModifier3DBaseUVE(entityManager, bone, "LookAtModifier3D");
    EXPECT_EQ(entityManager.GetComponentUVE<BoneModifierComponentUVE>(bone).influence, 0.5F);
}

TEST_F(Node3DDefinitionsUVETest, AbstractBaseComponentsRejectValuesTheySaveBadly) {
    EXPECT_TRUE(IsBoneModifierComponentValidUVE(BoneModifierComponentUVE{}));
    EXPECT_FALSE(IsBoneModifierComponentValidUVE(BoneModifierComponentUVE{true, 1.5F}));
    EXPECT_FALSE(IsBoneModifierComponentValidUVE(BoneModifierComponentUVE{true, std::numeric_limits<float>::quiet_NaN()}));
    PhysicsObjectComponentUVE object{};
    EXPECT_TRUE(IsPhysicsObjectComponentValidUVE(object));
    object.collisionPriority = -1.0F;
    EXPECT_FALSE(IsPhysicsObjectComponentValidUVE(object));
    object.collisionPriority = 1.0F;
    object.disableMode = static_cast<PhysicsObjectDisableModeUVE>(9);
    EXPECT_FALSE(IsPhysicsObjectComponentValidUVE(object));
    EXPECT_FALSE(IsRenderInstanceComponentValidUVE(
        RenderInstanceComponentUVE{1U, std::numeric_limits<float>::infinity(), true}));
}

TEST_F(Node3DDefinitionsUVETest, RenderInstanceChildBasesCarryRenderInstanceAndTheirOwnComponent) {
    const EntityUVE surface = CreateEntityUVE();
    const EntityUVE light = CreateEntityUVE();
    ApplySurfaceInstance3DBaseUVE(entityManager, surface, "MeshInstance3D");
    ApplyLightEmitter3DBaseUVE(entityManager, light, "OmniLight3D");
    ExpectNode3DBaselineUVE(entityManager, surface, "MeshInstance3D");
    ExpectNode3DBaselineUVE(entityManager, light, "OmniLight3D");
    for (const EntityUVE entity : {surface, light}) {
        EXPECT_TRUE(entityManager.HasComponentUVE<RenderInstanceComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<NodeMetadataComponentUVE>(entity));
    }
    EXPECT_TRUE(entityManager.HasComponentUVE<SurfaceInstanceComponentUVE>(surface));
    EXPECT_FALSE(entityManager.HasComponentUVE<LightEmitterComponentUVE>(surface));
    EXPECT_TRUE(entityManager.HasComponentUVE<LightEmitterComponentUVE>(light));
    EXPECT_FALSE(entityManager.HasComponentUVE<SurfaceInstanceComponentUVE>(light));
}

TEST_F(Node3DDefinitionsUVETest, MeshAndParticleNodesAreSurfaceInstances) {
    const EntityUVE mesh = CreateEntityUVE();
    const EntityUVE box = CreateEntityUVE();
    const EntityUVE particles = CreateEntityUVE();
    ApplyMeshInstance3DNodeDefinitionUVE(entityManager, mesh, MeshInstance3DNodeDefinitionUVE{});
    ApplyBoxMesh3DNodeDefinitionUVE(entityManager, box, BoxMesh3DNodeDefinitionUVE{});
    ApplyParticleEmitter3DNodeDefinitionUVE(entityManager, particles, ParticleEmitter3DNodeDefinitionUVE{});
    ExpectNode3DBaselineUVE(entityManager, mesh, "MeshInstance3D");
    ExpectNode3DBaselineUVE(entityManager, box, "Cube");
    ExpectNode3DBaselineUVE(entityManager, particles, "ParticleEmitter3D");
    for (const EntityUVE entity : {mesh, box, particles}) {
        EXPECT_TRUE(entityManager.HasComponentUVE<SurfaceInstanceComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<RenderInstanceComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<NodeMetadataComponentUVE>(entity));
    }
}

TEST_F(Node3DDefinitionsUVETest, Decal3DAndFogVolume3DAreRenderInstancesPlusTheirOwnComponent) {
    const EntityUVE decal = CreateEntityUVE();
    const EntityUVE fog = CreateEntityUVE();
    Decal3DNodeDefinitionUVE decalDefinition{};
    decalDefinition.decal.albedoMix = 0.25F;
    ApplyDecal3DNodeDefinitionUVE(entityManager, decal, decalDefinition);
    ApplyFogVolume3DNodeDefinitionUVE(entityManager, fog, FogVolume3DNodeDefinitionUVE{});
    ExpectNode3DBaselineUVE(entityManager, decal, "Decal3D");
    ExpectNode3DBaselineUVE(entityManager, fog, "FogVolume3D");
    for (const EntityUVE entity : {decal, fog}) {
        EXPECT_TRUE(entityManager.HasComponentUVE<RenderInstanceComponentUVE>(entity));
        EXPECT_FALSE(entityManager.HasComponentUVE<SurfaceInstanceComponentUVE>(entity));
        EXPECT_FALSE(entityManager.HasComponentUVE<LightEmitterComponentUVE>(entity));
    }
    EXPECT_EQ(entityManager.GetComponentUVE<Decal3DNodeComponentUVE>(decal).albedoMix, 0.25F);
    EXPECT_EQ(entityManager.GetComponentUVE<FogVolume3DNodeComponentUVE>(fog), FogVolume3DNodeComponentUVE{});
    // A second apply keeps what was authored.
    entityManager.GetComponentUVE<FogVolume3DNodeComponentUVE>(fog).density = -0.5F;
    ApplyFogVolume3DNodeDefinitionUVE(entityManager, fog, FogVolume3DNodeDefinitionUVE{});
    EXPECT_EQ(entityManager.GetComponentUVE<FogVolume3DNodeComponentUVE>(fog).density, -0.5F);
}

TEST_F(Node3DDefinitionsUVETest, RenderInstanceFamilyComponentsRejectValuesTheySaveBadly) {
    EXPECT_TRUE(IsSurfaceInstanceComponentValidUVE(SurfaceInstanceComponentUVE{}));
    SurfaceInstanceComponentUVE surface{};
    surface.transparency = 1.5F;
    EXPECT_FALSE(IsSurfaceInstanceComponentValidUVE(surface));
    surface = {};
    surface.lodBias = 0.0F;
    EXPECT_FALSE(IsSurfaceInstanceComponentValidUVE(surface));
    surface = {};
    surface.visibilityRangeBegin = 10.0F;
    surface.visibilityRangeEnd = 5.0F;
    EXPECT_FALSE(IsSurfaceInstanceComponentValidUVE(surface));

    EXPECT_TRUE(IsLightEmitterComponentValidUVE(LightEmitterComponentUVE{}));
    LightEmitterComponentUVE light{};
    light.shadowOpacity = 2.0F;
    EXPECT_FALSE(IsLightEmitterComponentValidUVE(light));
    light = {};
    light.energy = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsLightEmitterComponentValidUVE(light));

    EXPECT_TRUE(IsDecal3DNodeComponentValidUVE(Decal3DNodeComponentUVE{}));
    Decal3DNodeComponentUVE decal{};
    decal.albedoMix = -0.1F;
    EXPECT_FALSE(IsDecal3DNodeComponentValidUVE(decal));

    EXPECT_TRUE(IsFogVolume3DNodeComponentValidUVE(FogVolume3DNodeComponentUVE{}));
    FogVolume3DNodeComponentUVE fog{};
    fog.density = -2.0F; // Negative density is allowed: it clears fog.
    EXPECT_TRUE(IsFogVolume3DNodeComponentValidUVE(fog));
    fog.size.y = 0.0F;
    EXPECT_FALSE(IsFogVolume3DNodeComponentValidUVE(fog));
    fog = {};
    fog.shape = static_cast<FogVolumeShapeUVE>(9);
    EXPECT_FALSE(IsFogVolume3DNodeComponentValidUVE(fog));
}

TEST_F(Node3DDefinitionsUVETest, Skeleton3DIsANode3DChildThatStartsWithNoBones) {
    const EntityUVE skeleton = CreateEntityUVE();
    ApplySkeleton3DNodeDefinitionUVE(entityManager, skeleton, Skeleton3DNodeDefinitionUVE{});
    ExpectNode3DBaselineUVE(entityManager, skeleton, "Skeleton3D");
    EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(skeleton));
    EXPECT_TRUE(entityManager.HasComponentUVE<NodeMetadataComponentUVE>(skeleton));
    EXPECT_TRUE(entityManager.GetComponentUVE<Skeleton3DNodeComponentUVE>(skeleton).bones.empty());
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

TEST_F(Node3DDefinitionsUVETest, SceneRootIsAPureNodeCarryingTheCommonNodeSection) {
    const EntityUVE root = CreateEntityUVE();
    ApplySceneRootNodeDefinitionUVE(entityManager, root, SceneRootNodeDefinitionUVE{});

    // In the hierarchy and named, but with no transform: placing things in space is what Node3D
    // adds, and the root has nothing to place. Its children start their own transform chains.
    EXPECT_FALSE(entityManager.HasComponentUVE<TransformComponentUVE>(root));
    EXPECT_FALSE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<HierarchyComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<NameComponentUVE>(root));
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(root).name,
              SceneRootNodeDefinitionUVE::defaultName);
    EXPECT_TRUE(entityManager.HasComponentUVE<SceneRootComponentUVE>(root));

    // The root's Inspector offers no Add Component, so the whole common Node section is attached
    // here - anything left out could never be reached from the editor.
    EXPECT_TRUE(entityManager.HasComponentUVE<ProcessComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<ThreadGroupComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsInterpolationComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<AutoTranslateComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<EditorDescriptionComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<ScriptComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<NodeMetadataComponentUVE>(root));

    // And it is still the idempotent recipe the document lifecycle relies on: applying twice
    // adds nothing and changes nothing, including an authored value.
    entityManager.GetComponentUVE<ProcessComponentUVE>(root).priority = 7;
    ApplySceneRootNodeDefinitionUVE(entityManager, root, SceneRootNodeDefinitionUVE{});
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(root).name,
              SceneRootNodeDefinitionUVE::defaultName);
    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(root).priority, 7);
    EXPECT_FALSE(entityManager.HasComponentUVE<TransformComponentUVE>(root));
}

TEST_F(Node3DDefinitionsUVETest, SceneRootMigrationBakesAnOldRootTransformIntoItsChildren) {
    // A scene saved when the root still had a transform: the root moved, rotated and scaled, and
    // every child's world pose was composed through it. Dropping the transform must move nothing.
    const EntityUVE root = CreateEntityUVE();
    TransformComponentUVE rootTransform{};
    rootTransform.localPosition = Math::Vector3UVE{10.0F, -2.0F, 4.0F};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.2F, rootTransform.localRotation));
    rootTransform.localScale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    sceneGraph.AttachTransformUVE(entityManager, root, rootTransform);

    const EntityUVE child = CreateEntityUVE();
    TransformComponentUVE childTransform{};
    childTransform.localPosition = Math::Vector3UVE{1.0F, 0.5F, -3.0F};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 0.4F, childTransform.localRotation));
    childTransform.localScale = Math::Vector3UVE{0.5F, 1.0F, 1.5F};
    sceneGraph.AttachTransformUVE(entityManager, child, childTransform);
    sceneGraph.SetParentUVE(entityManager, child, root);

    // A top-level child never composed from the root, so the migration must not touch it.
    const EntityUVE topLevelChild = CreateEntityUVE();
    TransformComponentUVE topLevelTransform{};
    topLevelTransform.localPosition = Math::Vector3UVE{-5.0F, 0.0F, 0.0F};
    topLevelTransform.topLevel = true;
    sceneGraph.AttachTransformUVE(entityManager, topLevelChild, topLevelTransform);
    sceneGraph.SetParentUVE(entityManager, topLevelChild, root);

    sceneGraph.UpdateUVE(entityManager);
    const WorldTransformComponentUVE childBefore = entityManager.GetComponentUVE<WorldTransformComponentUVE>(child);
    const WorldTransformComponentUVE topLevelBefore =
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(topLevelChild);

    ApplySceneRootNodeDefinitionUVE(entityManager, root, SceneRootNodeDefinitionUVE{});
    ASSERT_FALSE(entityManager.HasComponentUVE<TransformComponentUVE>(root));
    sceneGraph.UpdateUVE(entityManager);

    const WorldTransformComponentUVE& childAfter = entityManager.GetComponentUVE<WorldTransformComponentUVE>(child);
    constexpr float kTolerance = 1.0e-5F;
    EXPECT_NEAR(childAfter.worldPosition.x, childBefore.worldPosition.x, kTolerance);
    EXPECT_NEAR(childAfter.worldPosition.y, childBefore.worldPosition.y, kTolerance);
    EXPECT_NEAR(childAfter.worldPosition.z, childBefore.worldPosition.z, kTolerance);
    EXPECT_NEAR(childAfter.worldScale.x, childBefore.worldScale.x, kTolerance);
    EXPECT_NEAR(childAfter.worldScale.y, childBefore.worldScale.y, kTolerance);
    EXPECT_NEAR(childAfter.worldScale.z, childBefore.worldScale.z, kTolerance);
    // q and -q are the same rotation, so compare through the absolute dot product.
    const float dot = childAfter.worldRotation.x * childBefore.worldRotation.x +
                      childAfter.worldRotation.y * childBefore.worldRotation.y +
                      childAfter.worldRotation.z * childBefore.worldRotation.z +
                      childAfter.worldRotation.w * childBefore.worldRotation.w;
    EXPECT_NEAR(std::abs(dot), 1.0F, kTolerance);
    // The baked rotation is now the truth; the stale Euler cache must not replay over it.
    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(child).rotationEditMode,
              RotationEditModeUVE::Quaternion);

    const WorldTransformComponentUVE& topLevelAfter =
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(topLevelChild);
    EXPECT_EQ(topLevelAfter.worldPosition, topLevelBefore.worldPosition);
    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(topLevelChild).localPosition,
              topLevelTransform.localPosition);

    // Idempotent: a second apply finds no transform and bakes nothing twice.
    ApplySceneRootNodeDefinitionUVE(entityManager, root, SceneRootNodeDefinitionUVE{});
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_NEAR(entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).worldPosition.x,
                childBefore.worldPosition.x, kTolerance);
}

TEST_F(Node3DDefinitionsUVETest, SpringArm3DIsRegisteredCreatableAsACameraNode) {
    // The registry row and the editor switch must keep agreeing about this kind: the registry
    // advertises it as a creatable camera node, and the switch now creates it from the same
    // definition this test file pins.
    const Nodes::SceneNodeDescriptorUVE* descriptor =
        Nodes::FindSceneNodeDescriptorUVE(Nodes::SceneNodeKindUVE::SpringArm3D);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->typeId, "spring_arm_3d");
    EXPECT_EQ(descriptor->displayName, "SpringArm3D");
    EXPECT_TRUE(descriptor->libraryCreatable);
}

TEST_F(Node3DDefinitionsUVETest, SpringArmTargetResolutionMatchesTheAuthoredEnvelope) {
    // Unobstructed: full reach.
    EXPECT_EQ(ResolveSpringArm3DTargetUVE(std::nullopt, 0.1F, 4.0F), 4.0F);
    // Hit reported by the raycast: distance minus margin.
    EXPECT_NEAR(ResolveSpringArm3DTargetUVE(1.5F, 0.1F, 4.0F), 1.4F, 1.0e-6F);
    // Hit closer than the margin itself: never negative.
    EXPECT_EQ(ResolveSpringArm3DTargetUVE(0.05F, 0.1F, 4.0F), 0.0F);
    // A hit report inconsistent with the arm's own envelope degrades to "unobstructed" instead
    // of stretching the arm past its authored length.
    EXPECT_EQ(ResolveSpringArm3DTargetUVE(9.0F, 0.1F, 4.0F), 4.0F);
    EXPECT_EQ(ResolveSpringArm3DTargetUVE(std::numeric_limits<float>::quiet_NaN(), 0.1F, 4.0F), 4.0F);
    // And absurd authored envelopes refuse politely the same way.
    EXPECT_EQ(ResolveSpringArm3DTargetUVE(1.0F, -1.0F, 4.0F), 4.0F);
    EXPECT_EQ(ResolveSpringArm3DTargetUVE(1.0F, 0.1F, 0.0F), 0.0F);
}

TEST_F(Node3DDefinitionsUVETest, SpringArmRetractionSnapsSoTheCameraNeverClips) {
    // Obstruction appears mid-frame: the arm arrives at the target THIS step, not after a
    // smooth glide through the wall.
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(4.0F, 1.4F, 8.0F, 1.0F / 60.0F), 1.4F);
    // Already retracted, target deeper still: still snaps.
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(2.0F, 1.4F, 8.0F, 1.0F / 60.0F), 1.4F);
}

TEST_F(Node3DDefinitionsUVETest, SpringArmExtensionBlendsMonotonicallyAndNeverOvershoots) {
    float current = 1.4F;
    constexpr float kDt = 1.0F / 60.0F;
    for (int step = 0; step < 240; ++step) {
        const float before = current;
        current = ResolveSpringArm3DLengthUVE(current, 4.0F, 8.0F, kDt);
        EXPECT_LE(current, 4.0F) << "extension must never overshoot the target";
        if (before == 4.0F) {
            // Once the completion tolerance has settled the arm it must hold exactly - a law
            // that kept creeping at the target is a slow camera bleed, not smoothing.
            EXPECT_EQ(current, 4.0F);
        } else {
            EXPECT_GT(current, before) << "still short of target: extension must keep moving";
        }
    }
    // 240 steps at 8/s is ~32 time constants: settled long ago, via the completion tolerance,
    // so the settled read is exact rather than "close enough".
    EXPECT_EQ(current, 4.0F);
}

TEST_F(Node3DDefinitionsUVETest, SpringArmSmoothingZeroMatchesGodotSnapBothWays) {
    // The authored escape hatch: smoothing 0 reproduces Godot's SpringArm3D behaviour exactly
    // (Godot ships no smoothing member at all - snap on the way out as well).
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(1.4F, 4.0F, 0.0F, 1.0F / 60.0F), 4.0F);
}

TEST_F(Node3DDefinitionsUVETest, SpringArmLengthRefusesDegenerateCallsWithoutMoving) {
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(2.0F, 4.0F, 8.0F, 0.0F), 2.0F);   // dt 0: frozen
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(2.0F, 4.0F, 8.0F, -1.0F), 2.0F);  // dt negative: frozen
    // A NaN input stays the caller's (the validator's) problem: the law returns its own current
    // length unchanged rather than smearing garbage through the blend. NaN never compares equal,
    // so these check the value category directly, not EXPECT_EQ.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(std::isnan(ResolveSpringArm3DLengthUVE(nan, 4.0F, 8.0F, 1.0F / 60.0F)));
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(2.0F, nan, 8.0F, 1.0F / 60.0F), 2.0F);
}

TEST_F(Node3DDefinitionsUVETest, SpringArmObstructThenClearRestoresTheAuthoredPose) {
    // The drift claim the whole child-delta design rests on, measured on the motion law itself:
    // an arm that snaps to a wall and springs back home must return to EXACTLY its authored
    // length, so the sum of every per-step child shift telescopes back to precisely zero extra
    // offset - not asymptotically close to it.
    float current = 4.0F;
    float childLocalZ = 4.0F; // camera authored at full reach behind the pivot
    constexpr float kAuthoredZ = 4.0F;
    constexpr float kDt = 1.0F / 60.0F;

    // Drive into a doorway over 5 steps; each step snaps deeper or holds, and the child rides.
    const float doorwayTarget = ResolveSpringArm3DTargetUVE(0.6F, 0.1F, 4.0F);
    float maximumDrift = 0.0F;
    for (int step = 0; step < 5; ++step) {
        const float next = ResolveSpringArm3DLengthUVE(current, doorwayTarget, 8.0F, kDt);
        childLocalZ += next - current;
        current = next;
        EXPECT_LT(current, kAuthoredZ);
    }
    // Then 600 steps of open air: springs back, settles exactly, and the child is returned home.
    for (int step = 0; step < 600; ++step) {
        const float next = ResolveSpringArm3DLengthUVE(current, 4.0F, 8.0F, kDt);
        childLocalZ += next - current;
        current = next;
        maximumDrift = std::max(maximumDrift, std::fabs(childLocalZ - current));
    }
    EXPECT_EQ(current, 4.0F);
    // The residual measured below is pure float-association error on ~600 accumulated deltas,
    // expected orders of magnitude below anything renderable - and the restored length itself
    // is exact, so the arm owes the scene nothing once settled.
    EXPECT_NEAR(childLocalZ + (4.0F - current), kAuthoredZ, 1.0e-6F);
    EXPECT_LE(maximumDrift, 1.0e-5F);
}

TEST_F(Node3DDefinitionsUVETest, SpawnPointSelectionIsDeterministicContentOrder) {
    // No candidates, no spawn.
    EXPECT_EQ(ResolveSpawnPoint3DSelectionUVE(std::span<const SpawnPoint3DCandidateUVE>{}),
              std::nullopt);
    // Sentinels are filtered again at this seam too: a caller bug must not become the spawn.
    const SpawnPoint3DCandidateUVE sentinel{kInvalidEntityUVE, false};
    {
        const SpawnPoint3DCandidateUVE only[]{sentinel};
        EXPECT_EQ(ResolveSpawnPoint3DSelectionUVE(only), std::nullopt);
    }

    const SpawnPoint3DCandidateUVE a{EntityUVE{7U, 0U}, false};
    const SpawnPoint3DCandidateUVE b{EntityUVE{2U, 1U}, true};
    const SpawnPoint3DCandidateUVE c{EntityUVE{2U, 0U}, false};
    {
        const SpawnPoint3DCandidateUVE only[]{a};
        EXPECT_EQ(ResolveSpawnPoint3DSelectionUVE(only), a.entity);
    }
    // Stable content order = lexicographic (index, generation): iteration order is irrelevant,
    // the same scene spawns the same way every time.
    {
        const SpawnPoint3DCandidateUVE all[]{a, b, c};
        EXPECT_EQ(ResolveSpawnPoint3DSelectionUVE(all), c.entity);
    }
    {
        const SpawnPoint3DCandidateUVE reversed[]{c, b, a};
        EXPECT_EQ(ResolveSpawnPoint3DSelectionUVE(reversed), c.entity);
    }
    {
        const SpawnPoint3DCandidateUVE mixed[]{b, a, sentinel, c};
        EXPECT_EQ(ResolveSpawnPoint3DSelectionUVE(mixed), c.entity);
    }
    // And `oneShot` plays no part in the ranking - it only spends the winner afterwards.
    const SpawnPoint3DCandidateUVE d{EntityUVE{1U, 0U}, true};
    {
        const SpawnPoint3DCandidateUVE pair[]{c, d};
        EXPECT_EQ(ResolveSpawnPoint3DSelectionUVE(pair), d.entity);
    }
}

TEST_F(Node3DDefinitionsUVETest, SpawnPoseComposeAppliesTheAuthoredOffsetInNodeSpace) {
    // Identity node: the offset is the pose.
    const std::optional<SpawnPoint3DPoseUVE> flat =
        ComposeSpawnPointPoseUVE({}, {}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, {});
    ASSERT_TRUE(flat.has_value());
    EXPECT_NEAR(flat->position.y, 1.0F, 1.0e-6F);

    // Translated node: node position adds after the offset is rotated.
    const Math::QuaternionUVE halfTurnAboutY{0.0F, 1.0F, 0.0F, 0.0F};
    const std::optional<SpawnPoint3DPoseUVE> posed = ComposeSpawnPointPoseUVE(
        Math::Vector3UVE{10.0F, 0.0F, 10.0F}, halfTurnAboutY, Math::Vector3UVE{1.0F, 0.0F, 0.0F},
        {});
    ASSERT_TRUE(posed.has_value());
    // 180 degrees about Y maps (1,0,0) to (-1,0,0), then the node position adds.
    EXPECT_NEAR(posed->position.x, 9.0F, 1.0e-5F);
    EXPECT_NEAR(posed->position.z, 10.0F, 1.0e-5F);
    // Rotation composes the same way the sweep composes parent rotation.
    EXPECT_NEAR(posed->rotation.y, 1.0F, 1.0e-5F);
    EXPECT_NEAR(posed->rotation.w, 0.0F, 1.0e-5F);

    // Garbage in, no teleport out.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ComposeSpawnPointPoseUVE(Math::Vector3UVE{nan, 0.0F, 0.0F}, {}, {}, {}).has_value());
    EXPECT_FALSE(ComposeSpawnPointPoseUVE({}, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F}, {}, {})
                     .has_value());
}

TEST_F(Node3DDefinitionsUVETest, SpawnPlayerLocalIsTheSweepInverse) {
    const SpawnPoint3DPoseUVE worldPose{Math::Vector3UVE{9.0F, 2.0F, -2.0F}, {}};

    // Root-level player (identity parent TRS): the pose falls straight through.
    const std::optional<SpawnPoint3DPoseUVE> rootLocal =
        ResolveSpawnPointPlayerLocalUVE(worldPose, {}, {}, Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    ASSERT_TRUE(rootLocal.has_value());
    EXPECT_NEAR(rootLocal->position.x, 9.0F, 1.0e-6F);

    // Translated parent: subtract, then divide scale component-wise.
    const std::optional<SpawnPoint3DPoseUVE> scaled = ResolveSpawnPointPlayerLocalUVE(
        worldPose, Math::Vector3UVE{5.0F, 1.0F, -2.0F}, {}, Math::Vector3UVE{2.0F, 1.0F, 0.5F});
    ASSERT_TRUE(scaled.has_value());
    EXPECT_NEAR(scaled->position.x, 2.0F, 1.0e-5F);
    EXPECT_NEAR(scaled->position.y, 1.0F, 1.0e-5F);
    EXPECT_NEAR(scaled->position.z, 0.0F, 1.0e-5F);

    // Rotated parent (180 degrees about Y): the offset un-rotates on entry.
    const Math::QuaternionUVE halfTurnAboutY{0.0F, 1.0F, 0.0F, 0.0F};
    const std::optional<SpawnPoint3DPoseUVE> turned = ResolveSpawnPointPlayerLocalUVE(
        SpawnPoint3DPoseUVE{Math::Vector3UVE{8.0F, 0.0F, 3.0F}, {}}, Math::Vector3UVE{10.0F, 0.0F, 3.0F},
        halfTurnAboutY, Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    ASSERT_TRUE(turned.has_value());
    EXPECT_NEAR(turned->position.x, 2.0F, 1.0e-5F);
    // Parent rotation inverts onto the spawn rotation; with identity spawn that hands the
    // parent's own half-turn back (180 degrees about Y maps (1,0,0) to (-1,0,0)).
    const Math::Vector3UVE mapped = Math::RotateVectorUVE(turned->rotation, {1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(mapped.x, -1.0F, 1.0e-5F);

    // The property the whole feature rests on: feed the solved local pose back through the
    // sweep's forward formula and land on the spawn pose again - measured, not asserted away.
    {
        const SpawnPoint3DPoseUVE pose{Math::Vector3UVE{3.5F, -1.0F, 8.0F}, halfTurnAboutY};
        const Math::Vector3UVE parentPos{-4.0F, 2.0F, 1.0F};
        const Math::QuaternionUVE parentRot{0.0F, 0.0F, 1.0F, 0.0F}; // 180 degrees about Z
        const Math::Vector3UVE parentScale{2.0F, 3.0F, 0.5F};
        const std::optional<SpawnPoint3DPoseUVE> local =
            ResolveSpawnPointPlayerLocalUVE(pose, parentPos, parentRot, parentScale);
        ASSERT_TRUE(local.has_value());
        const Math::Vector3UVE roundTrip =
            parentPos + Math::RotateVectorUVE(parentRot, parentScale * local->position);
        EXPECT_NEAR(roundTrip.x, pose.position.x, 1.0e-4F);
        EXPECT_NEAR(roundTrip.y, pose.position.y, 1.0e-4F);
        EXPECT_NEAR(roundTrip.z, pose.position.z, 1.0e-4F);
        const Math::Vector3UVE forwardA = Math::RotateVectorUVE(
            Math::MultiplyUVE(parentRot, local->rotation), {0.0F, 0.0F, 1.0F});
        const Math::Vector3UVE forwardB = Math::RotateVectorUVE(pose.rotation, {0.0F, 0.0F, 1.0F});
        EXPECT_NEAR(forwardA.x, forwardB.x, 1.0e-4F);
        EXPECT_NEAR(forwardA.y, forwardB.y, 1.0e-4F);
    }

    // Refusals: a zero-scaled parent axis has no local pose to solve into, and garbage stays out.
    EXPECT_FALSE(ResolveSpawnPointPlayerLocalUVE(worldPose, {}, {}, Math::Vector3UVE{0.0F, 1.0F, 1.0F})
                     .has_value());
    EXPECT_FALSE(ResolveSpawnPointPlayerLocalUVE(worldPose, Math::Vector3UVE{
                                                 std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                                                 {}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})
                     .has_value());
}

TEST_F(Node3DDefinitionsUVETest, InteractionAreaCandidateCapHonoursAuthoredBudgetAndStorageBound) {
    // The per-tick interactor list is storage-bounded AND authored-bounded; the effective cap is
    // the smaller of the two, so an authored value above the fixed array can never scribble past
    // it, and a tighter authored budget is respected exactly.
    EXPECT_EQ(ResolveInteractionAreaCandidateCapUVE(4U, kMaximumInteractionAreaCandidatesUVE), 4U);
    EXPECT_EQ(ResolveInteractionAreaCandidateCapUVE(16U, kMaximumInteractionAreaCandidatesUVE), 16U);
    EXPECT_EQ(ResolveInteractionAreaCandidateCapUVE(4096U, kMaximumInteractionAreaCandidatesUVE),
              kMaximumInteractionAreaCandidatesUVE);
    EXPECT_EQ(ResolveInteractionAreaCandidateCapUVE(0U, kMaximumInteractionAreaCandidatesUVE), 0U);
    // The bound side is honoured symmetrically for storage smaller than the authored budget.
    EXPECT_EQ(ResolveInteractionAreaCandidateCapUVE(8U, 3U), 3U);
}

TEST_F(Node3DDefinitionsUVETest, PrimaryInteractorSelectionMatchesTheSpawnPointPlayerRule) {
    // Same contract as SpawnPoint3D selection: content order (index, generation) decides, never
    // ECS pool order, and an empty or garbage-only input fails closed to no value.
    EXPECT_EQ(ResolvePrimaryInteractorUVE(std::span<const EntityUVE>{}), std::nullopt);
    const EntityUVE sentinel = kInvalidEntityUVE;
    {
        const EntityUVE only[]{sentinel};
        EXPECT_EQ(ResolvePrimaryInteractorUVE(only), std::nullopt);
    }
    const EntityUVE a{7U, 0U};
    const EntityUVE b{2U, 1U};
    const EntityUVE c{2U, 0U};
    {
        const EntityUVE only[]{a};
        ASSERT_TRUE(ResolvePrimaryInteractorUVE(only).has_value());
        EXPECT_EQ(*ResolvePrimaryInteractorUVE(only), a);
    }
    {
        // Listed in "wrong" order on purpose: (2,1) and (2,0) both precede (7,0), and between the
        // two index-2 entries the lower generation wins - identical ranking to spawn selection.
        const EntityUVE all[]{a, b, c};
        ASSERT_TRUE(ResolvePrimaryInteractorUVE(all).has_value());
        EXPECT_EQ(*ResolvePrimaryInteractorUVE(all), c);
    }
}

TEST_F(Node3DDefinitionsUVETest, InteractionFocusPicksTheNearestAreaWithDeterministicTies) {
    // The Lyra-style best-candidate rule this engine owns so games do not re-implement it: the
    // nearest overlapping area wins; equal distances fall back to (index,generation) ordering so
    // the answer never depends on iteration/pool order. Empty or garbage input means no focus.
    EXPECT_EQ(ResolveInteractionFocusUVE(std::span<const InteractionFocusCandidateUVE>{}),
              std::nullopt);
    const InteractionFocusCandidateUVE sentinel{kInvalidEntityUVE, 0.0F};
    {
        const InteractionFocusCandidateUVE only[]{sentinel};
        EXPECT_EQ(ResolveInteractionFocusUVE(only), std::nullopt);
    }
    const InteractionFocusCandidateUVE near{EntityUVE{9U, 0U}, 1.0F};
    const InteractionFocusCandidateUVE far{EntityUVE{1U, 0U}, 4.0F};
    {
        // A worse entity wins anyway because it is nearer; order in the span is irrelevant.
        const InteractionFocusCandidateUVE all[]{far, near};
        ASSERT_TRUE(ResolveInteractionFocusUVE(all).has_value());
        EXPECT_EQ(*ResolveInteractionFocusUVE(all), near.areaEntity);
    }
    {
        // The exact tie: equal distances, entities listed in REVERSE ranked order - the lower
        // (index,generation) still wins, which is the determinism claim this test is measuring.
        const InteractionFocusCandidateUVE tieA{EntityUVE{5U, 0U}, 2.0F};
        const InteractionFocusCandidateUVE tieB{EntityUVE{2U, 0U}, 2.0F};
        const InteractionFocusCandidateUVE reversed[]{tieA, tieB};
        ASSERT_TRUE(ResolveInteractionFocusUVE(reversed).has_value());
        EXPECT_EQ(*ResolveInteractionFocusUVE(reversed), tieB.areaEntity);
        const InteractionFocusCandidateUVE natural[]{tieB, tieA};
        ASSERT_TRUE(ResolveInteractionFocusUVE(natural).has_value());
        EXPECT_EQ(*ResolveInteractionFocusUVE(natural), tieB.areaEntity);
    }
}

TEST_F(Node3DDefinitionsUVETest, MarkerPoseComposeSharesTheSpawnPointCompositionContract) {
    // The pure half of fly-to-marker: the same measured composition contract the spawn point
    // owns - position = node position + node rotation * authored offset, rotation composes,
    // node scale stays out of it - so a marker's viewpoint tracks prefab-level transforms
    // identically to every other authored-offset node in the engine.
    const std::optional<Marker3DPoseUVE> flat =
        ComposeMarker3DPoseUVE({}, {}, Math::Vector3UVE{0.0F, 0.0F, -4.0F}, {});
    ASSERT_TRUE(flat.has_value());
    EXPECT_NEAR(flat->position.z, -4.0F, 1.0e-6F);

    const Math::QuaternionUVE halfTurnAboutY{0.0F, 1.0F, 0.0F, 0.0F};
    const std::optional<Marker3DPoseUVE> posed = ComposeMarker3DPoseUVE(
        Math::Vector3UVE{10.0F, 0.0F, 10.0F}, halfTurnAboutY,
        Math::Vector3UVE{1.0F, 0.0F, 0.0F}, {});
    ASSERT_TRUE(posed.has_value());
    // 180 degrees about Y maps (1,0,0) to (-1,0,0), then the node position adds.
    EXPECT_NEAR(posed->position.x, 9.0F, 1.0e-5F);
    EXPECT_NEAR(posed->position.z, 10.0F, 1.0e-5F);
    EXPECT_NEAR(posed->rotation.y, 1.0F, 1.0e-5F);
    EXPECT_NEAR(posed->rotation.w, 0.0F, 1.0e-5F);

    // A rotated marker composes its facing under the node's rotation, not around it.
    const Math::QuaternionUVE quarterTurnAboutY{0.0F, 0.70710678F, 0.0F, 0.70710678F};
    const std::optional<Marker3DPoseUVE> faced = ComposeMarker3DPoseUVE(
        Math::Vector3UVE{3.0F, 1.0F, -2.0F}, quarterTurnAboutY, {}, halfTurnAboutY);
    ASSERT_TRUE(faced.has_value());
    const Math::QuaternionUVE expectedFacing =
        Math::MultiplyUVE(quarterTurnAboutY, halfTurnAboutY);
    const Math::Vector3UVE expectedForward =
        Math::RotateVectorUVE(expectedFacing, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    const Math::Vector3UVE actualForward =
        Math::RotateVectorUVE(faced->rotation, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    EXPECT_NEAR(actualForward.x, expectedForward.x, 1.0e-5F);
    EXPECT_NEAR(actualForward.y, expectedForward.y, 1.0e-5F);
    EXPECT_NEAR(actualForward.z, expectedForward.z, 1.0e-5F);

    // Garbage in, no viewpoint out - fail-closed like every other 3D resolver.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ComposeMarker3DPoseUVE(Math::Vector3UVE{nan, 0.0F, 0.0F}, {}, {}, {}).has_value());
    EXPECT_FALSE(ComposeMarker3DPoseUVE({}, Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F}, {}, {})
                     .has_value());
    EXPECT_FALSE(ComposeMarker3DPoseUVE({}, {}, Math::Vector3UVE{nan, 0.0F, 0.0F}, {}).has_value());
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
