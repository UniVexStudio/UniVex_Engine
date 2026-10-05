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
#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/object_metadata_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/all_objects_3d_uve.h"
#include "uve/scene/objects/scene_object_registry_uve.h"
#include "uve/scene/objects/scene_object_type_uve.h"
#include "uve/scene/objects/scene_root_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

// Every one of the 18 component-backed object kinds' definition is reachable from the aggregate
// header, mirroring the registry test's guarantee for the 21 data-carrying object components:
// no object kind's home file can be silently dropped without breaking this compile.
static_assert(std::is_class_v<Object3DObjectDefinitionUVE>);            // Object3D
static_assert(std::is_class_v<Area3DObjectDefinitionUVE>);           // Area3D
static_assert(std::is_class_v<Static3DObjectDefinitionUVE>);     // Static3D
static_assert(std::is_class_v<Character3DObjectDefinitionUVE>);  // Character3D
static_assert(std::is_class_v<Player3DObjectDefinitionUVE>);     // Player3D
static_assert(std::is_class_v<Camera3DObjectDefinitionUVE>);         // Camera3D
static_assert(std::is_class_v<MeshInstance3DObjectDefinitionUVE>);   // MeshInstance3D
static_assert(std::is_class_v<BoxMesh3DObjectDefinitionUVE>);        // BoxMesh3D
static_assert(std::is_class_v<SphereMesh3DObjectDefinitionUVE>);     // SphereMesh3D
static_assert(std::is_class_v<PlaneMesh3DObjectDefinitionUVE>);      // PlaneMesh3D
static_assert(std::is_class_v<Light3DObjectDefinitionUVE>);          // Light3D
static_assert(std::is_class_v<Collider3DObjectDefinitionUVE>);       // Collider3D
static_assert(std::is_class_v<Rigid3DObjectDefinitionUVE>);      // Rigid3D
static_assert(std::is_class_v<AudioSource3DObjectDefinitionUVE>);        // AudioSource3D
static_assert(std::is_class_v<ParticleEmitter3DObjectDefinitionUVE>); // ParticleEmitter3D
static_assert(std::is_class_v<ScriptObjectDefinitionUVE>);           // Script
static_assert(std::is_class_v<SpringArm3DObjectDefinitionUVE>);      // SpringArm3D
static_assert(std::is_class_v<AnimationSequencerObjectDefinitionUVE>);  // AnimationSequencer
static_assert(std::is_class_v<AnimationGraphObjectDefinitionUVE>);    // AnimationGraph

class Object3DDefinitionsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    [[nodiscard]] EntityUVE CreateEntityUVE() {
        return entityManager.CreateEntityUVE();
    }
};

// Every 3D scene-object recipe stands on the Object3D baseline; asserting it once per kind (instead
// of re-typing four component checks in every block) pins the composition, not just its effect.
void ExpectObject3DBaselineUVE(EntityManagerUVE& entityManager, const EntityUVE entity,
                             const std::string_view expectedName) {
    EXPECT_TRUE(entityManager.HasComponentUVE<TransformComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<HierarchyComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<NameComponentUVE>(entity));
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(entity).name, expectedName);
}

/// A pure Object: in the hierarchy and named, with the Object section, and nothing spatial.
void ExpectPureObjectUVE(EntityManagerUVE& entityManager, const EntityUVE entity, const std::string_view expectedName) {
    EXPECT_FALSE(entityManager.HasComponentUVE<TransformComponentUVE>(entity));
    EXPECT_FALSE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity));
    EXPECT_FALSE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<HierarchyComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<ProcessComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<ObjectMetadataComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<NameComponentUVE>(entity));
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(entity).name, expectedName);
}

TEST_F(Object3DDefinitionsUVETest, AllDefinitionDefaultsAreValid) {
    EXPECT_TRUE(IsObject3DObjectDefinitionValidUVE(Object3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsArea3DObjectDefinitionValidUVE(Area3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsStatic3DObjectDefinitionValidUVE(Static3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsCharacter3DObjectDefinitionValidUVE(Character3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsPlayer3DObjectDefinitionValidUVE(Player3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsCamera3DObjectDefinitionValidUVE(Camera3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsMeshInstance3DObjectDefinitionValidUVE(MeshInstance3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsBoxMesh3DObjectDefinitionValidUVE(BoxMesh3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsSphereMesh3DObjectDefinitionValidUVE(SphereMesh3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsPlaneMesh3DObjectDefinitionValidUVE(PlaneMesh3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsLight3DObjectDefinitionValidUVE(Light3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsCollider3DObjectDefinitionValidUVE(Collider3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsRigid3DObjectDefinitionValidUVE(Rigid3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsAudioSource3DObjectDefinitionValidUVE(AudioSource3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsParticleEmitter3DObjectDefinitionValidUVE(ParticleEmitter3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsScriptObjectDefinitionValidUVE(ScriptObjectDefinitionUVE{}));
    EXPECT_TRUE(IsSpringArm3DObjectDefinitionValidUVE(SpringArm3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsAnimationSequencerObjectDefinitionValidUVE(AnimationSequencerObjectDefinitionUVE{}));
    EXPECT_TRUE(IsAnimationGraphObjectDefinitionValidUVE(AnimationGraphObjectDefinitionUVE{}));
    EXPECT_TRUE(IsKinematic3DObjectDefinitionValidUVE(Kinematic3DObjectDefinitionUVE{}));
}

// The seven names the editor's legacy EditorEntityKindUVE path surfaces are locked at compile
// time: the editor now sources every default name from these definitions, so any drift here
// would silently rename what legacy creation produces.
static_assert(Object3DObjectDefinitionUVE::defaultName == "Object3D");
static_assert(Camera3DObjectDefinitionUVE::defaultName == "Camera");
static_assert(Light3DObjectDefinitionUVE::defaultName == "Directional Light");
static_assert(Collider3DObjectDefinitionUVE::defaultName == "Collision Box");
static_assert(BoxMesh3DObjectDefinitionUVE::defaultName == "Cube");
static_assert(SphereMesh3DObjectDefinitionUVE::defaultName == "UV Sphere");
static_assert(PlaneMesh3DObjectDefinitionUVE::defaultName == "Plane");
static_assert(Kinematic3DObjectDefinitionUVE::defaultName == "Kinematic3D");
static_assert(SpringArm3DObjectDefinitionUVE::defaultName == "SpringArm3D");

TEST_F(Object3DDefinitionsUVETest, DefaultNamesAreAuthoredPerKindNotGeneric) {
    // The six kinds that previously lived behind legacy EditorEntityKindUVE values keep their
    // exact historical names; the kinds the editor used to name "Empty" now carry their own.
    //
    // The transform-only base is Object3D every way an author can meet it: display name, kind
    // enumerator, and the "object_3d" on-disk id. Its two earlier ids, "node_3d" and "empty",
    // still resolve to it at load (covered below) so the renames broke no saved file.
    EXPECT_EQ(Object3DObjectDefinitionUVE::defaultName, "Object3D");
    EXPECT_EQ(Camera3DObjectDefinitionUVE::defaultName, "Camera");
    EXPECT_EQ(Light3DObjectDefinitionUVE::defaultName, "Directional Light");
    EXPECT_EQ(Collider3DObjectDefinitionUVE::defaultName, "Collision Box");
    EXPECT_EQ(BoxMesh3DObjectDefinitionUVE::defaultName, "Cube");
    EXPECT_EQ(SphereMesh3DObjectDefinitionUVE::defaultName, "UV Sphere");
    EXPECT_EQ(PlaneMesh3DObjectDefinitionUVE::defaultName, "Plane");
    EXPECT_EQ(Area3DObjectDefinitionUVE::defaultName, "Area3D");
    EXPECT_EQ(Static3DObjectDefinitionUVE::defaultName, "Static3D");
    EXPECT_EQ(Character3DObjectDefinitionUVE::defaultName, "Character3D");
    EXPECT_EQ(MeshInstance3DObjectDefinitionUVE::defaultName, "MeshInstance3D");
    EXPECT_EQ(Rigid3DObjectDefinitionUVE::defaultName, "Rigid3D");
    EXPECT_EQ(AudioSource3DObjectDefinitionUVE::defaultName, "AudioSource3D");
    EXPECT_EQ(ParticleEmitter3DObjectDefinitionUVE::defaultName, "ParticleEmitter3D");
    EXPECT_EQ(ScriptObjectDefinitionUVE::defaultName, "Script");
    EXPECT_EQ(AnimationSequencerObjectDefinitionUVE::defaultName, "AnimationSequencer");
    EXPECT_EQ(AnimationGraphObjectDefinitionUVE::defaultName, "AnimationGraph");
    EXPECT_EQ(Kinematic3DObjectDefinitionUVE::defaultName, "Kinematic3D");
}

TEST_F(Object3DDefinitionsUVETest, ApplyAttachesEachKindsExactComponentRecipe) {
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyObject3DObjectDefinitionUVE(entityManager, entity, Object3DObjectDefinitionUVE{});
        // Object3D's recipe is the baseline itself, not "nothing": the guarantee is pinned
        // behaviour-for-behaviour in the dedicated tests below.
        ExpectObject3DBaselineUVE(entityManager, entity, Object3DObjectDefinitionUVE::defaultName);
        EXPECT_FALSE(entityManager.HasComponentUVE<CameraComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyArea3DObjectDefinitionUVE(entityManager, entity, Area3DObjectDefinitionUVE{});
        ExpectObject3DBaselineUVE(entityManager, entity, Area3DObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<AreaComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyStatic3DObjectDefinitionUVE(entityManager, entity, Static3DObjectDefinitionUVE{});
        ExpectObject3DBaselineUVE(entityManager, entity, Static3DObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
        EXPECT_FALSE(entityManager.HasComponentUVE<Rigid3DComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyCamera3DObjectDefinitionUVE(entityManager, entity, Camera3DObjectDefinitionUVE{});
        ExpectObject3DBaselineUVE(entityManager, entity, Camera3DObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<CameraComponentUVE>(entity));
        EXPECT_TRUE(entityManager.GetComponentUVE<CameraComponentUVE>(entity).current);
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyMeshInstance3DObjectDefinitionUVE(entityManager, entity, MeshInstance3DObjectDefinitionUVE{});
        ExpectObject3DBaselineUVE(entityManager, entity, MeshInstance3DObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<MeshComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyCollider3DObjectDefinitionUVE(entityManager, entity, Collider3DObjectDefinitionUVE{});
        ExpectObject3DBaselineUVE(entityManager, entity, Collider3DObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyRigid3DObjectDefinitionUVE(entityManager, entity, Rigid3DObjectDefinitionUVE{});
        ExpectObject3DBaselineUVE(entityManager, entity, Rigid3DObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<Rigid3DComponentUVE>(entity));
        // Object3D > PhysicsObject3D > Rigid3D: a simulated body is a physics object, so it is the
        // component that decides what its disabled state means and how it yields in a contact.
        EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsObjectComponentUVE>(entity));
        // A simulated body without a shape is a body nothing can hit: the kind carries a collider
        // the same way Static3D and Kinematic3D do, and its own definition validates it.
        EXPECT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
        EXPECT_TRUE(IsColliderComponentValidUVE(entityManager.GetComponentUVE<ColliderComponentUVE>(entity)));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyAudioSource3DObjectDefinitionUVE(entityManager, entity, AudioSource3DObjectDefinitionUVE{});
        ExpectObject3DBaselineUVE(entityManager, entity, AudioSource3DObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<AudioSourceComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyParticleEmitter3DObjectDefinitionUVE(entityManager, entity, ParticleEmitter3DObjectDefinitionUVE{});
        ExpectObject3DBaselineUVE(entityManager, entity, ParticleEmitter3DObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<ParticleEmitterComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyScriptObjectDefinitionUVE(entityManager, entity, ScriptObjectDefinitionUVE{});
        ExpectObject3DBaselineUVE(entityManager, entity, ScriptObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<ScriptComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyAnimationSequencerObjectDefinitionUVE(entityManager, entity, AnimationSequencerObjectDefinitionUVE{});
        ExpectPureObjectUVE(entityManager, entity, AnimationSequencerObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<AnimationSequencerComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyAnimationGraphObjectDefinitionUVE(entityManager, entity, AnimationGraphObjectDefinitionUVE{});
        ExpectPureObjectUVE(entityManager, entity, AnimationGraphObjectDefinitionUVE::defaultName);
        EXPECT_TRUE(entityManager.HasComponentUVE<AnimationGraphComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplySpringArm3DObjectDefinitionUVE(entityManager, entity, SpringArm3DObjectDefinitionUVE{});
        ExpectObject3DBaselineUVE(entityManager, entity, SpringArm3DObjectDefinitionUVE::defaultName);
        ASSERT_TRUE(entityManager.HasComponentUVE<SpringArm3DComponentUVE>(entity));
        // The recipe seeds the runtime state exactly the way the deserializer does: an arm that
        // has never been simulated reads as fully extended, valid before the first step.
        const SpringArm3DComponentUVE& arm =
            entityManager.GetComponentUVE<SpringArm3DComponentUVE>(entity);
        EXPECT_EQ(arm.currentLength, arm.armLength);
        EXPECT_TRUE(IsSpringArm3DObjectComponentValidUVE(arm));
    }
}

TEST_F(Object3DDefinitionsUVETest, PrimitiveMeshRecipesKeepTheirDistinctShapesColorsAndColliders) {
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyBoxMesh3DObjectDefinitionUVE(entityManager, entity, BoxMesh3DObjectDefinitionUVE{});
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
        ApplySphereMesh3DObjectDefinitionUVE(entityManager, entity, SphereMesh3DObjectDefinitionUVE{});
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
        ApplyPlaneMesh3DObjectDefinitionUVE(entityManager, entity, PlaneMesh3DObjectDefinitionUVE{});
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

TEST_F(Object3DDefinitionsUVETest, Player3DIsACharacterMarkedAsThePossessedBody) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyPlayer3DObjectDefinitionUVE(entityManager, entity, Player3DObjectDefinitionUVE{});
    ExpectObject3DBaselineUVE(entityManager, entity, Player3DObjectDefinitionUVE::defaultName);
    EXPECT_TRUE(entityManager.HasComponentUVE<CharacterControllerComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<PlayerComponentUVE>(entity));
    EXPECT_TRUE(entityManager.GetComponentUVE<PlayerComponentUVE>(entity).possessOnPlay);
    ASSERT_TRUE(entityManager.HasComponentUVE<HealthComponentUVE>(entity));
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<HealthComponentUVE>(entity).maxHealth, 100.0F);
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, entity), Objects::SceneObjectKindUVE::Player3D);
}

TEST_F(Object3DDefinitionsUVETest, CharacterBodyIsItsChainPlusAReadyToWalkCapsule) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyCharacter3DObjectDefinitionUVE(entityManager, entity, Character3DObjectDefinitionUVE{});
    // Object3D > PhysicsObject3D > SolidBody3D > Character3D, and nothing from another branch.
    ExpectObject3DBaselineUVE(entityManager, entity, Character3DObjectDefinitionUVE::defaultName);
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsObjectComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<SolidBodyComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<CharacterControllerComponentUVE>(entity));
    EXPECT_FALSE(entityManager.HasComponentUVE<Rigid3DComponentUVE>(entity));
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
    ApplyCharacter3DObjectDefinitionUVE(entityManager, entity, Character3DObjectDefinitionUVE{});
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<CharacterControllerComponentUVE>(entity).moveSpeed, 9.0F);
}

TEST_F(Object3DDefinitionsUVETest, CharacterBodyRefusesSettingsItCannotRunWith) {
    EXPECT_TRUE(IsCharacter3DObjectDefinitionValidUVE(Character3DObjectDefinitionUVE{}));
    const auto invalid = [](auto change) {
        Character3DObjectDefinitionUVE definition{};
        change(definition.controller);
        return !IsCharacter3DObjectDefinitionValidUVE(definition);
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

TEST_F(Object3DDefinitionsUVETest, SolidBodyBaseSitsOnPhysicsObject) {
    const EntityUVE entity = CreateEntityUVE();
    ApplySolidBody3DBaseUVE(entityManager, entity, "Static3D");
    ExpectObject3DBaselineUVE(entityManager, entity, "Static3D");
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsObjectComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<SolidBodyComponentUVE>(entity));
    entityManager.GetComponentUVE<SolidBodyComponentUVE>(entity).lockMotionZ = true;
    ApplySolidBody3DBaseUVE(entityManager, entity, "Static3D");
    EXPECT_TRUE(entityManager.GetComponentUVE<SolidBodyComponentUVE>(entity).lockMotionZ);
}

TEST_F(Object3DDefinitionsUVETest, KinematicRecipeMatchesTheFormerInlineEditorRecipe) {
    // The last inline multi-component recipe the editor's creation switch used to hardcode:
    // collider + kinematic body + the animatable body's own component, in that spirit unchanged.
    const EntityUVE entity = CreateEntityUVE();
    ApplyKinematic3DObjectDefinitionUVE(entityManager, entity, Kinematic3DObjectDefinitionUVE{});
    // Object3D > PhysicsObject3D > Kinematic3D, so a platform that is stopped can be kept as an
    // immovable obstacle or taken out of the world rather than only deleted.
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsObjectComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<ColliderComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<Rigid3DComponentUVE>(entity));
    ASSERT_TRUE(entityManager.HasComponentUVE<Kinematic3DComponentUVE>(entity));
    EXPECT_TRUE(entityManager.GetComponentUVE<Rigid3DComponentUVE>(entity).isKinematic);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Kinematic3DComponentUVE>(entity).interpolation, 1.0F);

    Kinematic3DObjectDefinitionUVE nonKinematic{};
    nonKinematic.body.isKinematic = false;
    EXPECT_FALSE(IsKinematic3DObjectDefinitionValidUVE(nonKinematic));
}

TEST_F(Object3DDefinitionsUVETest, LightRecipeDefaultsToDirectionalSunLight) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyLight3DObjectDefinitionUVE(entityManager, entity, Light3DObjectDefinitionUVE{});
    ASSERT_TRUE(entityManager.HasComponentUVE<LightComponentUVE>(entity));
    EXPECT_EQ(entityManager.GetComponentUVE<LightComponentUVE>(entity).type, LightTypeUVE::Directional);
}

TEST_F(Object3DDefinitionsUVETest, Object3DIsReachableUnderBothItsNewAndLegacyTypeIds) {
    const Objects::SceneObjectDescriptorUVE* descriptor =
        Objects::FindSceneObjectDescriptorUVE(Objects::SceneObjectKindUVE::Object3D);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->typeId, "object_3d");
    EXPECT_EQ(descriptor->displayName, "Object3D");
    EXPECT_TRUE(descriptor->libraryCreatable);

    // New writes use the canonical id; both ids this kind had before it must resolve to the very
    // same row, since saved documents and layouts carrying either have no way to upgrade
    // themselves: "node_3d" from the Object3D -> Object3D rename, "empty" from before that.
    EXPECT_EQ(Objects::FindSceneObjectDescriptorUVE("object_3d"), descriptor);
    EXPECT_EQ(Objects::FindSceneObjectDescriptorUVE("node_3d"), descriptor);
    EXPECT_EQ(Objects::FindSceneObjectDescriptorUVE("empty"), descriptor);
    EXPECT_EQ(Objects::GetSceneObjectTypeIdUVE(Objects::SceneObjectKindUVE::Object3D), "object_3d");
}

TEST_F(Object3DDefinitionsUVETest, Object3DApplyAttachesALocalTransformToABareEntity) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyObject3DObjectDefinitionUVE(entityManager, entity, Object3DObjectDefinitionUVE{});

    // The meaningful part of the guarantee: an Object3D reached without the creation shell still
    // ends up with the transform every scene object needs, at identity and named after its kind.
    const TransformComponentUVE& local = entityManager.GetComponentUVE<TransformComponentUVE>(entity);
    EXPECT_EQ(local.localPosition.x, 0.0F);
    EXPECT_EQ(local.localPosition.y, 0.0F);
    EXPECT_EQ(local.localPosition.z, 0.0F);
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(entity).name,
              Object3DObjectDefinitionUVE::defaultName);
    EXPECT_EQ(entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent, kInvalidEntityUVE);
}

TEST_F(Object3DDefinitionsUVETest, Object3DApplyPreservesAuthoredValuesAndRepairsOnlyWhatIsMissing) {
    const EntityUVE entity = CreateEntityUVE();
    // A partially-baselined entity, as a partial deserialization leaves behind: transform is
    // present and authored, the rest of the baseline is not.
    TransformComponentUVE authored;
    authored.localPosition = Math::Vector3UVE{3.0F, -2.0F, 7.5F};
    entityManager.AddComponentUVE<TransformComponentUVE>(entity, authored);
    entityManager.AddComponentUVE<NameComponentUVE>(entity, NameComponentUVE{"AuthoredPivot"});

    ApplyObject3DObjectDefinitionUVE(entityManager, entity, Object3DObjectDefinitionUVE{});

    // Authored state is sacred: position and name survive application untouched, and the missing
    // half of the baseline is what got repaired.
    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition.x, 3.0F);
    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition.z, 7.5F);
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(entity).name, "AuthoredPivot");
    EXPECT_TRUE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<HierarchyComponentUVE>(entity));
}

TEST_F(Object3DDefinitionsUVETest, Object3DCarriesVisibilityAndTheCommonObjectSection) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyObject3DObjectDefinitionUVE(entityManager, entity, Object3DObjectDefinitionUVE{});
    // Its Inspector has no Add Component, so every section it shows is attached by the recipe.
    EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<ProcessComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<ThreadGroupComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsInterpolationComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<AutoTranslateComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<EditorDescriptionComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<ScriptComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<ObjectMetadataComponentUVE>(entity));
}

TEST_F(Object3DDefinitionsUVETest, AbstractBasesAreObject3DPlusTheirOwnComponent) {
    const EntityUVE bone = CreateEntityUVE();
    const EntityUVE physics = CreateEntityUVE();
    const EntityUVE render = CreateEntityUVE();
    ApplyBoneModifier3DBaseUVE(entityManager, bone, "LookAtModifier3D");
    ApplyPhysicsObject3DBaseUVE(entityManager, physics, "Area3D");
    ApplyRenderInstance3DBaseUVE(entityManager, render, "MeshInstance3D");
    ExpectObject3DBaselineUVE(entityManager, bone, "LookAtModifier3D");
    ExpectObject3DBaselineUVE(entityManager, physics, "Area3D");
    ExpectObject3DBaselineUVE(entityManager, render, "MeshInstance3D");
    for (const EntityUVE entity : {bone, physics, render}) {
        EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<ProcessComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<ObjectMetadataComponentUVE>(entity));
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

TEST_F(Object3DDefinitionsUVETest, AbstractBaseComponentsRejectValuesTheySaveBadly) {
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

TEST_F(Object3DDefinitionsUVETest, RenderInstanceChildBasesCarryRenderInstanceAndTheirOwnComponent) {
    const EntityUVE surface = CreateEntityUVE();
    const EntityUVE light = CreateEntityUVE();
    ApplySurfaceInstance3DBaseUVE(entityManager, surface, "MeshInstance3D");
    ApplyLightEmitter3DBaseUVE(entityManager, light, "OmniLight3D");
    ExpectObject3DBaselineUVE(entityManager, surface, "MeshInstance3D");
    ExpectObject3DBaselineUVE(entityManager, light, "OmniLight3D");
    for (const EntityUVE entity : {surface, light}) {
        EXPECT_TRUE(entityManager.HasComponentUVE<RenderInstanceComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<ObjectMetadataComponentUVE>(entity));
    }
    EXPECT_TRUE(entityManager.HasComponentUVE<SurfaceInstanceComponentUVE>(surface));
    EXPECT_FALSE(entityManager.HasComponentUVE<LightEmitterComponentUVE>(surface));
    EXPECT_TRUE(entityManager.HasComponentUVE<LightEmitterComponentUVE>(light));
    EXPECT_FALSE(entityManager.HasComponentUVE<SurfaceInstanceComponentUVE>(light));
}

TEST_F(Object3DDefinitionsUVETest, MeshAndParticleObjectsAreSurfaceInstances) {
    const EntityUVE mesh = CreateEntityUVE();
    const EntityUVE box = CreateEntityUVE();
    const EntityUVE particles = CreateEntityUVE();
    ApplyMeshInstance3DObjectDefinitionUVE(entityManager, mesh, MeshInstance3DObjectDefinitionUVE{});
    ApplyBoxMesh3DObjectDefinitionUVE(entityManager, box, BoxMesh3DObjectDefinitionUVE{});
    ApplyParticleEmitter3DObjectDefinitionUVE(entityManager, particles, ParticleEmitter3DObjectDefinitionUVE{});
    ExpectObject3DBaselineUVE(entityManager, mesh, "MeshInstance3D");
    ExpectObject3DBaselineUVE(entityManager, box, "Cube");
    ExpectObject3DBaselineUVE(entityManager, particles, "ParticleEmitter3D");
    for (const EntityUVE entity : {mesh, box, particles}) {
        EXPECT_TRUE(entityManager.HasComponentUVE<SurfaceInstanceComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<RenderInstanceComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));
        EXPECT_TRUE(entityManager.HasComponentUVE<ObjectMetadataComponentUVE>(entity));
    }
}

TEST_F(Object3DDefinitionsUVETest, Decal3DAndFogVolume3DAreRenderInstancesPlusTheirOwnComponent) {
    const EntityUVE decal = CreateEntityUVE();
    const EntityUVE fog = CreateEntityUVE();
    Decal3DObjectDefinitionUVE decalDefinition{};
    decalDefinition.decal.albedoMix = 0.25F;
    ApplyDecal3DObjectDefinitionUVE(entityManager, decal, decalDefinition);
    ApplyFogVolume3DObjectDefinitionUVE(entityManager, fog, FogVolume3DObjectDefinitionUVE{});
    ExpectObject3DBaselineUVE(entityManager, decal, "Decal3D");
    ExpectObject3DBaselineUVE(entityManager, fog, "FogVolume3D");
    for (const EntityUVE entity : {decal, fog}) {
        EXPECT_TRUE(entityManager.HasComponentUVE<RenderInstanceComponentUVE>(entity));
        EXPECT_FALSE(entityManager.HasComponentUVE<SurfaceInstanceComponentUVE>(entity));
        EXPECT_FALSE(entityManager.HasComponentUVE<LightEmitterComponentUVE>(entity));
    }
    EXPECT_EQ(entityManager.GetComponentUVE<Decal3DComponentUVE>(decal).albedoMix, 0.25F);
    EXPECT_EQ(entityManager.GetComponentUVE<FogVolume3DComponentUVE>(fog), FogVolume3DComponentUVE{});
    // A second apply keeps what was authored.
    entityManager.GetComponentUVE<FogVolume3DComponentUVE>(fog).density = -0.5F;
    ApplyFogVolume3DObjectDefinitionUVE(entityManager, fog, FogVolume3DObjectDefinitionUVE{});
    EXPECT_EQ(entityManager.GetComponentUVE<FogVolume3DComponentUVE>(fog).density, -0.5F);
}

TEST_F(Object3DDefinitionsUVETest, RenderInstanceFamilyComponentsRejectValuesTheySaveBadly) {
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

    EXPECT_TRUE(IsDecal3DObjectComponentValidUVE(Decal3DComponentUVE{}));
    Decal3DComponentUVE decal{};
    decal.albedoMix = -0.1F;
    EXPECT_FALSE(IsDecal3DObjectComponentValidUVE(decal));

    EXPECT_TRUE(IsFogVolume3DObjectComponentValidUVE(FogVolume3DComponentUVE{}));
    FogVolume3DComponentUVE fog{};
    fog.density = -2.0F; // Negative density is allowed: it clears fog.
    EXPECT_TRUE(IsFogVolume3DObjectComponentValidUVE(fog));
    fog.size.y = 0.0F;
    EXPECT_FALSE(IsFogVolume3DObjectComponentValidUVE(fog));
    fog = {};
    fog.shape = static_cast<FogVolumeShapeUVE>(9);
    EXPECT_FALSE(IsFogVolume3DObjectComponentValidUVE(fog));
}

TEST_F(Object3DDefinitionsUVETest, Skeleton3DIsAObject3DChildThatStartsWithNoBones) {
    const EntityUVE skeleton = CreateEntityUVE();
    ApplySkeleton3DObjectDefinitionUVE(entityManager, skeleton, Skeleton3DObjectDefinitionUVE{});
    ExpectObject3DBaselineUVE(entityManager, skeleton, "Skeleton3D");
    EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(skeleton));
    EXPECT_TRUE(entityManager.HasComponentUVE<ObjectMetadataComponentUVE>(skeleton));
    EXPECT_TRUE(entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeleton).bones.empty());
}

TEST_F(Object3DDefinitionsUVETest, Object3DApplyIsIdempotent) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyObject3DObjectDefinitionUVE(entityManager, entity, Object3DObjectDefinitionUVE{});
    entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition =
        Math::Vector3UVE{1.0F, 2.0F, 3.0F};

    ApplyObject3DObjectDefinitionUVE(entityManager, entity, Object3DObjectDefinitionUVE{});
    EXPECT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition.y, 2.0F);
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(entity).name,
              Object3DObjectDefinitionUVE::defaultName);
}

TEST_F(Object3DDefinitionsUVETest, Object3DApplyRefusesADestroyedEntity) {
    const EntityUVE entity = CreateEntityUVE();
    entityManager.DestroyEntityUVE(entity);
    // Same refusal as the scene-root apply: dead entities get nothing, and nothing crashes.
    ApplyObject3DObjectDefinitionUVE(entityManager, entity, Object3DObjectDefinitionUVE{});
    EXPECT_FALSE(entityManager.IsAliveUVE(entity));
}

TEST_F(Object3DDefinitionsUVETest, SceneRootIsAPureObjectCarryingTheCommonObjectSection) {
    const EntityUVE root = CreateEntityUVE();
    ApplySceneRootObjectDefinitionUVE(entityManager, root, SceneRootObjectDefinitionUVE{});

    // In the hierarchy and named, but with no transform: placing things in space is what Object3D
    // adds, and the root has nothing to place. Its children start their own transform chains.
    EXPECT_FALSE(entityManager.HasComponentUVE<TransformComponentUVE>(root));
    EXPECT_FALSE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<HierarchyComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<NameComponentUVE>(root));
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(root).name,
              SceneRootObjectDefinitionUVE::defaultName);
    EXPECT_TRUE(entityManager.HasComponentUVE<SceneRootComponentUVE>(root));

    // The root's Inspector offers no Add Component, so the whole common Object section is attached
    // here - anything left out could never be reached from the editor.
    EXPECT_TRUE(entityManager.HasComponentUVE<ProcessComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<ThreadGroupComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsInterpolationComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<AutoTranslateComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<EditorDescriptionComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<ScriptComponentUVE>(root));
    EXPECT_TRUE(entityManager.HasComponentUVE<ObjectMetadataComponentUVE>(root));

    // And it is still the idempotent recipe the document lifecycle relies on: applying twice
    // adds nothing and changes nothing, including an authored value.
    entityManager.GetComponentUVE<ProcessComponentUVE>(root).priority = 7;
    ApplySceneRootObjectDefinitionUVE(entityManager, root, SceneRootObjectDefinitionUVE{});
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(root).name,
              SceneRootObjectDefinitionUVE::defaultName);
    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(root).priority, 7);
    EXPECT_FALSE(entityManager.HasComponentUVE<TransformComponentUVE>(root));
}

TEST_F(Object3DDefinitionsUVETest, SceneRootMigrationBakesAnOldRootTransformIntoItsChildren) {
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

    ApplySceneRootObjectDefinitionUVE(entityManager, root, SceneRootObjectDefinitionUVE{});
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
    ApplySceneRootObjectDefinitionUVE(entityManager, root, SceneRootObjectDefinitionUVE{});
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_NEAR(entityManager.GetComponentUVE<WorldTransformComponentUVE>(child).worldPosition.x,
                childBefore.worldPosition.x, kTolerance);
}

TEST_F(Object3DDefinitionsUVETest, SpringArm3DIsRegisteredCreatableAsACameraObject) {
    // The registry row and the editor switch must keep agreeing about this kind: the registry
    // advertises it as a creatable cameran object, and the switch now creates it from the same
    // definition this test file pins.
    const Objects::SceneObjectDescriptorUVE* descriptor =
        Objects::FindSceneObjectDescriptorUVE(Objects::SceneObjectKindUVE::SpringArm3D);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->typeId, "spring_arm_3d");
    EXPECT_EQ(descriptor->displayName, "SpringArm3D");
    EXPECT_TRUE(descriptor->libraryCreatable);
}

TEST_F(Object3DDefinitionsUVETest, SpringArmTargetResolutionMatchesTheAuthoredEnvelope) {
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

TEST_F(Object3DDefinitionsUVETest, SpringArmRetractionSnapsSoTheCameraNeverClips) {
    // Obstruction appears mid-frame: the arm arrives at the target THIS step, not after a
    // smooth glide through the wall.
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(4.0F, 1.4F, 8.0F, 1.0F / 60.0F), 1.4F);
    // Already retracted, target deeper still: still snaps.
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(2.0F, 1.4F, 8.0F, 1.0F / 60.0F), 1.4F);
}

TEST_F(Object3DDefinitionsUVETest, SpringArmExtensionBlendsMonotonicallyAndNeverOvershoots) {
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

TEST_F(Object3DDefinitionsUVETest, SpringArmSmoothingZeroMatchesGodotSnapBothWays) {
    // The authored escape hatch: smoothing 0 reproduces Godot's SpringArm3D behaviour exactly
    // (Godot ships no smoothing member at all - snap on the way out as well).
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(1.4F, 4.0F, 0.0F, 1.0F / 60.0F), 4.0F);
}

TEST_F(Object3DDefinitionsUVETest, SpringArmLengthRefusesDegenerateCallsWithoutMoving) {
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(2.0F, 4.0F, 8.0F, 0.0F), 2.0F);   // dt 0: frozen
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(2.0F, 4.0F, 8.0F, -1.0F), 2.0F);  // dt negative: frozen
    // A NaN input stays the caller's (the validator's) problem: the law returns its own current
    // length unchanged rather than smearing garbage through the blend. NaN never compares equal,
    // so these check the value category directly, not EXPECT_EQ.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(std::isnan(ResolveSpringArm3DLengthUVE(nan, 4.0F, 8.0F, 1.0F / 60.0F)));
    EXPECT_EQ(ResolveSpringArm3DLengthUVE(2.0F, nan, 8.0F, 1.0F / 60.0F), 2.0F);
}

TEST_F(Object3DDefinitionsUVETest, SpringArmObstructThenClearRestoresTheAuthoredPose) {
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

TEST_F(Object3DDefinitionsUVETest, SpawnPointSelectionIsDeterministicContentOrder) {
    // No candidates, no spawn.
    EXPECT_EQ(ResolveSpawnPoint3DSelectionUVE(std::span<const SpawnPoint3DCandidateUVE>{}),
              std::nullopt);

    // The predicate the resolver and the spawn query's result order both read: ascending
    // (index, generation), strictly, so equal handles never re-order each other.
    EXPECT_TRUE(SortsBeforeSpawnPointUVE(EntityUVE{1U, 0U}, EntityUVE{2U, 0U}));
    EXPECT_TRUE(SortsBeforeSpawnPointUVE(EntityUVE{1U, 0U}, EntityUVE{1U, 1U}));
    EXPECT_FALSE(SortsBeforeSpawnPointUVE(EntityUVE{1U, 1U}, EntityUVE{1U, 0U}));
    EXPECT_FALSE(SortsBeforeSpawnPointUVE(EntityUVE{2U, 0U}, EntityUVE{1U, 9U}));
    EXPECT_FALSE(SortsBeforeSpawnPointUVE(EntityUVE{3U, 2U}, EntityUVE{3U, 2U}));
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

TEST_F(Object3DDefinitionsUVETest, SpawnPoseComposeAppliesTheAuthoredOffsetInObjectSpace) {
    // Identity object: the offset is the pose.
    const std::optional<SpawnPoint3DPoseUVE> flat =
        ComposeSpawnPointPoseUVE({}, {}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, {});
    ASSERT_TRUE(flat.has_value());
    EXPECT_NEAR(flat->position.y, 1.0F, 1.0e-6F);

    // Translated object: object position adds after the offset is rotated.
    const Math::QuaternionUVE halfTurnAboutY{0.0F, 1.0F, 0.0F, 0.0F};
    const std::optional<SpawnPoint3DPoseUVE> posed = ComposeSpawnPointPoseUVE(
        Math::Vector3UVE{10.0F, 0.0F, 10.0F}, halfTurnAboutY, Math::Vector3UVE{1.0F, 0.0F, 0.0F},
        {});
    ASSERT_TRUE(posed.has_value());
    // 180 degrees about Y maps (1,0,0) to (-1,0,0), then the object position adds.
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

TEST_F(Object3DDefinitionsUVETest, SpawnPlayerLocalIsTheSweepInverse) {
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

TEST_F(Object3DDefinitionsUVETest, InteractionAreaCandidateCapHonoursAuthoredBudgetAndStorageBound) {
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

TEST_F(Object3DDefinitionsUVETest, PrimaryInteractorSelectionMatchesTheSpawnPointPlayerRule) {
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

TEST_F(Object3DDefinitionsUVETest, InteractionFocusPicksTheNearestAreaWithDeterministicTies) {
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

TEST_F(Object3DDefinitionsUVETest, MarkerPoseComposeSharesTheSpawnPointCompositionContract) {
    // The pure half of fly-to-marker: the same measured composition contract the spawn point
    // owns - position = object position + object rotation * authored offset, rotation composes,
    // object scale stays out of it - so a marker's viewpoint tracks prefab-level transforms
    // identically to every other authored-offset object in the engine.
    const std::optional<Marker3DPoseUVE> flat =
        ComposeMarker3DPoseUVE({}, {}, Math::Vector3UVE{0.0F, 0.0F, -4.0F}, {});
    ASSERT_TRUE(flat.has_value());
    EXPECT_NEAR(flat->position.z, -4.0F, 1.0e-6F);

    const Math::QuaternionUVE halfTurnAboutY{0.0F, 1.0F, 0.0F, 0.0F};
    const std::optional<Marker3DPoseUVE> posed = ComposeMarker3DPoseUVE(
        Math::Vector3UVE{10.0F, 0.0F, 10.0F}, halfTurnAboutY,
        Math::Vector3UVE{1.0F, 0.0F, 0.0F}, {});
    ASSERT_TRUE(posed.has_value());
    // 180 degrees about Y maps (1,0,0) to (-1,0,0), then the object position adds.
    EXPECT_NEAR(posed->position.x, 9.0F, 1.0e-5F);
    EXPECT_NEAR(posed->position.z, 10.0F, 1.0e-5F);
    EXPECT_NEAR(posed->rotation.y, 1.0F, 1.0e-5F);
    EXPECT_NEAR(posed->rotation.w, 0.0F, 1.0e-5F);

    // A rotated marker composes its facing under the object's rotation, not around it.
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

TEST_F(Object3DDefinitionsUVETest, AnimationGraphIsCreatableAndValidatesItsBlend) {
    EXPECT_TRUE(IsAnimationGraphObjectDefinitionValidUVE(AnimationGraphObjectDefinitionUVE{}));
    AnimationGraphObjectDefinitionUVE noOutput;
    noOutput.tree.nodes.erase(noOutput.tree.nodes.begin());
    EXPECT_FALSE(IsAnimationGraphObjectDefinitionValidUVE(noOutput));
    const Objects::SceneObjectDescriptorUVE* descriptor =
        Objects::FindSceneObjectDescriptorUVE(Objects::SceneObjectKindUVE::AnimationGraph);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_TRUE(descriptor->libraryCreatable);
}

TEST_F(Object3DDefinitionsUVETest, CameraCurrentIsExclusiveAndSkipsEditorInternal) {
    const EntityUVE first = CreateEntityUVE();
    ApplyCamera3DObjectDefinitionUVE(entityManager, first, Camera3DObjectDefinitionUVE{});
    const EntityUVE second = CreateEntityUVE();
    ApplyCamera3DObjectDefinitionUVE(entityManager, second, Camera3DObjectDefinitionUVE{});
    EXPECT_TRUE(entityManager.GetComponentUVE<CameraComponentUVE>(first).current);
    EXPECT_FALSE(entityManager.GetComponentUVE<CameraComponentUVE>(second).current);
    ASSERT_TRUE(FindCurrentCameraEntityUVE(entityManager).has_value());
    EXPECT_EQ(*FindCurrentCameraEntityUVE(entityManager), first);

    MakeCameraCurrentUVE(entityManager, second);
    EXPECT_FALSE(entityManager.GetComponentUVE<CameraComponentUVE>(first).current);
    EXPECT_TRUE(entityManager.GetComponentUVE<CameraComponentUVE>(second).current);
    ASSERT_TRUE(FindCurrentCameraEntityUVE(entityManager).has_value());
    EXPECT_EQ(*FindCurrentCameraEntityUVE(entityManager), second);

    const EntityUVE internal = CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, internal, TransformComponentUVE{});
    entityManager.AddComponentUVE<CameraComponentUVE>(internal);
    entityManager.AddComponentUVE<EditorInternalEntityComponentUVE>(internal);
    ASSERT_TRUE(FindCurrentCameraEntityUVE(entityManager).has_value());
    EXPECT_EQ(*FindCurrentCameraEntityUVE(entityManager), second);
    EXPECT_FALSE(IsDocumentCameraEntityUVE(entityManager, internal));
}

} // namespace
// =================================================================================================
// Participation: what a PhysicsObject3D's Process mode and disable mode mean to the physics world.
// =================================================================================================

class PhysicsObjectParticipationUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    /// A body-shaped entity with a transform, a PhysicsObject component and a Process component
    /// whose mode the scene graph resolves the way the engine resolves it every frame.
    [[nodiscard]] Scene::EntityUVE MakeObjectUVE(const Scene::TickModeUVE mode,
                                                 const PhysicsObjectDisableModeUVE disableMode) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, Scene::TransformComponentUVE{});
        sceneGraph.UpdateUVE(entityManager);
        Scene::ProcessComponentUVE process{};
        process.mode = mode;
        entityManager.AddComponentUVE<Scene::ProcessComponentUVE>(entity, process);
        PhysicsObjectComponentUVE object{};
        object.disableMode = disableMode;
        entityManager.AddComponentUVE<PhysicsObjectComponentUVE>(entity, object);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }
};

TEST_F(Object3DDefinitionsUVETest, ARigid3DBodyKeepsAColliderItWasGivenAndGetsOneWhenItHasNone) {
    // The recipe only fills in what is missing, so a body whose collider was authored - a layer, a
    // capsule, half extents - keeps every value when the definition is applied over it.
    const EntityUVE authored = entityManager.CreateEntityUVE();
    ColliderComponentUVE shape{};
    shape.shapeType = ColliderShapeTypeUVE::Capsule;
    shape.radius = 0.25F;
    shape.height = 1.0F;
    shape.collisionLayer = 4U;
    entityManager.AddComponentUVE<ColliderComponentUVE>(authored, shape);
    ApplyRigid3DObjectDefinitionUVE(entityManager, authored, Rigid3DObjectDefinitionUVE{});
    const ColliderComponentUVE& kept = entityManager.GetComponentUVE<ColliderComponentUVE>(authored);
    EXPECT_EQ(kept.shapeType, ColliderShapeTypeUVE::Capsule);
    EXPECT_FLOAT_EQ(kept.radius, 0.25F);
    EXPECT_EQ(kept.collisionLayer, 4U);

    // A wrong collider is a wrong definition: the validator refuses it rather than letting the
    // edit create a body that can never be collided with.
    Rigid3DObjectDefinitionUVE invalid{};
    invalid.collider.collisionLayer = 0U;
    EXPECT_FALSE(IsRigid3DObjectDefinitionValidUVE(invalid));
}

TEST_F(Object3DDefinitionsUVETest, AFrozenRigid3DIsOutOfTheSimulationWithoutBeingDeleted) {
    // The reason a Rigid3D carries the physics object base: its Process mode and disable mode
    // answer the question a game keeps asking - "stop this body, but keep it in the scene".
    const EntityUVE entity = CreateEntityUVE();
    ApplyRigid3DObjectDefinitionUVE(entityManager, entity, Rigid3DObjectDefinitionUVE{});

    // Running (the default) - simulated, in the world, and colliding like anything else.
    EXPECT_TRUE(IsPhysicsObjectSimulatedUVE(entityManager, entity));
    EXPECT_TRUE(IsPhysicsObjectInWorldUVE(entityManager, entity));

    // The Object3D baseline already attached the Process component: the object's schedule is
    // authored on it, not re-added on top of it.
    ASSERT_TRUE(entityManager.HasComponentUVE<ProcessComponentUVE>(entity));
    entityManager.GetComponentUVE<ProcessComponentUVE>(entity).mode = TickModeUVE::Never;
    sceneGraph.UpdateUVE(entityManager);

    // Default disable mode is Remove: out of the world entirely, which the collider cache, every
    // query and the simulation all honour through the one rule.
    EXPECT_FALSE(IsPhysicsObjectSimulatedUVE(entityManager, entity));
    EXPECT_FALSE(IsPhysicsObjectInWorldUVE(entityManager, entity));

    // Authored as MakeStatic instead, it stays in the world as an obstacle nothing can move.
    entityManager.GetComponentUVE<PhysicsObjectComponentUVE>(entity).disableMode =
        PhysicsObjectDisableModeUVE::MakeStatic;
    EXPECT_FALSE(IsPhysicsObjectSimulatedUVE(entityManager, entity));
    EXPECT_TRUE(IsPhysicsObjectInWorldUVE(entityManager, entity));
}
TEST_F(Object3DDefinitionsUVETest, BodyKindsWithShapesAreToldApartByTheirControllerNotTheirCollider) {
    // A Rigid3D and a Character3D are both a collider plus a body. The type has to come from the
    // controller: a body with a shape and no controller is a Rigid3D, and one with it is a
    // character.
    const EntityUVE rigid = entityManager.CreateEntityUVE();
    ApplyRigid3DObjectDefinitionUVE(entityManager, rigid, Rigid3DObjectDefinitionUVE{});
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, rigid), Objects::SceneObjectKindUVE::Rigid3D);

    const EntityUVE character = entityManager.CreateEntityUVE();
    ApplyCharacter3DObjectDefinitionUVE(entityManager, character, Character3DObjectDefinitionUVE{});
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, character), Objects::SceneObjectKindUVE::Character3D);

    const EntityUVE player = entityManager.CreateEntityUVE();
    ApplyPlayer3DObjectDefinitionUVE(entityManager, player, Player3DObjectDefinitionUVE{});
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, player), Objects::SceneObjectKindUVE::Player3D);
}

TEST_F(PhysicsObjectParticipationUVETest, AnEntityThatIsNotAPhysicsObjectIsLeftAlone) {
    // Nothing here says it should stop, and a body assembled by hand must not be quietly taken out
    // of the world by a system that was looking for a component that is not there.
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::ProcessComponentUVE never{};
    never.mode = Scene::TickModeUVE::Never;
    entityManager.AddComponentUVE<Scene::ProcessComponentUVE>(entity, never);

    EXPECT_EQ(ResolvePhysicsObjectParticipationUVE(entityManager, entity, false),
              PhysicsObjectParticipationUVE::Active);
    EXPECT_TRUE(IsPhysicsObjectInWorldUVE(entityManager, entity));
    EXPECT_TRUE(IsPhysicsObjectSimulatedUVE(entityManager, entity));
}

TEST_F(PhysicsObjectParticipationUVETest, ARunningObjectParticipatesWhateverItsDisableModeSays) {
    // The disable mode is what happens while it is *not* running; an object that is running is
    // running, and authoring KeepActive must not make it immortal either.
    for (const PhysicsObjectDisableModeUVE disableMode :
         {PhysicsObjectDisableModeUVE::Remove, PhysicsObjectDisableModeUVE::MakeStatic,
          PhysicsObjectDisableModeUVE::KeepActive}) {
        const Scene::EntityUVE entity = MakeObjectUVE(Scene::TickModeUVE::Running, disableMode);
        EXPECT_EQ(ResolvePhysicsObjectParticipationUVE(entityManager, entity, false),
                  PhysicsObjectParticipationUVE::Active);
    }
}

TEST_F(PhysicsObjectParticipationUVETest, AStoppedObjectIsTakenOutKeptAsAStaticOrLeftRunning) {
    const Scene::EntityUVE removed = MakeObjectUVE(Scene::TickModeUVE::Never, PhysicsObjectDisableModeUVE::Remove);
    EXPECT_EQ(ResolvePhysicsObjectParticipationUVE(entityManager, removed, false),
              PhysicsObjectParticipationUVE::Removed);
    EXPECT_FALSE(IsPhysicsObjectInWorldUVE(entityManager, removed));
    EXPECT_FALSE(IsPhysicsObjectSimulatedUVE(entityManager, removed));

    // Kept in the world as an immovable obstacle: still found, still in the way, never moved.
    const Scene::EntityUVE kept =
        MakeObjectUVE(Scene::TickModeUVE::Never, PhysicsObjectDisableModeUVE::MakeStatic);
    EXPECT_EQ(ResolvePhysicsObjectParticipationUVE(entityManager, kept, false),
              PhysicsObjectParticipationUVE::StaticOnly);
    EXPECT_TRUE(IsPhysicsObjectInWorldUVE(entityManager, kept));
    EXPECT_FALSE(IsPhysicsObjectSimulatedUVE(entityManager, kept));

    // KeepActive is the author saying "this one keeps simulating while it is stopped" - the mode
    // exists so a scripted body can outlive its own schedule.
    const Scene::EntityUVE active =
        MakeObjectUVE(Scene::TickModeUVE::Never, PhysicsObjectDisableModeUVE::KeepActive);
    EXPECT_EQ(ResolvePhysicsObjectParticipationUVE(entityManager, active, false),
              PhysicsObjectParticipationUVE::Active);
}

TEST_F(PhysicsObjectParticipationUVETest, ThePauseAnswerIsTheCallersAndTheFixedStepAsksWithFalse) {
    // A pause-only object is out of the world for the fixed step, which does not run while the
    // simulation is paused, and in it for anything asking on behalf of a paused world.
    const Scene::EntityUVE entity =
        MakeObjectUVE(Scene::TickModeUVE::PausedOnly, PhysicsObjectDisableModeUVE::Remove);

    EXPECT_EQ(ResolvePhysicsObjectParticipationUVE(entityManager, entity, /*simulationPaused=*/true),
              PhysicsObjectParticipationUVE::Active);
    EXPECT_EQ(ResolvePhysicsObjectParticipationUVE(entityManager, entity, /*simulationPaused=*/false),
              PhysicsObjectParticipationUVE::Removed);

    // The two convenience questions always speak for the physics step, so a paused body is not
    // simulated by it either way.
    EXPECT_FALSE(IsPhysicsObjectInWorldUVE(entityManager, entity));
    EXPECT_FALSE(IsPhysicsObjectSimulatedUVE(entityManager, entity));
}

TEST_F(PhysicsObjectParticipationUVETest, CollisionPriorityDefaultsToOneAndZeroMeansNeverYields) {
    const Scene::EntityUVE plain = entityManager.CreateEntityUVE();
    // No component at all is the neutral answer, not an error: everything that is not a physics
    // object weighs exactly as much as one authored with the defaults.
    EXPECT_FLOAT_EQ(GetPhysicsObjectCollisionPriorityUVE(entityManager, plain), 1.0F);
    EXPECT_FLOAT_EQ(GetPhysicsObjectYieldWeightUVE(entityManager, plain, 0.5F), 0.5F);

    const Scene::EntityUVE heavy = MakeObjectUVE(Scene::TickModeUVE::Running, PhysicsObjectDisableModeUVE::Remove);
    entityManager.GetComponentUVE<PhysicsObjectComponentUVE>(heavy).collisionPriority = 2.0F;
    // Twice the priority yields half as much of the same overlap.
    EXPECT_FLOAT_EQ(GetPhysicsObjectCollisionPriorityUVE(entityManager, heavy), 2.0F);
    EXPECT_FLOAT_EQ(GetPhysicsObjectYieldWeightUVE(entityManager, heavy, 0.5F), 0.25F);

    // Zero is the authored "pinned in place": legal, and worth exactly no correction.
    entityManager.GetComponentUVE<PhysicsObjectComponentUVE>(heavy).collisionPriority = 0.0F;
    EXPECT_FLOAT_EQ(GetPhysicsObjectYieldWeightUVE(entityManager, heavy, 0.5F), 0.0F);

    // A nonsense priority falls back to the neutral one rather than to something the solver would
    // have to special-case, and a body with no mass to give way with yields nothing whatever its
    // priority says.
    entityManager.GetComponentUVE<PhysicsObjectComponentUVE>(heavy).collisionPriority = -4.0F;
    EXPECT_FLOAT_EQ(GetPhysicsObjectCollisionPriorityUVE(entityManager, heavy), 1.0F);
    EXPECT_FLOAT_EQ(GetPhysicsObjectYieldWeightUVE(entityManager, heavy, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(GetPhysicsObjectYieldWeightUVE(entityManager, heavy, -1.0F), 0.0F);
}

TEST_F(PhysicsObjectParticipationUVETest, TheAbstractObjectDefinitionsStillAttachTheirComponents) {
    // The recipes and the rule live in one file, and the rule must not have replaced the recipes:
    // SolidBody3D is still the recipe that gives a body its physics object base, and an entity
    // built from it is Active until something says otherwise.
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    ApplySolidBody3DBaseUVE(entityManager, entity, "SolidBody3D");
    EXPECT_TRUE(entityManager.HasComponentUVE<PhysicsObjectComponentUVE>(entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<SolidBodyComponentUVE>(entity));
    EXPECT_EQ(ResolvePhysicsObjectParticipationUVE(entityManager, entity, false),
              PhysicsObjectParticipationUVE::Active);
}

TEST(ObjectDefinitions3DUVETest, PrimitiveCollidersMatchTheKindTheyBelongTo) {
    // The editor converts a primitive in place (Cube -> Plane and so on) and refreshes the
    // collider to match the new kind. It reads these same definitions to do it, so this pins the
    // pairing the conversion depends on: each primitive kind's authored collider must be the one
    // its own definition declares, and a flat primitive must not inherit a cube's depth.
    EXPECT_EQ(BoxMesh3DObjectDefinitionUVE{}.mesh.kind, PrimitiveMeshKindUVE::Cube);
    EXPECT_EQ(SphereMesh3DObjectDefinitionUVE{}.mesh.kind, PrimitiveMeshKindUVE::UVSphere);
    EXPECT_EQ(PlaneMesh3DObjectDefinitionUVE{}.mesh.kind, PrimitiveMeshKindUVE::Plane);

    // The plane is the one that actually differs, and the one a duplicated constant would get
    // wrong: it is thin on Y where the volumetric primitives are not.
    const Math::Vector3UVE planeExtents = PlaneMesh3DObjectDefinitionUVE{}.collider.halfExtents;
    const Math::Vector3UVE boxExtents = BoxMesh3DObjectDefinitionUVE{}.collider.halfExtents;
    EXPECT_LT(planeExtents.y, boxExtents.y) << "a plane's collider must be flatter than a cube's";
    EXPECT_FLOAT_EQ(planeExtents.x, boxExtents.x);
    EXPECT_FLOAT_EQ(planeExtents.z, boxExtents.z);

    // Every primitive definition must carry a collider that passes validation - an unauthored or
    // zeroed one would make the created entity fail IsColliderComponentValidUVE downstream.
    EXPECT_TRUE(IsBoxMesh3DObjectDefinitionValidUVE(BoxMesh3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsSphereMesh3DObjectDefinitionValidUVE(SphereMesh3DObjectDefinitionUVE{}));
    EXPECT_TRUE(IsPlaneMesh3DObjectDefinitionValidUVE(PlaneMesh3DObjectDefinitionUVE{}));
}

} // namespace UVE::Scene::Tests
