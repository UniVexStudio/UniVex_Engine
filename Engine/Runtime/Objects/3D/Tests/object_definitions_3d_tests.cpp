// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include "auto_translate_component.h"
#include "bone_modifier_component.h"
#include "physics_object_component.h"
#include "render_instance_component.h"
#include "light_emitter_component.h"
#include "character_controller_component.h"
#include "solid_body_component.h"
#include "surface_instance_component.h"
#include "camera_component.h"
#include "collider_component.h"
#include "disabled_component.h"
#include "editor_description_component.h"
#include "editor_internal_entity_component.h"
#include "entity.h"
#include "hierarchy_component.h"
#include "light_component.h"
#include "name_component.h"
#include "object_metadata_component.h"
#include "physics_interpolation_component.h"
#include "primitive_mesh_component.h"
#include "process_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "thread_group_component.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "aabb.h"
#include "quaternion.h"
#include "memory_manager.h"
#include "all_objects_3d.h"
#include "collider_3d.h"
#include "pawn.h"
#include "scene_object_registry.h"
#include "scene_object_type.h"
#include "scene_root.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

// Every one of the 18 component-backed object kinds' definition is reachable from the aggregate
// header, mirroring the registry test's guarantee for the 21 data-carrying object components:
// no object kind's home file can be silently dropped without breaking this compile.
static_assert(std::is_class_v<Object3DObjectDefinition>);            // Object3D
static_assert(std::is_class_v<Area3DObjectDefinition>);           // Area3D
static_assert(std::is_class_v<Static3DObjectDefinition>);     // Static3D
static_assert(std::is_class_v<Character3DObjectDefinition>);  // Character3D
static_assert(std::is_class_v<Player3DObjectDefinition>);     // Player3D
static_assert(std::is_class_v<Camera3DObjectDefinition>);         // Camera3D
static_assert(std::is_class_v<MeshInstance3DObjectDefinition>);   // MeshInstance3D
static_assert(std::is_class_v<BoxMesh3DObjectDefinition>);        // BoxMesh3D
static_assert(std::is_class_v<SphereMesh3DObjectDefinition>);     // SphereMesh3D
static_assert(std::is_class_v<PlaneMesh3DObjectDefinition>);      // PlaneMesh3D
static_assert(std::is_class_v<Light3DObjectDefinition>);          // Light3D
static_assert(std::is_class_v<Collider3DObjectDefinition>);       // Collider3D
static_assert(std::is_class_v<Rigid3DObjectDefinition>);      // Rigid3D
static_assert(std::is_class_v<AudioSource3DObjectDefinition>);        // AudioSource3D
static_assert(std::is_class_v<ParticleEmitter3DObjectDefinition>); // ParticleEmitter3D
static_assert(std::is_class_v<ScriptObjectDefinition>);           // Script
static_assert(std::is_class_v<SpringArm3DObjectDefinition>);      // SpringArm3D
static_assert(std::is_class_v<AnimationSequencerObjectDefinition>);  // AnimationSequencer
static_assert(std::is_class_v<AnimationGraphObjectDefinition>);    // AnimationGraph

class Object3DDefinitionsTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    [[nodiscard]] Entity CreateEntity() {
        return entityManager.CreateEntity();
    }
};

// Every 3D scene-object recipe stands on the Object3D baseline; asserting it once per kind (instead
// of re-typing four component checks in every block) pins the composition, not just its effect.
void ExpectObject3DBaseline(EntityManager& entityManager, const Entity entity,
                             const std::string_view expectedName) {
    EXPECT_TRUE(entityManager.HasComponent<TransformComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<WorldTransformComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<HierarchyComponent>(entity));
    ASSERT_TRUE(entityManager.HasComponent<NameComponent>(entity));
    EXPECT_EQ(entityManager.GetComponent<NameComponent>(entity).name, expectedName);
}

/// A pure Object: in the hierarchy and named, with the Object section, and nothing spatial.
void ExpectPureObject(EntityManager& entityManager, const Entity entity, const std::string_view expectedName) {
    EXPECT_FALSE(entityManager.HasComponent<TransformComponent>(entity));
    EXPECT_FALSE(entityManager.HasComponent<WorldTransformComponent>(entity));
    EXPECT_FALSE(entityManager.HasComponent<VisibilityComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<HierarchyComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<ProcessComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<ObjectMetadataComponent>(entity));
    ASSERT_TRUE(entityManager.HasComponent<NameComponent>(entity));
    EXPECT_EQ(entityManager.GetComponent<NameComponent>(entity).name, expectedName);
}

TEST_F(Object3DDefinitionsTest, AllDefinitionDefaultsAreValid) {
    EXPECT_TRUE(IsObject3DObjectDefinitionValid(Object3DObjectDefinition{}));
    EXPECT_TRUE(IsArea3DObjectDefinitionValid(Area3DObjectDefinition{}));
    EXPECT_TRUE(IsStatic3DObjectDefinitionValid(Static3DObjectDefinition{}));
    EXPECT_TRUE(IsCharacter3DObjectDefinitionValid(Character3DObjectDefinition{}));
    EXPECT_TRUE(IsPlayer3DObjectDefinitionValid(Player3DObjectDefinition{}));
    EXPECT_TRUE(IsCamera3DObjectDefinitionValid(Camera3DObjectDefinition{}));
    EXPECT_TRUE(IsMeshInstance3DObjectDefinitionValid(MeshInstance3DObjectDefinition{}));
    EXPECT_TRUE(IsBoxMesh3DObjectDefinitionValid(BoxMesh3DObjectDefinition{}));
    EXPECT_TRUE(IsSphereMesh3DObjectDefinitionValid(SphereMesh3DObjectDefinition{}));
    EXPECT_TRUE(IsPlaneMesh3DObjectDefinitionValid(PlaneMesh3DObjectDefinition{}));
    EXPECT_TRUE(IsLight3DObjectDefinitionValid(Light3DObjectDefinition{}));
    EXPECT_TRUE(IsCollider3DObjectDefinitionValid(Collider3DObjectDefinition{}));
    EXPECT_TRUE(IsRigid3DObjectDefinitionValid(Rigid3DObjectDefinition{}));
    EXPECT_TRUE(IsAudioSource3DObjectDefinitionValid(AudioSource3DObjectDefinition{}));
    EXPECT_TRUE(IsParticleEmitter3DObjectDefinitionValid(ParticleEmitter3DObjectDefinition{}));
    EXPECT_TRUE(IsScriptObjectDefinitionValid(ScriptObjectDefinition{}));
    EXPECT_TRUE(IsSpringArm3DObjectDefinitionValid(SpringArm3DObjectDefinition{}));
    EXPECT_TRUE(IsAnimationSequencerObjectDefinitionValid(AnimationSequencerObjectDefinition{}));
    EXPECT_TRUE(IsAnimationGraphObjectDefinitionValid(AnimationGraphObjectDefinition{}));
    EXPECT_TRUE(IsKinematic3DObjectDefinitionValid(Kinematic3DObjectDefinition{}));
}

// The seven names the editor's legacy EditorEntityKind path surfaces are locked at compile
// time: the editor now sources every default name from these definitions, so any drift here
// would silently rename what legacy creation produces.
static_assert(Object3DObjectDefinition::defaultName == "Object3D");
static_assert(Camera3DObjectDefinition::defaultName == "Camera");
static_assert(Light3DObjectDefinition::defaultName == "Directional Light");
static_assert(Collider3DObjectDefinition::defaultName == "Collision Box");
static_assert(BoxMesh3DObjectDefinition::defaultName == "Cube");
static_assert(SphereMesh3DObjectDefinition::defaultName == "UV Sphere");
static_assert(PlaneMesh3DObjectDefinition::defaultName == "Plane");
static_assert(Kinematic3DObjectDefinition::defaultName == "Kinematic3D");
static_assert(SpringArm3DObjectDefinition::defaultName == "SpringArm3D");

TEST_F(Object3DDefinitionsTest, DefaultNamesAreAuthoredPerKindNotGeneric) {
    // The six kinds that previously lived behind legacy EditorEntityKind values keep their
    // exact historical names; the kinds the editor used to name "Empty" now carry their own.
    //
    // The transform-only base is Object3D every way an author can meet it: display name, kind
    // enumerator, and the "object_3d" on-disk id. Its two earlier ids, "node_3d" and "empty",
    // still resolve to it at load (covered below) so the renames broke no saved file.
    EXPECT_EQ(Object3DObjectDefinition::defaultName, "Object3D");
    EXPECT_EQ(Camera3DObjectDefinition::defaultName, "Camera");
    EXPECT_EQ(Light3DObjectDefinition::defaultName, "Directional Light");
    EXPECT_EQ(Collider3DObjectDefinition::defaultName, "Collision Box");
    EXPECT_EQ(BoxMesh3DObjectDefinition::defaultName, "Cube");
    EXPECT_EQ(SphereMesh3DObjectDefinition::defaultName, "UV Sphere");
    EXPECT_EQ(PlaneMesh3DObjectDefinition::defaultName, "Plane");
    EXPECT_EQ(Area3DObjectDefinition::defaultName, "Area3D");
    EXPECT_EQ(Static3DObjectDefinition::defaultName, "Static3D");
    EXPECT_EQ(Character3DObjectDefinition::defaultName, "Character3D");
    EXPECT_EQ(MeshInstance3DObjectDefinition::defaultName, "MeshInstance3D");
    EXPECT_EQ(Rigid3DObjectDefinition::defaultName, "Rigid3D");
    EXPECT_EQ(AudioSource3DObjectDefinition::defaultName, "AudioSource3D");
    EXPECT_EQ(ParticleEmitter3DObjectDefinition::defaultName, "ParticleEmitter3D");
    EXPECT_EQ(ScriptObjectDefinition::defaultName, "Script");
    EXPECT_EQ(AnimationSequencerObjectDefinition::defaultName, "AnimationSequencer");
    EXPECT_EQ(AnimationGraphObjectDefinition::defaultName, "AnimationGraph");
    EXPECT_EQ(Kinematic3DObjectDefinition::defaultName, "Kinematic3D");
}

TEST_F(Object3DDefinitionsTest, ApplyAttachesEachKindsExactComponentRecipe) {
    {
        const Entity entity = CreateEntity();
        ApplyObject3DObjectDefinition(entityManager, entity, Object3DObjectDefinition{});
        // Object3D's recipe is the baseline itself, not "nothing": the guarantee is pinned
        // behaviour-for-behaviour in the dedicated tests below.
        ExpectObject3DBaseline(entityManager, entity, Object3DObjectDefinition::defaultName);
        EXPECT_FALSE(entityManager.HasComponent<CameraComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyArea3DObjectDefinition(entityManager, entity, Area3DObjectDefinition{});
        ExpectObject3DBaseline(entityManager, entity, Area3DObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<AreaComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyStatic3DObjectDefinition(entityManager, entity, Static3DObjectDefinition{});
        ExpectObject3DBaseline(entityManager, entity, Static3DObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<ColliderComponent>(entity));
        EXPECT_FALSE(entityManager.HasComponent<Rigid3DComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyCamera3DObjectDefinition(entityManager, entity, Camera3DObjectDefinition{});
        ExpectObject3DBaseline(entityManager, entity, Camera3DObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<CameraComponent>(entity));
        EXPECT_TRUE(entityManager.GetComponent<CameraComponent>(entity).current);
    }
    {
        const Entity entity = CreateEntity();
        ApplyMeshInstance3DObjectDefinition(entityManager, entity, MeshInstance3DObjectDefinition{});
        ExpectObject3DBaseline(entityManager, entity, MeshInstance3DObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<MeshComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyCollider3DObjectDefinition(entityManager, entity, Collider3DObjectDefinition{});
        ExpectObject3DBaseline(entityManager, entity, Collider3DObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<ColliderComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyRigid3DObjectDefinition(entityManager, entity, Rigid3DObjectDefinition{});
        ExpectObject3DBaseline(entityManager, entity, Rigid3DObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<Rigid3DComponent>(entity));
        // Object3D > PhysicsObject3D > Rigid3D: a simulated body is a physics object, so it is the
        // component that decides what its disabled state means and how it yields in a contact.
        EXPECT_TRUE(entityManager.HasComponent<PhysicsObjectComponent>(entity));
        // A simulated body without a shape is a body nothing can hit: the kind carries a collider
        // the same way Static3D and Kinematic3D do, and its own definition validates it.
        EXPECT_TRUE(entityManager.HasComponent<ColliderComponent>(entity));
        EXPECT_TRUE(IsColliderComponentValid(entityManager.GetComponent<ColliderComponent>(entity)));
    }
    {
        const Entity entity = CreateEntity();
        ApplyAudioSource3DObjectDefinition(entityManager, entity, AudioSource3DObjectDefinition{});
        ExpectObject3DBaseline(entityManager, entity, AudioSource3DObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<AudioSourceComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyParticleEmitter3DObjectDefinition(entityManager, entity, ParticleEmitter3DObjectDefinition{});
        ExpectObject3DBaseline(entityManager, entity, ParticleEmitter3DObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<ParticleEmitterComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyScriptObjectDefinition(entityManager, entity, ScriptObjectDefinition{});
        ExpectObject3DBaseline(entityManager, entity, ScriptObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<ScriptComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyAnimationSequencerObjectDefinition(entityManager, entity, AnimationSequencerObjectDefinition{});
        ExpectPureObject(entityManager, entity, AnimationSequencerObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<AnimationSequencerComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyAnimationGraphObjectDefinition(entityManager, entity, AnimationGraphObjectDefinition{});
        ExpectPureObject(entityManager, entity, AnimationGraphObjectDefinition::defaultName);
        EXPECT_TRUE(entityManager.HasComponent<AnimationGraphComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplySpringArm3DObjectDefinition(entityManager, entity, SpringArm3DObjectDefinition{});
        ExpectObject3DBaseline(entityManager, entity, SpringArm3DObjectDefinition::defaultName);
        ASSERT_TRUE(entityManager.HasComponent<SpringArm3DComponent>(entity));
        // The recipe seeds the runtime state exactly the way the deserializer does: an arm that
        // has never been simulated reads as fully extended, valid before the first step.
        const SpringArm3DComponent& arm =
            entityManager.GetComponent<SpringArm3DComponent>(entity);
        EXPECT_EQ(arm.currentLength, arm.armLength);
        EXPECT_TRUE(IsSpringArm3DObjectComponentValid(arm));
    }
}

TEST_F(Object3DDefinitionsTest, PrimitiveMeshRecipesKeepTheirDistinctShapesWhiteDefaultsAndColliders) {
    {
        const Entity entity = CreateEntity();
        ApplyBoxMesh3DObjectDefinition(entityManager, entity, BoxMesh3DObjectDefinition{});
        ASSERT_TRUE(entityManager.HasComponent<PrimitiveMeshComponent>(entity));
        ASSERT_TRUE(entityManager.HasComponent<ColliderComponent>(entity));
        const PrimitiveMeshComponent& mesh = entityManager.GetComponent<PrimitiveMeshComponent>(entity);
        EXPECT_EQ(mesh.kind, PrimitiveMeshKind::Cube);
        EXPECT_EQ(mesh.baseColor, (Math::Vector3{1.0F, 1.0F, 1.0F}));
        EXPECT_EQ(entityManager.GetComponent<ColliderComponent>(entity).halfExtents,
                  (Math::Vector3{0.5F, 0.5F, 0.5F}));
    }
    {
        const Entity entity = CreateEntity();
        ApplySphereMesh3DObjectDefinition(entityManager, entity, SphereMesh3DObjectDefinition{});
        ASSERT_TRUE(entityManager.HasComponent<PrimitiveMeshComponent>(entity));
        ASSERT_TRUE(entityManager.HasComponent<ColliderComponent>(entity));
        const PrimitiveMeshComponent& mesh = entityManager.GetComponent<PrimitiveMeshComponent>(entity);
        EXPECT_EQ(mesh.kind, PrimitiveMeshKind::UVSphere);
        EXPECT_EQ(mesh.baseColor, (Math::Vector3{1.0F, 1.0F, 1.0F}));
        EXPECT_EQ(entityManager.GetComponent<ColliderComponent>(entity).halfExtents,
                  (Math::Vector3{0.5F, 0.5F, 0.5F}));
    }
    {
        const Entity entity = CreateEntity();
        ApplyPlaneMesh3DObjectDefinition(entityManager, entity, PlaneMesh3DObjectDefinition{});
        ASSERT_TRUE(entityManager.HasComponent<PrimitiveMeshComponent>(entity));
        ASSERT_TRUE(entityManager.HasComponent<ColliderComponent>(entity));
        const PrimitiveMeshComponent& mesh = entityManager.GetComponent<PrimitiveMeshComponent>(entity);
        EXPECT_EQ(mesh.kind, PrimitiveMeshKind::Plane);
        EXPECT_EQ(mesh.baseColor, (Math::Vector3{1.0F, 1.0F, 1.0F}));
        // The plane's collider is a thin floor slab, not a full-height box.
        EXPECT_EQ(entityManager.GetComponent<ColliderComponent>(entity).halfExtents,
                  (Math::Vector3{0.5F, 0.025F, 0.5F}));
    }
}

TEST_F(Object3DDefinitionsTest, Player3DIsACharacterMarkedAsThePossessedBody) {
    const Entity entity = CreateEntity();
    ApplyPlayer3DObjectDefinition(entityManager, entity, Player3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, entity, Player3DObjectDefinition::defaultName);
    EXPECT_TRUE(entityManager.HasComponent<CharacterControllerComponent>(entity));
    ASSERT_TRUE(entityManager.HasComponent<PlayerComponent>(entity));
    EXPECT_TRUE(entityManager.GetComponent<PlayerComponent>(entity).possessOnPlay);
    ASSERT_TRUE(entityManager.HasComponent<HealthComponent>(entity));
    EXPECT_FLOAT_EQ(entityManager.GetComponent<HealthComponent>(entity).maxHealth, 100.0F);
    ASSERT_TRUE(entityManager.HasComponent<PawnComponent>(entity));
    ASSERT_TRUE(entityManager.HasComponent<ControllerComponent>(entity));
    EXPECT_EQ(entityManager.GetComponent<ControllerComponent>(entity).kind,
              ControllerKind::Player);
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, entity), Objects::SceneObjectKind::Player3D);
}

TEST_F(Object3DDefinitionsTest, CharacterBodyIsItsChainWithNoShapeOfItsOwn) {
    const Entity entity = CreateEntity();
    ApplyCharacter3DObjectDefinition(entityManager, entity, Character3DObjectDefinition{});
    // Object3D > PhysicsObject3D > SolidBody3D > Character3D, and nothing from another branch.
    ExpectObject3DBaseline(entityManager, entity, Character3DObjectDefinition::defaultName);
    EXPECT_TRUE(entityManager.HasComponent<PhysicsObjectComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<SolidBodyComponent>(entity));
    ASSERT_TRUE(entityManager.HasComponent<CharacterControllerComponent>(entity));
    EXPECT_FALSE(entityManager.HasComponent<Rigid3DComponent>(entity));
    EXPECT_FALSE(entityManager.HasComponent<RenderInstanceComponent>(entity));
    EXPECT_TRUE(entityManager.GetComponent<CharacterControllerComponent>(entity).builtInMovement);

    // No collider until the author adds one. A character born with a capsule made the Collider3D
    // an author then added a second shape in the same place, and the controller pushed the body
    // out of it - the character was flung the moment the collider appeared.
    EXPECT_FALSE(entityManager.HasComponent<ColliderComponent>(entity))
        << "a Character3D is a controller, not a shape";

    // And a collider the author does add is the only one there is: adding it is not a second body.
    entityManager.AddComponent<ColliderComponent>(entity, Collider3D::MakeCapsule(0.4F, 1.8F));
    ASSERT_TRUE(entityManager.HasComponent<ColliderComponent>(entity));
    EXPECT_EQ(entityManager.GetComponent<ColliderComponent>(entity).shapeType,
              ColliderShapeType::Capsule);

    // Applying again keeps what was authored - the collider included.
    entityManager.GetComponent<CharacterControllerComponent>(entity).moveSpeed = 9.0F;
    ApplyCharacter3DObjectDefinition(entityManager, entity, Character3DObjectDefinition{});
    EXPECT_FLOAT_EQ(entityManager.GetComponent<CharacterControllerComponent>(entity).moveSpeed, 9.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<ColliderComponent>(entity).height, 1.8F);
}

TEST_F(Object3DDefinitionsTest, AShapeUnderTheCharacterIsTheCharactersBody) {
    // The authoring shape: a character with no collider of its own, and a Collider3D under it.
    // Resolving is what makes that shape the body's shape instead of an object standing next to
    // it - and what makes the row badge's "no body" claim honest, since it asks the same question.
    const Entity body = CreateEntity();
    ApplyCharacter3DObjectDefinition(entityManager, body, Character3DObjectDefinition{});
    EXPECT_EQ(ResolveCharacterShapeEntity(entityManager, sceneGraph, body), kInvalidEntity)
        << "no shape anywhere yet";

    const Entity shape = entityManager.CreateEntity();
    Collider3DObjectDefinition shapeDefinition{};
    shapeDefinition.collider = Collider3D::MakeCapsule(0.4F, 1.8F);
    ApplyCollider3DObjectDefinition(entityManager, shape, shapeDefinition);
    sceneGraph.SetParent(entityManager, shape, body);

    EXPECT_EQ(ResolveCharacterShapeEntity(entityManager, sceneGraph, body), shape);

    // A collider authored on the body itself wins over anything under it.
    entityManager.AddComponent<ColliderComponent>(body, Collider3D::MakeBox({0.5F, 0.5F, 0.5F}));
    EXPECT_EQ(ResolveCharacterShapeEntity(entityManager, sceneGraph, body), body);
}

TEST_F(Object3DDefinitionsTest, TheCapsuleCharactersUsedToBeBornWithIsNamed) {
    // A document saved before characters stopped carrying a shape still has this capsule in it, and
    // the author never added it - so the Outliner has to be able to say where a body nobody asked
    // for came from. The test is about telling that shape apart from one an author sized.
    const ColliderComponent legacy = Character3DObjectDefinition::MakeDefaultCollider();
    EXPECT_TRUE(IsLegacyCharacterCapsule(legacy));

    // A capsule the author sized: same shape, different numbers.
    ColliderComponent authored = Collider3D::MakeCapsule(0.5F, 2.0F);
    EXPECT_FALSE(IsLegacyCharacterCapsule(authored));

    // A box of the same size is not the capsule.
    EXPECT_FALSE(IsLegacyCharacterCapsule(Collider3D::MakeBox({0.4F, 0.9F, 0.4F})));

    // Float noise from a file round trip is still the same capsule; a millimetre is not.
    ColliderComponent rounded = legacy;
    rounded.height = 1.8F - 1.0e-6F;
    EXPECT_TRUE(IsLegacyCharacterCapsule(rounded));
    rounded.height = 1.8F - 0.01F;
    EXPECT_FALSE(IsLegacyCharacterCapsule(rounded));
}

TEST_F(Object3DDefinitionsTest, AMeshAModelWearsIsNotTheCharactersShape) {
    // The authoring trap: a character with no Collider3D, and the mesh it wears under it. Every
    // mesh in this engine carries a collider, because a mesh you can see is something you can hit -
    // so adopting "the first child with a collider" made the model's own collider the body's shape.
    // The author never added a shape, and the Inspector had none to show.
    const Entity body = CreateEntity();
    ApplyCharacter3DObjectDefinition(entityManager, body, Character3DObjectDefinition{});

    const Entity wornMesh = entityManager.CreateEntity();
    ApplyBoxMesh3DObjectDefinition(entityManager, wornMesh, BoxMesh3DObjectDefinition{});
    ASSERT_TRUE(entityManager.HasComponent<ColliderComponent>(wornMesh))
        << "a visible primitive is a solid - that is why it must not also be a body's shape";
    sceneGraph.SetParent(entityManager, wornMesh, body);

    EXPECT_EQ(ResolveCharacterShapeEntity(entityManager, sceneGraph, body), kInvalidEntity)
        << "a mesh the body wears is not a description of the body";

    // A MeshInstance3D the author gave a collider to is the same story: it is geometry.
    const Entity model = entityManager.CreateEntity();
    ApplyMeshInstance3DObjectDefinition(entityManager, model, MeshInstance3DObjectDefinition{});
    entityManager.AddComponent<ColliderComponent>(model, Collider3D::MakeBox({0.5F, 0.5F, 0.5F}));
    sceneGraph.SetParent(entityManager, model, body);
    EXPECT_EQ(ResolveCharacterShapeEntity(entityManager, sceneGraph, body), kInvalidEntity);

    // A Collider3D is a shape and nothing else - it draws nothing - so it is the body's shape.
    const Entity shape = entityManager.CreateEntity();
    ApplyCollider3DObjectDefinition(entityManager, shape, Collider3DObjectDefinition{});
    sceneGraph.SetParent(entityManager, shape, body);
    EXPECT_EQ(ResolveCharacterShapeEntity(entityManager, sceneGraph, body), shape);
}

TEST_F(Object3DDefinitionsTest, AShapeUnderTheCharacterIgnoresChildrenThatAreBodiesThemselves) {
    // A second body standing under a character is a neighbour, not a description of it: its
    // collider must never be adopted as the character's shape.
    const Entity body = CreateEntity();
    ApplyCharacter3DObjectDefinition(entityManager, body, Character3DObjectDefinition{});

    const Entity neighbour = entityManager.CreateEntity();
    ApplyRigid3DObjectDefinition(entityManager, neighbour, Rigid3DObjectDefinition{});
    sceneGraph.SetParent(entityManager, neighbour, body);

    EXPECT_EQ(ResolveCharacterShapeEntity(entityManager, sceneGraph, body), kInvalidEntity)
        << "a Rigid3D under a character is not the character's shape";
}

TEST_F(Object3DDefinitionsTest, CharacterBodyStillBuildsAReadyToWalkCapsuleWhenAskedForOne) {
    const Entity entity = CreateEntity();
    ApplyCharacter3DObjectDefinition(entityManager, entity,
                                        Character3DObjectDefinition::WithDefaultCollider());
    // A person-sized capsule, so a caller that wants a body out of the box still gets one.
    ASSERT_TRUE(entityManager.HasComponent<ColliderComponent>(entity));
    const ColliderComponent& shape = entityManager.GetComponent<ColliderComponent>(entity);
    EXPECT_EQ(shape.shapeType, ColliderShapeType::Capsule);
    EXPECT_FLOAT_EQ(shape.height, 1.8F);
    EXPECT_FLOAT_EQ(shape.radius, 0.4F);
}

TEST_F(Object3DDefinitionsTest, CharacterBodyRefusesSettingsItCannotRunWith) {
    EXPECT_TRUE(IsCharacter3DObjectDefinitionValid(Character3DObjectDefinition{}));
    const auto invalid = [](auto change) {
        Character3DObjectDefinition definition{};
        change(definition.controller);
        return !IsCharacter3DObjectDefinitionValid(definition);
    };
    EXPECT_TRUE(invalid([](CharacterControllerComponent& c) { c.airControl = 1.5F; }));
    EXPECT_TRUE(invalid([](CharacterControllerComponent& c) { c.moveSpeed = -1.0F; }));
    EXPECT_TRUE(invalid([](CharacterControllerComponent& c) { c.maxSlides = 0U; }));
    EXPECT_TRUE(invalid([](CharacterControllerComponent& c) { c.maxSlides = 33U; }));
    EXPECT_TRUE(invalid([](CharacterControllerComponent& c) { c.coyoteTimeSeconds = -0.1F; }));
    EXPECT_TRUE(invalid([](CharacterControllerComponent& c) { c.motionMode = static_cast<CharacterMotionMode>(7); }));
    EXPECT_TRUE(invalid([](CharacterControllerComponent& c) {
        c.velocity = Math::Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F};
    }));
}

TEST_F(Object3DDefinitionsTest, SolidBodyBaseSitsOnPhysicsObject) {
    const Entity entity = CreateEntity();
    ApplySolidBody3DBase(entityManager, entity, "Static3D");
    ExpectObject3DBaseline(entityManager, entity, "Static3D");
    EXPECT_TRUE(entityManager.HasComponent<PhysicsObjectComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<SolidBodyComponent>(entity));
    entityManager.GetComponent<SolidBodyComponent>(entity).lockMotionZ = true;
    ApplySolidBody3DBase(entityManager, entity, "Static3D");
    EXPECT_TRUE(entityManager.GetComponent<SolidBodyComponent>(entity).lockMotionZ);
}

TEST_F(Object3DDefinitionsTest, KinematicRecipeMatchesTheFormerInlineEditorRecipe) {
    // The last inline multi-component recipe the editor's creation switch used to hardcode:
    // collider + kinematic body + the animatable body's own component, in that spirit unchanged.
    const Entity entity = CreateEntity();
    ApplyKinematic3DObjectDefinition(entityManager, entity, Kinematic3DObjectDefinition{});
    // Object3D > PhysicsObject3D > Kinematic3D, so a platform that is stopped can be kept as an
    // immovable obstacle or taken out of the world rather than only deleted.
    EXPECT_TRUE(entityManager.HasComponent<PhysicsObjectComponent>(entity));
    ASSERT_TRUE(entityManager.HasComponent<ColliderComponent>(entity));
    ASSERT_TRUE(entityManager.HasComponent<Rigid3DComponent>(entity));
    ASSERT_TRUE(entityManager.HasComponent<Kinematic3DComponent>(entity));
    EXPECT_TRUE(entityManager.GetComponent<Rigid3DComponent>(entity).isKinematic);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Kinematic3DComponent>(entity).interpolation, 1.0F);

    Kinematic3DObjectDefinition nonKinematic{};
    nonKinematic.body.isKinematic = false;
    EXPECT_FALSE(IsKinematic3DObjectDefinitionValid(nonKinematic));
}

TEST_F(Object3DDefinitionsTest, LightRecipeDefaultsToDirectionalSunLight) {
    const Entity entity = CreateEntity();
    ApplyLight3DObjectDefinition(entityManager, entity, Light3DObjectDefinition{});
    ASSERT_TRUE(entityManager.HasComponent<LightComponent>(entity));
    EXPECT_EQ(entityManager.GetComponent<LightComponent>(entity).type, LightType::Directional);
}

TEST_F(Object3DDefinitionsTest, Object3DIsReachableUnderBothItsNewAndLegacyTypeIds) {
    const Objects::SceneObjectDescriptor* descriptor =
        Objects::FindSceneObjectDescriptor(Objects::SceneObjectKind::Object3D);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->typeId, "object_3d");
    EXPECT_EQ(descriptor->displayName, "Object3D");
    EXPECT_TRUE(descriptor->libraryCreatable);

    // New writes use the canonical id; both ids this kind had before it must resolve to the very
    // same row, since saved documents and layouts carrying either have no way to upgrade
    // themselves: "node_3d" from the Object3D -> Object3D rename, "empty" from before that.
    EXPECT_EQ(Objects::FindSceneObjectDescriptor("object_3d"), descriptor);
    EXPECT_EQ(Objects::FindSceneObjectDescriptor("node_3d"), descriptor);
    EXPECT_EQ(Objects::FindSceneObjectDescriptor("empty"), descriptor);
    EXPECT_EQ(Objects::GetSceneObjectTypeId(Objects::SceneObjectKind::Object3D), "object_3d");
}

TEST_F(Object3DDefinitionsTest, Object3DApplyAttachesALocalTransformToABareEntity) {
    const Entity entity = CreateEntity();
    ApplyObject3DObjectDefinition(entityManager, entity, Object3DObjectDefinition{});

    // The meaningful part of the guarantee: an Object3D reached without the creation shell still
    // ends up with the transform every scene object needs, at identity and named after its kind.
    const TransformComponent& local = entityManager.GetComponent<TransformComponent>(entity);
    EXPECT_EQ(local.localPosition.x, 0.0F);
    EXPECT_EQ(local.localPosition.y, 0.0F);
    EXPECT_EQ(local.localPosition.z, 0.0F);
    EXPECT_EQ(entityManager.GetComponent<NameComponent>(entity).name,
              Object3DObjectDefinition::defaultName);
    EXPECT_EQ(entityManager.GetComponent<HierarchyComponent>(entity).parent, kInvalidEntity);
}

TEST_F(Object3DDefinitionsTest, Object3DApplyPreservesAuthoredValuesAndRepairsOnlyWhatIsMissing) {
    const Entity entity = CreateEntity();
    // A partially-baselined entity, as a partial deserialization leaves behind: transform is
    // present and authored, the rest of the baseline is not.
    TransformComponent authored;
    authored.localPosition = Math::Vector3{3.0F, -2.0F, 7.5F};
    entityManager.AddComponent<TransformComponent>(entity, authored);
    entityManager.AddComponent<NameComponent>(entity, NameComponent{"AuthoredPivot"});

    ApplyObject3DObjectDefinition(entityManager, entity, Object3DObjectDefinition{});

    // Authored state is sacred: position and name survive application untouched, and the missing
    // half of the baseline is what got repaired.
    EXPECT_EQ(entityManager.GetComponent<TransformComponent>(entity).localPosition.x, 3.0F);
    EXPECT_EQ(entityManager.GetComponent<TransformComponent>(entity).localPosition.z, 7.5F);
    EXPECT_EQ(entityManager.GetComponent<NameComponent>(entity).name, "AuthoredPivot");
    EXPECT_TRUE(entityManager.HasComponent<WorldTransformComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<HierarchyComponent>(entity));
}

TEST_F(Object3DDefinitionsTest, Object3DCarriesVisibilityAndTheCommonObjectSection) {
    const Entity entity = CreateEntity();
    ApplyObject3DObjectDefinition(entityManager, entity, Object3DObjectDefinition{});
    // Its Inspector has no Add Component, so every section it shows is attached by the recipe.
    EXPECT_TRUE(entityManager.HasComponent<VisibilityComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<ProcessComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<ThreadGroupComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<PhysicsInterpolationComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<AutoTranslateComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<EditorDescriptionComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<ScriptComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<ObjectMetadataComponent>(entity));
}

TEST_F(Object3DDefinitionsTest, AbstractBasesAreObject3DPlusTheirOwnComponent) {
    const Entity bone = CreateEntity();
    const Entity physics = CreateEntity();
    const Entity render = CreateEntity();
    ApplyBoneModifier3DBase(entityManager, bone, "LookAtModifier3D");
    ApplyPhysicsObject3DBase(entityManager, physics, "Area3D");
    ApplyRenderInstance3DBase(entityManager, render, "MeshInstance3D");
    ExpectObject3DBaseline(entityManager, bone, "LookAtModifier3D");
    ExpectObject3DBaseline(entityManager, physics, "Area3D");
    ExpectObject3DBaseline(entityManager, render, "MeshInstance3D");
    for (const Entity entity : {bone, physics, render}) {
        EXPECT_TRUE(entityManager.HasComponent<VisibilityComponent>(entity));
        EXPECT_TRUE(entityManager.HasComponent<ProcessComponent>(entity));
        EXPECT_TRUE(entityManager.HasComponent<ObjectMetadataComponent>(entity));
    }
    EXPECT_TRUE(entityManager.HasComponent<BoneModifierComponent>(bone));
    EXPECT_TRUE(entityManager.HasComponent<PhysicsObjectComponent>(physics));
    EXPECT_TRUE(entityManager.HasComponent<RenderInstanceComponent>(render));
    // The child's name, never the abstract base's.
    EXPECT_EQ(entityManager.GetComponent<NameComponent>(physics).name, "Area3D");
    // Authored values survive a second apply.
    entityManager.GetComponent<BoneModifierComponent>(bone).influence = 0.5F;
    ApplyBoneModifier3DBase(entityManager, bone, "LookAtModifier3D");
    EXPECT_EQ(entityManager.GetComponent<BoneModifierComponent>(bone).influence, 0.5F);
}

TEST_F(Object3DDefinitionsTest, AbstractBaseComponentsRejectValuesTheySaveBadly) {
    EXPECT_TRUE(IsBoneModifierComponentValid(BoneModifierComponent{}));
    EXPECT_FALSE(IsBoneModifierComponentValid(BoneModifierComponent{true, 1.5F}));
    EXPECT_FALSE(IsBoneModifierComponentValid(BoneModifierComponent{true, std::numeric_limits<float>::quiet_NaN()}));
    PhysicsObjectComponent object{};
    EXPECT_TRUE(IsPhysicsObjectComponentValid(object));
    object.collisionPriority = -1.0F;
    EXPECT_FALSE(IsPhysicsObjectComponentValid(object));
    object.collisionPriority = 1.0F;
    object.disableMode = static_cast<PhysicsObjectDisableMode>(9);
    EXPECT_FALSE(IsPhysicsObjectComponentValid(object));
    EXPECT_FALSE(IsRenderInstanceComponentValid(
        RenderInstanceComponent{1U, std::numeric_limits<float>::infinity(), true}));
}

TEST_F(Object3DDefinitionsTest, RenderInstanceChildBasesCarryRenderInstanceAndTheirOwnComponent) {
    const Entity surface = CreateEntity();
    const Entity light = CreateEntity();
    ApplySurfaceInstance3DBase(entityManager, surface, "MeshInstance3D");
    ApplyLightEmitter3DBase(entityManager, light, "OmniLight3D");
    ExpectObject3DBaseline(entityManager, surface, "MeshInstance3D");
    ExpectObject3DBaseline(entityManager, light, "OmniLight3D");
    for (const Entity entity : {surface, light}) {
        EXPECT_TRUE(entityManager.HasComponent<RenderInstanceComponent>(entity));
        EXPECT_TRUE(entityManager.HasComponent<VisibilityComponent>(entity));
        EXPECT_TRUE(entityManager.HasComponent<ObjectMetadataComponent>(entity));
    }
    EXPECT_TRUE(entityManager.HasComponent<SurfaceInstanceComponent>(surface));
    EXPECT_FALSE(entityManager.HasComponent<LightEmitterComponent>(surface));
    EXPECT_TRUE(entityManager.HasComponent<LightEmitterComponent>(light));
    EXPECT_FALSE(entityManager.HasComponent<SurfaceInstanceComponent>(light));
}

TEST_F(Object3DDefinitionsTest, MeshAndParticleObjectsAreSurfaceInstances) {
    const Entity mesh = CreateEntity();
    const Entity box = CreateEntity();
    const Entity particles = CreateEntity();
    ApplyMeshInstance3DObjectDefinition(entityManager, mesh, MeshInstance3DObjectDefinition{});
    ApplyBoxMesh3DObjectDefinition(entityManager, box, BoxMesh3DObjectDefinition{});
    ApplyParticleEmitter3DObjectDefinition(entityManager, particles, ParticleEmitter3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, mesh, "MeshInstance3D");
    ExpectObject3DBaseline(entityManager, box, "Cube");
    ExpectObject3DBaseline(entityManager, particles, "ParticleEmitter3D");
    for (const Entity entity : {mesh, box, particles}) {
        EXPECT_TRUE(entityManager.HasComponent<SurfaceInstanceComponent>(entity));
        EXPECT_TRUE(entityManager.HasComponent<RenderInstanceComponent>(entity));
        EXPECT_TRUE(entityManager.HasComponent<VisibilityComponent>(entity));
        EXPECT_TRUE(entityManager.HasComponent<ObjectMetadataComponent>(entity));
    }
}

TEST_F(Object3DDefinitionsTest, Decal3DAndFogVolume3DAreRenderInstancesPlusTheirOwnComponent) {
    const Entity decal = CreateEntity();
    const Entity fog = CreateEntity();
    Decal3DObjectDefinition decalDefinition{};
    decalDefinition.decal.albedoMix = 0.25F;
    ApplyDecal3DObjectDefinition(entityManager, decal, decalDefinition);
    ApplyFogVolume3DObjectDefinition(entityManager, fog, FogVolume3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, decal, "Decal3D");
    ExpectObject3DBaseline(entityManager, fog, "FogVolume3D");
    for (const Entity entity : {decal, fog}) {
        EXPECT_TRUE(entityManager.HasComponent<RenderInstanceComponent>(entity));
        EXPECT_FALSE(entityManager.HasComponent<SurfaceInstanceComponent>(entity));
        EXPECT_FALSE(entityManager.HasComponent<LightEmitterComponent>(entity));
    }
    EXPECT_EQ(entityManager.GetComponent<Decal3DComponent>(decal).albedoMix, 0.25F);
    EXPECT_EQ(entityManager.GetComponent<FogVolume3DComponent>(fog), FogVolume3DComponent{});
    // A second apply keeps what was authored.
    entityManager.GetComponent<FogVolume3DComponent>(fog).density = -0.5F;
    ApplyFogVolume3DObjectDefinition(entityManager, fog, FogVolume3DObjectDefinition{});
    EXPECT_EQ(entityManager.GetComponent<FogVolume3DComponent>(fog).density, -0.5F);
}

TEST_F(Object3DDefinitionsTest, OptimizationVolumesAreObject3DNotRenderInstances) {
    const Entity occluder = CreateEntity();
    const Entity region = CreateEntity();
    const Entity partition = CreateEntity();
    const Entity lod = CreateEntity();
    const Entity probe = CreateEntity();
    ApplyOccluder3DObjectDefinition(entityManager, occluder, Occluder3DObjectDefinition{});
    ApplyVisibilityRegion3DObjectDefinition(entityManager, region, VisibilityRegion3DObjectDefinition{});
    ApplyWorldPartition3DObjectDefinition(entityManager, partition, WorldPartition3DObjectDefinition{});
    ApplyLodGroup3DObjectDefinition(entityManager, lod, LodGroup3DObjectDefinition{});
    ApplyReflectionProbe3DObjectDefinition(entityManager, probe, ReflectionProbe3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, occluder, "Occluder3D");
    ExpectObject3DBaseline(entityManager, region, "VisibilityRegion3D");
    ExpectObject3DBaseline(entityManager, partition, "WorldPartition3D");
    ExpectObject3DBaseline(entityManager, lod, "LODGroup3D");
    ExpectObject3DBaseline(entityManager, probe, "ReflectionProbe3D");
    for (const Entity entity : {occluder, region, partition, lod, probe}) {
        EXPECT_TRUE(entityManager.HasComponent<VisibilityComponent>(entity));
        EXPECT_TRUE(entityManager.HasComponent<ProcessComponent>(entity));
        EXPECT_TRUE(entityManager.HasComponent<ObjectMetadataComponent>(entity));
        EXPECT_FALSE(entityManager.HasComponent<RenderInstanceComponent>(entity));
        EXPECT_FALSE(entityManager.HasComponent<SurfaceInstanceComponent>(entity));
        EXPECT_FALSE(entityManager.HasComponent<LightEmitterComponent>(entity));
        EXPECT_FALSE(entityManager.HasComponent<PhysicsObjectComponent>(entity));
    }
    EXPECT_TRUE(entityManager.HasComponent<Occluder3DComponent>(occluder));
    EXPECT_TRUE(entityManager.HasComponent<VisibilityRegion3DComponent>(region));
    EXPECT_TRUE(entityManager.HasComponent<WorldPartition3DComponent>(partition));
    EXPECT_TRUE(entityManager.HasComponent<LodGroup3DComponent>(lod));
    EXPECT_TRUE(entityManager.HasComponent<ReflectionProbe3DComponent>(probe));
}

TEST_F(Object3DDefinitionsTest, RenderInstanceFamilyComponentsRejectValuesTheySaveBadly) {
    EXPECT_TRUE(IsSurfaceInstanceComponentValid(SurfaceInstanceComponent{}));
    SurfaceInstanceComponent surface{};
    surface.transparency = 1.5F;
    EXPECT_FALSE(IsSurfaceInstanceComponentValid(surface));
    surface = {};
    surface.lodBias = 0.0F;
    EXPECT_FALSE(IsSurfaceInstanceComponentValid(surface));
    surface = {};
    surface.visibilityRangeBegin = 10.0F;
    surface.visibilityRangeEnd = 5.0F;
    EXPECT_FALSE(IsSurfaceInstanceComponentValid(surface));
    // One override per surface, and no more than a mesh can have surfaces.
    surface = {};
    surface.surfaceMaterialOverridePaths.assign(kMaximumSurfaceMaterialOverrides, std::string{});
    EXPECT_TRUE(IsSurfaceInstanceComponentValid(surface));
    surface.surfaceMaterialOverridePaths.push_back("one_too_many.uvmat");
    EXPECT_FALSE(IsSurfaceInstanceComponentValid(surface));
    SurfaceInstanceComponent overfilled{};
    overfilled.surfaceMaterialOverridePaths.assign(kMaximumSurfaceMaterialOverrides + 2U, "x.uvmat");
    EXPECT_EQ(SanitizeSurfaceInstanceComponent(overfilled).surfaceMaterialOverridePaths.size(),
              kMaximumSurfaceMaterialOverrides);
    // Trailing empties say what an absent entry says, so they are trimmed; interior ones mark the
    // surface they belong to and stay.
    SurfaceInstanceComponent trailing{};
    trailing.surfaceMaterialOverridePaths = {"", "red.uvmat", "", ""};
    EXPECT_EQ(SanitizeSurfaceInstanceComponent(trailing).surfaceMaterialOverridePaths,
              (std::vector<std::string>{"", "red.uvmat"}));

    EXPECT_FALSE(IsSurfaceInstance3DOutsideVisibilityRange(SurfaceInstanceComponent{}, 0.0F));
    EXPECT_FALSE(IsSurfaceInstance3DOutsideVisibilityRange(SurfaceInstanceComponent{}, 1000.0F));
    SurfaceInstanceComponent ranged{};
    ranged.visibilityRangeBegin = 5.0F;
    ranged.visibilityRangeEnd = 20.0F;
    EXPECT_TRUE(IsSurfaceInstance3DOutsideVisibilityRange(ranged, 4.9F));
    EXPECT_FALSE(IsSurfaceInstance3DOutsideVisibilityRange(ranged, 5.0F));
    EXPECT_FALSE(IsSurfaceInstance3DOutsideVisibilityRange(ranged, 20.0F));
    EXPECT_TRUE(IsSurfaceInstance3DOutsideVisibilityRange(ranged, 20.1F));
    EXPECT_TRUE(IsSurfaceInstance3DOutsideVisibilityRange(ranged, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FLOAT_EQ(SurfaceInstance3DVisibilityFadeWeight(ranged, 5.0F), 1.0F);
    EXPECT_FLOAT_EQ(SurfaceInstance3DVisibilityFadeWeight(ranged, 4.9F), 0.0F);
    SurfaceInstanceComponent selfFade{};
    selfFade.visibilityRangeBegin = 5.0F;
    selfFade.visibilityRangeBeginMargin = 2.0F;
    selfFade.visibilityRangeEnd = 20.0F;
    selfFade.visibilityRangeEndMargin = 4.0F;
    selfFade.visibilityRangeFadeMode = SurfaceFadeMode::Self;
    EXPECT_TRUE(IsSurfaceInstance3DOutsideVisibilityRange(selfFade, 2.9F));
    EXPECT_FALSE(IsSurfaceInstance3DOutsideVisibilityRange(selfFade, 3.0F));
    EXPECT_FALSE(IsSurfaceInstance3DOutsideVisibilityRange(selfFade, 24.0F));
    EXPECT_TRUE(IsSurfaceInstance3DOutsideVisibilityRange(selfFade, 24.1F));
    EXPECT_FLOAT_EQ(SurfaceInstance3DVisibilityFadeWeight(selfFade, 3.0F), 0.0F);
    EXPECT_FLOAT_EQ(SurfaceInstance3DVisibilityFadeWeight(selfFade, 4.0F), 0.5F);
    EXPECT_FLOAT_EQ(SurfaceInstance3DVisibilityFadeWeight(selfFade, 5.0F), 1.0F);
    EXPECT_FLOAT_EQ(SurfaceInstance3DVisibilityFadeWeight(selfFade, 20.0F), 1.0F);
    EXPECT_FLOAT_EQ(SurfaceInstance3DVisibilityFadeWeight(selfFade, 22.0F), 0.5F);
    EXPECT_FLOAT_EQ(SurfaceInstance3DVisibilityFadeWeight(selfFade, 24.0F), 0.0F);
    SurfaceInstanceComponent dependencies{};
    dependencies.visibilityRangeBegin = 5.0F;
    dependencies.visibilityRangeEnd = 20.0F;
    dependencies.visibilityRangeBeginMargin = 2.0F;
    dependencies.visibilityRangeFadeMode = SurfaceFadeMode::Dependencies;
    EXPECT_TRUE(IsSurfaceInstance3DOutsideVisibilityRange(dependencies, 4.9F));
    EXPECT_FLOAT_EQ(SurfaceInstance3DVisibilityFadeWeight(dependencies, 5.0F), 1.0F);
    SurfaceInstanceComponent invalidRange{};
    invalidRange.transparency = 2.0F;
    EXPECT_TRUE(IsSurfaceInstance3DOutsideVisibilityRange(invalidRange, 1.0F));

    Math::Aabb bounds = Math::Aabb::FromCenterExtents({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    ExpandSurfaceInstance3DCullBounds(SurfaceInstanceComponent{}, bounds);
    EXPECT_NEAR(bounds.min.x, -0.5F, 1.0e-6F);
    EXPECT_NEAR(bounds.max.x, 0.5F, 1.0e-6F);
    SurfaceInstanceComponent padded{};
    padded.extraCullMargin = 2.0F;
    ExpandSurfaceInstance3DCullBounds(padded, bounds);
    EXPECT_NEAR(bounds.min.x, -2.5F, 1.0e-6F);
    EXPECT_NEAR(bounds.max.x, 2.5F, 1.0e-6F);
    EXPECT_NEAR(bounds.min.y, -2.5F, 1.0e-6F);
    EXPECT_NEAR(bounds.max.z, 2.5F, 1.0e-6F);

    EXPECT_TRUE(SurfaceInstance3DCastsShadow(SurfaceInstanceComponent{}));
    EXPECT_TRUE(SurfaceInstance3DDrawsInView(SurfaceInstanceComponent{}));
    SurfaceInstanceComponent shadowOff{};
    shadowOff.castShadow = SurfaceShadowMode::Off;
    EXPECT_FALSE(SurfaceInstance3DCastsShadow(shadowOff));
    EXPECT_TRUE(SurfaceInstance3DDrawsInView(shadowOff));
    SurfaceInstanceComponent shadowsOnly{};
    shadowsOnly.castShadow = SurfaceShadowMode::ShadowsOnly;
    EXPECT_TRUE(SurfaceInstance3DCastsShadow(shadowsOnly));
    EXPECT_FALSE(SurfaceInstance3DDrawsInView(shadowsOnly));
    SurfaceInstanceComponent doubleSided{};
    doubleSided.castShadow = SurfaceShadowMode::DoubleSided;
    EXPECT_TRUE(SurfaceInstance3DCastsShadow(doubleSided));
    EXPECT_TRUE(SurfaceInstance3DDrawsInView(doubleSided));
    EXPECT_FLOAT_EQ(SurfaceInstance3DOpacity(SurfaceInstanceComponent{}), 1.0F);
    SurfaceInstanceComponent faded{};
    faded.transparency = 0.25F;
    EXPECT_FLOAT_EQ(SurfaceInstance3DOpacity(faded), 0.75F);
    faded.transparency = 1.0F;
    EXPECT_FLOAT_EQ(SurfaceInstance3DOpacity(faded), 0.0F);
    faded.transparency = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(SurfaceInstance3DOpacity(faded), 1.0F);
    EXPECT_FALSE(SurfaceInstance3DHasOverlay(SurfaceInstanceComponent{}));
    SurfaceInstanceComponent overlay{};
    overlay.materialOverlayPath = "flash.uvmat";
    EXPECT_TRUE(SurfaceInstance3DHasOverlay(overlay));
    EXPECT_FLOAT_EQ(ApplySurfaceInstance3DOverlaySortBias(4.0F), 3.999F);
    EXPECT_TRUE(std::isnan(ApplySurfaceInstance3DOverlaySortBias(std::numeric_limits<float>::quiet_NaN())));
    EXPECT_FLOAT_EQ(SurfaceInstance3DLodDistance(SurfaceInstanceComponent{}, 40.0F), 40.0F);
    SurfaceInstanceComponent keepDetail{};
    keepDetail.lodBias = 2.0F;
    EXPECT_FLOAT_EQ(SurfaceInstance3DLodDistance(keepDetail, 40.0F), 20.0F);
    SurfaceInstanceComponent dropSooner{};
    dropSooner.lodBias = 0.5F;
    EXPECT_FLOAT_EQ(SurfaceInstance3DLodDistance(dropSooner, 40.0F), 80.0F);
    dropSooner.lodBias = 0.0F;
    EXPECT_FLOAT_EQ(SurfaceInstance3DLodDistance(dropSooner, 40.0F), 40.0F);
    EXPECT_TRUE(std::isnan(
        SurfaceInstance3DLodDistance(SurfaceInstanceComponent{}, std::numeric_limits<float>::quiet_NaN())));

    EXPECT_TRUE(IsRenderInstance3DOnViewLayers(0x00000001U, 0xFFFFFFFFU));
    EXPECT_TRUE(IsRenderInstance3DOnViewLayers(0x00000002U, 0x00000002U));
    EXPECT_FALSE(IsRenderInstance3DOnViewLayers(0x00000001U, 0x00000002U));
    EXPECT_FALSE(IsRenderInstance3DOnViewLayers(0x00000001U, 0U));
    EXPECT_FALSE(IsRenderInstance3DOnViewLayers(0U, 0xFFFFFFFFU));
    EXPECT_FLOAT_EQ(ApplyRenderInstance3DSortingOffset(4.0F, 1.5F), 5.5F);
    EXPECT_FLOAT_EQ(ApplyRenderInstance3DSortingOffset(4.0F, -1.0F), 3.0F);
    EXPECT_FLOAT_EQ(ApplyRenderInstance3DSortingOffset(4.0F, std::numeric_limits<float>::quiet_NaN()), 4.0F);

    EXPECT_TRUE(IsLightEmitter3DLightingLayers(0xFFFFFFFFU, 1U));
    EXPECT_TRUE(IsLightEmitter3DLightingLayers(0x2U, 0x2U));
    EXPECT_FALSE(IsLightEmitter3DLightingLayers(0x1U, 0x2U));
    EXPECT_FALSE(IsLightEmitter3DLightingLayers(0U, 0xFFFFFFFFU));
    EXPECT_FLOAT_EQ(LightEmitter3DDistanceFadeWeight(0.0F, 40.0F, 10.0F), 1.0F);
    EXPECT_FLOAT_EQ(LightEmitter3DDistanceFadeWeight(40.0F, 40.0F, 10.0F), 1.0F);
    EXPECT_FLOAT_EQ(LightEmitter3DDistanceFadeWeight(45.0F, 40.0F, 10.0F), 0.5F);
    EXPECT_FLOAT_EQ(LightEmitter3DDistanceFadeWeight(50.0F, 40.0F, 10.0F), 0.0F);
    EXPECT_FLOAT_EQ(LightEmitter3DDistanceFadeWeight(41.0F, 40.0F, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(LightEmitter3DDistanceFadeWeight(40.0F, 40.0F, 0.0F), 1.0F);
    EXPECT_FLOAT_EQ(LightEmitter3DDistanceFadeWeight(std::numeric_limits<float>::quiet_NaN(), 40.0F, 10.0F), 0.0F);

    EXPECT_TRUE(IsLightEmitterComponentValid(LightEmitterComponent{}));
    LightEmitterComponent light{};
    light.shadowOpacity = 2.0F;
    EXPECT_FALSE(IsLightEmitterComponentValid(light));
    light = {};
    light.energy = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsLightEmitterComponentValid(light));

    EXPECT_TRUE(IsDecal3DObjectComponentValid(Decal3DComponent{}));
    Decal3DComponent decal{};
    decal.albedoMix = -0.1F;
    EXPECT_FALSE(IsDecal3DObjectComponentValid(decal));

    EXPECT_TRUE(IsFogVolume3DObjectComponentValid(FogVolume3DComponent{}));
    FogVolume3DComponent fog{};
    fog.density = -2.0F; // Negative density is allowed: it clears fog.
    EXPECT_TRUE(IsFogVolume3DObjectComponentValid(fog));
    fog.size.y = 0.0F;
    EXPECT_FALSE(IsFogVolume3DObjectComponentValid(fog));
    fog = {};
    fog.shape = static_cast<FogVolumeShape>(9);
    EXPECT_FALSE(IsFogVolume3DObjectComponentValid(fog));
}

TEST_F(Object3DDefinitionsTest, Skeleton3DIsAObject3DChildThatStartsWithNoBones) {
    const Entity skeleton = CreateEntity();
    ApplySkeleton3DObjectDefinition(entityManager, skeleton, Skeleton3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, skeleton, "Skeleton3D");
    EXPECT_TRUE(entityManager.HasComponent<VisibilityComponent>(skeleton));
    EXPECT_TRUE(entityManager.HasComponent<ObjectMetadataComponent>(skeleton));
    EXPECT_TRUE(entityManager.GetComponent<Skeleton3DComponent>(skeleton).bones.empty());
}

TEST_F(Object3DDefinitionsTest, Object3DApplyIsIdempotent) {
    const Entity entity = CreateEntity();
    ApplyObject3DObjectDefinition(entityManager, entity, Object3DObjectDefinition{});
    entityManager.GetComponent<TransformComponent>(entity).localPosition =
        Math::Vector3{1.0F, 2.0F, 3.0F};

    ApplyObject3DObjectDefinition(entityManager, entity, Object3DObjectDefinition{});
    EXPECT_EQ(entityManager.GetComponent<TransformComponent>(entity).localPosition.y, 2.0F);
    EXPECT_EQ(entityManager.GetComponent<NameComponent>(entity).name,
              Object3DObjectDefinition::defaultName);
}

TEST_F(Object3DDefinitionsTest, Object3DApplyRefusesADestroyedEntity) {
    const Entity entity = CreateEntity();
    entityManager.DestroyEntity(entity);
    // Same refusal as the scene-root apply: dead entities get nothing, and nothing crashes.
    ApplyObject3DObjectDefinition(entityManager, entity, Object3DObjectDefinition{});
    EXPECT_FALSE(entityManager.IsAlive(entity));
}

TEST_F(Object3DDefinitionsTest, SceneRootIsAPureObjectCarryingTheCommonObjectSection) {
    const Entity root = CreateEntity();
    ApplySceneRootObjectDefinition(entityManager, root, SceneRootObjectDefinition{});

    // In the hierarchy and named, but with no transform: placing things in space is what Object3D
    // adds, and the root has nothing to place. Its children start their own transform chains.
    EXPECT_FALSE(entityManager.HasComponent<TransformComponent>(root));
    EXPECT_FALSE(entityManager.HasComponent<WorldTransformComponent>(root));
    EXPECT_TRUE(entityManager.HasComponent<HierarchyComponent>(root));
    EXPECT_TRUE(entityManager.HasComponent<NameComponent>(root));
    EXPECT_EQ(entityManager.GetComponent<NameComponent>(root).name,
              SceneRootObjectDefinition::defaultName);
    EXPECT_TRUE(entityManager.HasComponent<SceneRootComponent>(root));

    // The root's Inspector offers no Add Component, so the whole common Object section is attached
    // here - anything left out could never be reached from the editor.
    EXPECT_TRUE(entityManager.HasComponent<ProcessComponent>(root));
    EXPECT_TRUE(entityManager.HasComponent<ThreadGroupComponent>(root));
    EXPECT_TRUE(entityManager.HasComponent<PhysicsInterpolationComponent>(root));
    EXPECT_TRUE(entityManager.HasComponent<AutoTranslateComponent>(root));
    EXPECT_TRUE(entityManager.HasComponent<EditorDescriptionComponent>(root));
    EXPECT_TRUE(entityManager.HasComponent<ScriptComponent>(root));
    EXPECT_TRUE(entityManager.HasComponent<ObjectMetadataComponent>(root));

    // And it is still the idempotent recipe the document lifecycle relies on: applying twice
    // adds nothing and changes nothing, including an authored value.
    entityManager.GetComponent<ProcessComponent>(root).priority = 7;
    ApplySceneRootObjectDefinition(entityManager, root, SceneRootObjectDefinition{});
    EXPECT_EQ(entityManager.GetComponent<NameComponent>(root).name,
              SceneRootObjectDefinition::defaultName);
    EXPECT_EQ(entityManager.GetComponent<ProcessComponent>(root).priority, 7);
    EXPECT_FALSE(entityManager.HasComponent<TransformComponent>(root));
}

TEST_F(Object3DDefinitionsTest, SceneRootMigrationBakesAnOldRootTransformIntoItsChildren) {
    // A scene saved when the root still had a transform: the root moved, rotated and scaled, and
    // every child's world pose was composed through it. Dropping the transform must move nothing.
    const Entity root = CreateEntity();
    TransformComponent rootTransform{};
    rootTransform.localPosition = Math::Vector3{10.0F, -2.0F, 4.0F};
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, 1.2F, rootTransform.localRotation));
    rootTransform.localScale = Math::Vector3{2.0F, 2.0F, 2.0F};
    sceneGraph.AttachTransform(entityManager, root, rootTransform);

    const Entity child = CreateEntity();
    TransformComponent childTransform{};
    childTransform.localPosition = Math::Vector3{1.0F, 0.5F, -3.0F};
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{1.0F, 0.0F, 0.0F}, 0.4F, childTransform.localRotation));
    childTransform.localScale = Math::Vector3{0.5F, 1.0F, 1.5F};
    sceneGraph.AttachTransform(entityManager, child, childTransform);
    sceneGraph.SetParent(entityManager, child, root);

    // A top-level child never composed from the root, so the migration must not touch it.
    const Entity topLevelChild = CreateEntity();
    TransformComponent topLevelTransform{};
    topLevelTransform.localPosition = Math::Vector3{-5.0F, 0.0F, 0.0F};
    topLevelTransform.topLevel = true;
    sceneGraph.AttachTransform(entityManager, topLevelChild, topLevelTransform);
    sceneGraph.SetParent(entityManager, topLevelChild, root);

    sceneGraph.Update(entityManager);
    const WorldTransformComponent childBefore = entityManager.GetComponent<WorldTransformComponent>(child);
    const WorldTransformComponent topLevelBefore =
        entityManager.GetComponent<WorldTransformComponent>(topLevelChild);

    ApplySceneRootObjectDefinition(entityManager, root, SceneRootObjectDefinition{});
    ASSERT_FALSE(entityManager.HasComponent<TransformComponent>(root));
    sceneGraph.Update(entityManager);

    const WorldTransformComponent& childAfter = entityManager.GetComponent<WorldTransformComponent>(child);
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
    EXPECT_EQ(entityManager.GetComponent<TransformComponent>(child).rotationEditMode,
              RotationEditMode::Quaternion);

    const WorldTransformComponent& topLevelAfter =
        entityManager.GetComponent<WorldTransformComponent>(topLevelChild);
    EXPECT_EQ(topLevelAfter.worldPosition, topLevelBefore.worldPosition);
    EXPECT_EQ(entityManager.GetComponent<TransformComponent>(topLevelChild).localPosition,
              topLevelTransform.localPosition);

    // Idempotent: a second apply finds no transform and bakes nothing twice.
    ApplySceneRootObjectDefinition(entityManager, root, SceneRootObjectDefinition{});
    sceneGraph.Update(entityManager);
    EXPECT_NEAR(entityManager.GetComponent<WorldTransformComponent>(child).worldPosition.x,
                childBefore.worldPosition.x, kTolerance);
}

TEST_F(Object3DDefinitionsTest, SpringArm3DIsRegisteredCreatableAsACameraObject) {
    // The registry row and the editor switch must keep agreeing about this kind: the registry
    // advertises it as a creatable cameran object, and the switch now creates it from the same
    // definition this test file pins.
    const Objects::SceneObjectDescriptor* descriptor =
        Objects::FindSceneObjectDescriptor(Objects::SceneObjectKind::SpringArm3D);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->typeId, "spring_arm_3d");
    EXPECT_EQ(descriptor->displayName, "SpringArm3D");
    EXPECT_TRUE(descriptor->libraryCreatable);
}

TEST_F(Object3DDefinitionsTest, SpringArmTargetResolutionMatchesTheAuthoredEnvelope) {
    // Unobstructed: full reach.
    EXPECT_EQ(ResolveSpringArm3DTarget(std::nullopt, 0.1F, 4.0F), 4.0F);
    // Hit reported by the raycast: distance minus margin.
    EXPECT_NEAR(ResolveSpringArm3DTarget(1.5F, 0.1F, 4.0F), 1.4F, 1.0e-6F);
    // Hit closer than the margin itself: never negative.
    EXPECT_EQ(ResolveSpringArm3DTarget(0.05F, 0.1F, 4.0F), 0.0F);
    // A hit report inconsistent with the arm's own envelope degrades to "unobstructed" instead
    // of stretching the arm past its authored length.
    EXPECT_EQ(ResolveSpringArm3DTarget(9.0F, 0.1F, 4.0F), 4.0F);
    EXPECT_EQ(ResolveSpringArm3DTarget(std::numeric_limits<float>::quiet_NaN(), 0.1F, 4.0F), 4.0F);
    // And absurd authored envelopes refuse politely the same way.
    EXPECT_EQ(ResolveSpringArm3DTarget(1.0F, -1.0F, 4.0F), 4.0F);
    EXPECT_EQ(ResolveSpringArm3DTarget(1.0F, 0.1F, 0.0F), 0.0F);
}

TEST_F(Object3DDefinitionsTest, SpringArmRetractionSnapsSoTheCameraNeverClips) {
    // Obstruction appears mid-frame: the arm arrives at the target THIS step, not after a
    // smooth glide through the wall.
    EXPECT_EQ(ResolveSpringArm3DLength(4.0F, 1.4F, 8.0F, 1.0F / 60.0F), 1.4F);
    // Already retracted, target deeper still: still snaps.
    EXPECT_EQ(ResolveSpringArm3DLength(2.0F, 1.4F, 8.0F, 1.0F / 60.0F), 1.4F);
}

TEST_F(Object3DDefinitionsTest, SpringArmExtensionBlendsMonotonicallyAndNeverOvershoots) {
    float current = 1.4F;
    constexpr float kDt = 1.0F / 60.0F;
    for (int step = 0; step < 240; ++step) {
        const float before = current;
        current = ResolveSpringArm3DLength(current, 4.0F, 8.0F, kDt);
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

TEST_F(Object3DDefinitionsTest, SpringArmSmoothingZeroMatchesGodotSnapBothWays) {
    // The authored escape hatch: smoothing 0 reproduces Godot's SpringArm3D behaviour exactly
    // (Godot ships no smoothing member at all - snap on the way out as well).
    EXPECT_EQ(ResolveSpringArm3DLength(1.4F, 4.0F, 0.0F, 1.0F / 60.0F), 4.0F);
}

TEST_F(Object3DDefinitionsTest, SpringArmLengthRefusesDegenerateCallsWithoutMoving) {
    EXPECT_EQ(ResolveSpringArm3DLength(2.0F, 4.0F, 8.0F, 0.0F), 2.0F);   // dt 0: frozen
    EXPECT_EQ(ResolveSpringArm3DLength(2.0F, 4.0F, 8.0F, -1.0F), 2.0F);  // dt negative: frozen
    // A NaN input stays the caller's (the validator's) problem: the law returns its own current
    // length unchanged rather than smearing garbage through the blend. NaN never compares equal,
    // so these check the value category directly, not EXPECT_EQ.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(std::isnan(ResolveSpringArm3DLength(nan, 4.0F, 8.0F, 1.0F / 60.0F)));
    EXPECT_EQ(ResolveSpringArm3DLength(2.0F, nan, 8.0F, 1.0F / 60.0F), 2.0F);
}

TEST_F(Object3DDefinitionsTest, SpringArmObstructThenClearRestoresTheAuthoredPose) {
    // The drift claim the whole child-delta design rests on, measured on the motion law itself:
    // an arm that snaps to a wall and springs back home must return to EXACTLY its authored
    // length, so the sum of every per-step child shift telescopes back to precisely zero extra
    // offset - not asymptotically close to it.
    float current = 4.0F;
    float childLocalZ = 4.0F; // camera authored at full reach behind the pivot
    constexpr float kAuthoredZ = 4.0F;
    constexpr float kDt = 1.0F / 60.0F;

    // Drive into a doorway over 5 steps; each step snaps deeper or holds, and the child rides.
    const float doorwayTarget = ResolveSpringArm3DTarget(0.6F, 0.1F, 4.0F);
    float maximumDrift = 0.0F;
    for (int step = 0; step < 5; ++step) {
        const float next = ResolveSpringArm3DLength(current, doorwayTarget, 8.0F, kDt);
        childLocalZ += next - current;
        current = next;
        EXPECT_LT(current, kAuthoredZ);
    }
    // Then 600 steps of open air: springs back, settles exactly, and the child is returned home.
    for (int step = 0; step < 600; ++step) {
        const float next = ResolveSpringArm3DLength(current, 4.0F, 8.0F, kDt);
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

TEST_F(Object3DDefinitionsTest, SpawnPointSelectionIsDeterministicContentOrder) {
    // No candidates, no spawn.
    EXPECT_EQ(ResolveSpawnPoint3DSelection(std::span<const SpawnPoint3DCandidate>{}),
              std::nullopt);

    // The predicate the resolver and the spawn query's result order both read: ascending
    // (index, generation), strictly, so equal handles never re-order each other.
    EXPECT_TRUE(SortsBeforeSpawnPoint(Entity{1U, 0U}, Entity{2U, 0U}));
    EXPECT_TRUE(SortsBeforeSpawnPoint(Entity{1U, 0U}, Entity{1U, 1U}));
    EXPECT_FALSE(SortsBeforeSpawnPoint(Entity{1U, 1U}, Entity{1U, 0U}));
    EXPECT_FALSE(SortsBeforeSpawnPoint(Entity{2U, 0U}, Entity{1U, 9U}));
    EXPECT_FALSE(SortsBeforeSpawnPoint(Entity{3U, 2U}, Entity{3U, 2U}));
    // Sentinels are filtered again at this seam too: a caller bug must not become the spawn.
    const SpawnPoint3DCandidate sentinel{kInvalidEntity, false};
    {
        const SpawnPoint3DCandidate only[]{sentinel};
        EXPECT_EQ(ResolveSpawnPoint3DSelection(only), std::nullopt);
    }

    const SpawnPoint3DCandidate a{Entity{7U, 0U}, false};
    const SpawnPoint3DCandidate b{Entity{2U, 1U}, true};
    const SpawnPoint3DCandidate c{Entity{2U, 0U}, false};
    {
        const SpawnPoint3DCandidate only[]{a};
        EXPECT_EQ(ResolveSpawnPoint3DSelection(only), a.entity);
    }
    // Stable content order = lexicographic (index, generation): iteration order is irrelevant,
    // the same scene spawns the same way every time.
    {
        const SpawnPoint3DCandidate all[]{a, b, c};
        EXPECT_EQ(ResolveSpawnPoint3DSelection(all), c.entity);
    }
    {
        const SpawnPoint3DCandidate reversed[]{c, b, a};
        EXPECT_EQ(ResolveSpawnPoint3DSelection(reversed), c.entity);
    }
    {
        const SpawnPoint3DCandidate mixed[]{b, a, sentinel, c};
        EXPECT_EQ(ResolveSpawnPoint3DSelection(mixed), c.entity);
    }
    // And `oneShot` plays no part in the ranking - it only spends the winner afterwards.
    const SpawnPoint3DCandidate d{Entity{1U, 0U}, true};
    {
        const SpawnPoint3DCandidate pair[]{c, d};
        EXPECT_EQ(ResolveSpawnPoint3DSelection(pair), d.entity);
    }
}

TEST_F(Object3DDefinitionsTest, SpawnPoseComposeAppliesTheAuthoredOffsetInObjectSpace) {
    // Identity object: the offset is the pose.
    const std::optional<SpawnPoint3DPose> flat =
        ComposeSpawnPointPose({}, {}, Math::Vector3{0.0F, 1.0F, 0.0F}, {});
    ASSERT_TRUE(flat.has_value());
    EXPECT_NEAR(flat->position.y, 1.0F, 1.0e-6F);

    // Translated object: object position adds after the offset is rotated.
    const Math::Quaternion halfTurnAboutY{0.0F, 1.0F, 0.0F, 0.0F};
    const std::optional<SpawnPoint3DPose> posed = ComposeSpawnPointPose(
        Math::Vector3{10.0F, 0.0F, 10.0F}, halfTurnAboutY, Math::Vector3{1.0F, 0.0F, 0.0F},
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
    EXPECT_FALSE(ComposeSpawnPointPose(Math::Vector3{nan, 0.0F, 0.0F}, {}, {}, {}).has_value());
    EXPECT_FALSE(ComposeSpawnPointPose({}, Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F}, {}, {})
                     .has_value());
}

TEST_F(Object3DDefinitionsTest, SpawnPlayerLocalIsTheSweepInverse) {
    const SpawnPoint3DPose worldPose{Math::Vector3{9.0F, 2.0F, -2.0F}, {}};

    // Root-level player (identity parent TRS): the pose falls straight through.
    const std::optional<SpawnPoint3DPose> rootLocal =
        ResolveSpawnPointPlayerLocal(worldPose, {}, {}, Math::Vector3{1.0F, 1.0F, 1.0F});
    ASSERT_TRUE(rootLocal.has_value());
    EXPECT_NEAR(rootLocal->position.x, 9.0F, 1.0e-6F);

    // Translated parent: subtract, then divide scale component-wise.
    const std::optional<SpawnPoint3DPose> scaled = ResolveSpawnPointPlayerLocal(
        worldPose, Math::Vector3{5.0F, 1.0F, -2.0F}, {}, Math::Vector3{2.0F, 1.0F, 0.5F});
    ASSERT_TRUE(scaled.has_value());
    EXPECT_NEAR(scaled->position.x, 2.0F, 1.0e-5F);
    EXPECT_NEAR(scaled->position.y, 1.0F, 1.0e-5F);
    EXPECT_NEAR(scaled->position.z, 0.0F, 1.0e-5F);

    // Rotated parent (180 degrees about Y): the offset un-rotates on entry.
    const Math::Quaternion halfTurnAboutY{0.0F, 1.0F, 0.0F, 0.0F};
    const std::optional<SpawnPoint3DPose> turned = ResolveSpawnPointPlayerLocal(
        SpawnPoint3DPose{Math::Vector3{8.0F, 0.0F, 3.0F}, {}}, Math::Vector3{10.0F, 0.0F, 3.0F},
        halfTurnAboutY, Math::Vector3{1.0F, 1.0F, 1.0F});
    ASSERT_TRUE(turned.has_value());
    EXPECT_NEAR(turned->position.x, 2.0F, 1.0e-5F);
    // Parent rotation inverts onto the spawn rotation; with identity spawn that hands the
    // parent's own half-turn back (180 degrees about Y maps (1,0,0) to (-1,0,0)).
    const Math::Vector3 mapped = Math::RotateVector(turned->rotation, {1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(mapped.x, -1.0F, 1.0e-5F);

    // The property the whole feature rests on: feed the solved local pose back through the
    // sweep's forward formula and land on the spawn pose again - measured, not asserted away.
    {
        const SpawnPoint3DPose pose{Math::Vector3{3.5F, -1.0F, 8.0F}, halfTurnAboutY};
        const Math::Vector3 parentPos{-4.0F, 2.0F, 1.0F};
        const Math::Quaternion parentRot{0.0F, 0.0F, 1.0F, 0.0F}; // 180 degrees about Z
        const Math::Vector3 parentScale{2.0F, 3.0F, 0.5F};
        const std::optional<SpawnPoint3DPose> local =
            ResolveSpawnPointPlayerLocal(pose, parentPos, parentRot, parentScale);
        ASSERT_TRUE(local.has_value());
        const Math::Vector3 roundTrip =
            parentPos + Math::RotateVector(parentRot, parentScale * local->position);
        EXPECT_NEAR(roundTrip.x, pose.position.x, 1.0e-4F);
        EXPECT_NEAR(roundTrip.y, pose.position.y, 1.0e-4F);
        EXPECT_NEAR(roundTrip.z, pose.position.z, 1.0e-4F);
        const Math::Vector3 forwardA = Math::RotateVector(
            Math::Multiply(parentRot, local->rotation), {0.0F, 0.0F, 1.0F});
        const Math::Vector3 forwardB = Math::RotateVector(pose.rotation, {0.0F, 0.0F, 1.0F});
        EXPECT_NEAR(forwardA.x, forwardB.x, 1.0e-4F);
        EXPECT_NEAR(forwardA.y, forwardB.y, 1.0e-4F);
    }

    // Refusals: a zero-scaled parent axis has no local pose to solve into, and garbage stays out.
    EXPECT_FALSE(ResolveSpawnPointPlayerLocal(worldPose, {}, {}, Math::Vector3{0.0F, 1.0F, 1.0F})
                     .has_value());
    EXPECT_FALSE(ResolveSpawnPointPlayerLocal(worldPose, Math::Vector3{
                                                 std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                                                 {}, Math::Vector3{1.0F, 1.0F, 1.0F})
                     .has_value());
}

TEST_F(Object3DDefinitionsTest, InteractionAreaCandidateCapHonoursAuthoredBudgetAndStorageBound) {
    // The per-tick interactor list is storage-bounded AND authored-bounded; the effective cap is
    // the smaller of the two, so an authored value above the fixed array can never scribble past
    // it, and a tighter authored budget is respected exactly.
    EXPECT_EQ(ResolveInteractionAreaCandidateCap(4U, kMaximumInteractionAreaCandidates), 4U);
    EXPECT_EQ(ResolveInteractionAreaCandidateCap(16U, kMaximumInteractionAreaCandidates), 16U);
    EXPECT_EQ(ResolveInteractionAreaCandidateCap(4096U, kMaximumInteractionAreaCandidates),
              kMaximumInteractionAreaCandidates);
    EXPECT_EQ(ResolveInteractionAreaCandidateCap(0U, kMaximumInteractionAreaCandidates), 0U);
    // The bound side is honoured symmetrically for storage smaller than the authored budget.
    EXPECT_EQ(ResolveInteractionAreaCandidateCap(8U, 3U), 3U);
}

TEST_F(Object3DDefinitionsTest, PrimaryInteractorSelectionMatchesTheSpawnPointPlayerRule) {
    // Same contract as SpawnPoint3D selection: content order (index, generation) decides, never
    // ECS pool order, and an empty or garbage-only input fails closed to no value.
    EXPECT_EQ(ResolvePrimaryInteractor(std::span<const Entity>{}), std::nullopt);
    const Entity sentinel = kInvalidEntity;
    {
        const Entity only[]{sentinel};
        EXPECT_EQ(ResolvePrimaryInteractor(only), std::nullopt);
    }
    const Entity a{7U, 0U};
    const Entity b{2U, 1U};
    const Entity c{2U, 0U};
    {
        const Entity only[]{a};
        ASSERT_TRUE(ResolvePrimaryInteractor(only).has_value());
        EXPECT_EQ(*ResolvePrimaryInteractor(only), a);
    }
    {
        // Listed in "wrong" order on purpose: (2,1) and (2,0) both precede (7,0), and between the
        // two index-2 entries the lower generation wins - identical ranking to spawn selection.
        const Entity all[]{a, b, c};
        ASSERT_TRUE(ResolvePrimaryInteractor(all).has_value());
        EXPECT_EQ(*ResolvePrimaryInteractor(all), c);
    }
}

TEST_F(Object3DDefinitionsTest, InteractionFocusPicksTheNearestAreaWithDeterministicTies) {
    // The Lyra-style best-candidate rule this engine owns so games do not re-implement it: the
    // nearest overlapping area wins; equal distances fall back to (index,generation) ordering so
    // the answer never depends on iteration/pool order. Empty or garbage input means no focus.
    EXPECT_EQ(ResolveInteractionFocus(std::span<const InteractionFocusCandidate>{}),
              std::nullopt);
    const InteractionFocusCandidate sentinel{kInvalidEntity, 0.0F};
    {
        const InteractionFocusCandidate only[]{sentinel};
        EXPECT_EQ(ResolveInteractionFocus(only), std::nullopt);
    }
    const InteractionFocusCandidate near{Entity{9U, 0U}, 1.0F};
    const InteractionFocusCandidate far{Entity{1U, 0U}, 4.0F};
    {
        // A worse entity wins anyway because it is nearer; order in the span is irrelevant.
        const InteractionFocusCandidate all[]{far, near};
        ASSERT_TRUE(ResolveInteractionFocus(all).has_value());
        EXPECT_EQ(*ResolveInteractionFocus(all), near.areaEntity);
    }
    {
        // The exact tie: equal distances, entities listed in REVERSE ranked order - the lower
        // (index,generation) still wins, which is the determinism claim this test is measuring.
        const InteractionFocusCandidate tieA{Entity{5U, 0U}, 2.0F};
        const InteractionFocusCandidate tieB{Entity{2U, 0U}, 2.0F};
        const InteractionFocusCandidate reversed[]{tieA, tieB};
        ASSERT_TRUE(ResolveInteractionFocus(reversed).has_value());
        EXPECT_EQ(*ResolveInteractionFocus(reversed), tieB.areaEntity);
        const InteractionFocusCandidate natural[]{tieB, tieA};
        ASSERT_TRUE(ResolveInteractionFocus(natural).has_value());
        EXPECT_EQ(*ResolveInteractionFocus(natural), tieB.areaEntity);
    }
}

TEST_F(Object3DDefinitionsTest, MarkerPoseComposeSharesTheSpawnPointCompositionContract) {
    // The pure half of fly-to-marker: the same measured composition contract the spawn point
    // owns - position = object position + object rotation * authored offset, rotation composes,
    // object scale stays out of it - so a marker's viewpoint tracks prefab-level transforms
    // identically to every other authored-offset object in the engine.
    const std::optional<Marker3DPose> flat =
        ComposeMarker3DPose({}, {}, Math::Vector3{0.0F, 0.0F, -4.0F}, {});
    ASSERT_TRUE(flat.has_value());
    EXPECT_NEAR(flat->position.z, -4.0F, 1.0e-6F);

    const Math::Quaternion halfTurnAboutY{0.0F, 1.0F, 0.0F, 0.0F};
    const std::optional<Marker3DPose> posed = ComposeMarker3DPose(
        Math::Vector3{10.0F, 0.0F, 10.0F}, halfTurnAboutY,
        Math::Vector3{1.0F, 0.0F, 0.0F}, {});
    ASSERT_TRUE(posed.has_value());
    // 180 degrees about Y maps (1,0,0) to (-1,0,0), then the object position adds.
    EXPECT_NEAR(posed->position.x, 9.0F, 1.0e-5F);
    EXPECT_NEAR(posed->position.z, 10.0F, 1.0e-5F);
    EXPECT_NEAR(posed->rotation.y, 1.0F, 1.0e-5F);
    EXPECT_NEAR(posed->rotation.w, 0.0F, 1.0e-5F);

    // A rotated marker composes its facing under the object's rotation, not around it.
    const Math::Quaternion quarterTurnAboutY{0.0F, 0.70710678F, 0.0F, 0.70710678F};
    const std::optional<Marker3DPose> faced = ComposeMarker3DPose(
        Math::Vector3{3.0F, 1.0F, -2.0F}, quarterTurnAboutY, {}, halfTurnAboutY);
    ASSERT_TRUE(faced.has_value());
    const Math::Quaternion expectedFacing =
        Math::Multiply(quarterTurnAboutY, halfTurnAboutY);
    const Math::Vector3 expectedForward =
        Math::RotateVector(expectedFacing, Math::Vector3{0.0F, 0.0F, -1.0F});
    const Math::Vector3 actualForward =
        Math::RotateVector(faced->rotation, Math::Vector3{0.0F, 0.0F, -1.0F});
    EXPECT_NEAR(actualForward.x, expectedForward.x, 1.0e-5F);
    EXPECT_NEAR(actualForward.y, expectedForward.y, 1.0e-5F);
    EXPECT_NEAR(actualForward.z, expectedForward.z, 1.0e-5F);

    // Garbage in, no viewpoint out - fail-closed like every other 3D resolver.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ComposeMarker3DPose(Math::Vector3{nan, 0.0F, 0.0F}, {}, {}, {}).has_value());
    EXPECT_FALSE(ComposeMarker3DPose({}, Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F}, {}, {})
                     .has_value());
    EXPECT_FALSE(ComposeMarker3DPose({}, {}, Math::Vector3{nan, 0.0F, 0.0F}, {}).has_value());
}

TEST_F(Object3DDefinitionsTest, AnimationGraphIsCreatableAndValidatesItsBlend) {
    EXPECT_TRUE(IsAnimationGraphObjectDefinitionValid(AnimationGraphObjectDefinition{}));
    AnimationGraphObjectDefinition noOutput;
    noOutput.tree.nodes.erase(noOutput.tree.nodes.begin());
    EXPECT_FALSE(IsAnimationGraphObjectDefinitionValid(noOutput));
    const Objects::SceneObjectDescriptor* descriptor =
        Objects::FindSceneObjectDescriptor(Objects::SceneObjectKind::AnimationGraph);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_TRUE(descriptor->libraryCreatable);
}

TEST_F(Object3DDefinitionsTest, CameraCurrentIsExclusiveAndSkipsEditorInternal) {
    const Entity first = CreateEntity();
    ApplyCamera3DObjectDefinition(entityManager, first, Camera3DObjectDefinition{});
    const Entity second = CreateEntity();
    ApplyCamera3DObjectDefinition(entityManager, second, Camera3DObjectDefinition{});
    EXPECT_TRUE(entityManager.GetComponent<CameraComponent>(first).current);
    EXPECT_FALSE(entityManager.GetComponent<CameraComponent>(second).current);
    ASSERT_TRUE(FindCurrentCameraEntity(entityManager).has_value());
    EXPECT_EQ(*FindCurrentCameraEntity(entityManager), first);

    MakeCameraCurrent(entityManager, second);
    EXPECT_FALSE(entityManager.GetComponent<CameraComponent>(first).current);
    EXPECT_TRUE(entityManager.GetComponent<CameraComponent>(second).current);
    ASSERT_TRUE(FindCurrentCameraEntity(entityManager).has_value());
    EXPECT_EQ(*FindCurrentCameraEntity(entityManager), second);

    const Entity internal = CreateEntity();
    sceneGraph.AttachTransform(entityManager, internal, TransformComponent{});
    entityManager.AddComponent<CameraComponent>(internal);
    entityManager.AddComponent<EditorInternalEntityComponent>(internal);
    ASSERT_TRUE(FindCurrentCameraEntity(entityManager).has_value());
    EXPECT_EQ(*FindCurrentCameraEntity(entityManager), second);
    EXPECT_FALSE(IsDocumentCameraEntity(entityManager, internal));
}

} // namespace
// =================================================================================================
// Participation: what a PhysicsObject3D's Process mode and disable mode mean to the physics world.
// =================================================================================================

class PhysicsObjectParticipationTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    /// A body-shaped entity with a transform, a PhysicsObject component and a Process component
    /// whose mode the scene graph resolves the way the engine resolves it every frame.
    [[nodiscard]] Scene::Entity MakeObject(const Scene::TickMode mode,
                                                 const PhysicsObjectDisableMode disableMode) {
        const Scene::Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
        sceneGraph.Update(entityManager);
        Scene::ProcessComponent process{};
        process.mode = mode;
        entityManager.AddComponent<Scene::ProcessComponent>(entity, process);
        PhysicsObjectComponent object{};
        object.disableMode = disableMode;
        entityManager.AddComponent<PhysicsObjectComponent>(entity, object);
        sceneGraph.Update(entityManager);
        return entity;
    }
};

TEST_F(Object3DDefinitionsTest, ARigid3DBodyKeepsAColliderItWasGivenAndGetsOneWhenItHasNone) {
    // The recipe only fills in what is missing, so a body whose collider was authored - a layer, a
    // capsule, half extents - keeps every value when the definition is applied over it.
    const Entity authored = entityManager.CreateEntity();
    ColliderComponent shape{};
    shape.shapeType = ColliderShapeType::Capsule;
    shape.radius = 0.25F;
    shape.height = 1.0F;
    shape.collisionLayer = 4U;
    entityManager.AddComponent<ColliderComponent>(authored, shape);
    ApplyRigid3DObjectDefinition(entityManager, authored, Rigid3DObjectDefinition{});
    const ColliderComponent& kept = entityManager.GetComponent<ColliderComponent>(authored);
    EXPECT_EQ(kept.shapeType, ColliderShapeType::Capsule);
    EXPECT_FLOAT_EQ(kept.radius, 0.25F);
    EXPECT_EQ(kept.collisionLayer, 4U);

    // A wrong collider is a wrong definition: the validator refuses it rather than letting the
    // edit create a body that can never be collided with.
    Rigid3DObjectDefinition invalid{};
    invalid.collider.collisionLayer = 0U;
    EXPECT_FALSE(IsRigid3DObjectDefinitionValid(invalid));
}

TEST_F(Object3DDefinitionsTest, AFrozenRigid3DIsOutOfTheSimulationWithoutBeingDeleted) {
    // The reason a Rigid3D carries the physics object base: its Process mode and disable mode
    // answer the question a game keeps asking - "stop this body, but keep it in the scene".
    const Entity entity = CreateEntity();
    ApplyRigid3DObjectDefinition(entityManager, entity, Rigid3DObjectDefinition{});

    // Running (the default) - simulated, in the world, and colliding like anything else.
    EXPECT_TRUE(IsPhysicsObjectSimulated(entityManager, entity));
    EXPECT_TRUE(IsPhysicsObjectInWorld(entityManager, entity));

    // The Object3D baseline already attached the Process component: the object's schedule is
    // authored on it, not re-added on top of it.
    ASSERT_TRUE(entityManager.HasComponent<ProcessComponent>(entity));
    entityManager.GetComponent<ProcessComponent>(entity).mode = TickMode::Never;
    sceneGraph.Update(entityManager);

    // Default disable mode is Remove: out of the world entirely, which the collider cache, every
    // query and the simulation all honour through the one rule.
    EXPECT_FALSE(IsPhysicsObjectSimulated(entityManager, entity));
    EXPECT_FALSE(IsPhysicsObjectInWorld(entityManager, entity));

    // Authored as MakeStatic instead, it stays in the world as an obstacle nothing can move.
    entityManager.GetComponent<PhysicsObjectComponent>(entity).disableMode =
        PhysicsObjectDisableMode::MakeStatic;
    EXPECT_FALSE(IsPhysicsObjectSimulated(entityManager, entity));
    EXPECT_TRUE(IsPhysicsObjectInWorld(entityManager, entity));
}
TEST_F(Object3DDefinitionsTest, BodyKindsWithShapesAreToldApartByTheirControllerNotTheirCollider) {
    // A Rigid3D and a Character3D are both a collider plus a body. The type has to come from the
    // controller: a body with a shape and no controller is a Rigid3D, and one with it is a
    // character.
    const Entity rigid = entityManager.CreateEntity();
    ApplyRigid3DObjectDefinition(entityManager, rigid, Rigid3DObjectDefinition{});
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, rigid), Objects::SceneObjectKind::Rigid3D);

    const Entity character = entityManager.CreateEntity();
    ApplyCharacter3DObjectDefinition(entityManager, character, Character3DObjectDefinition{});
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, character), Objects::SceneObjectKind::Character3D);
    ASSERT_TRUE(entityManager.HasComponent<PawnComponent>(character));
    ASSERT_TRUE(entityManager.HasComponent<ControllerComponent>(character));
    EXPECT_EQ(entityManager.GetComponent<ControllerComponent>(character).kind,
              ControllerKind::AI);

    const Entity player = entityManager.CreateEntity();
    ApplyPlayer3DObjectDefinition(entityManager, player, Player3DObjectDefinition{});
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, player), Objects::SceneObjectKind::Player3D);
}

TEST_F(PhysicsObjectParticipationTest, AnEntityThatIsNotAPhysicsObjectIsLeftAlone) {
    // Nothing here says it should stop, and a body assembled by hand must not be quietly taken out
    // of the world by a system that was looking for a component that is not there.
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::ProcessComponent never{};
    never.mode = Scene::TickMode::Never;
    entityManager.AddComponent<Scene::ProcessComponent>(entity, never);

    EXPECT_EQ(ResolvePhysicsObjectParticipation(entityManager, entity, false),
              PhysicsObjectParticipation::Active);
    EXPECT_TRUE(IsPhysicsObjectInWorld(entityManager, entity));
    EXPECT_TRUE(IsPhysicsObjectSimulated(entityManager, entity));
}

TEST_F(PhysicsObjectParticipationTest, ARunningObjectParticipatesWhateverItsDisableModeSays) {
    // The disable mode is what happens while it is *not* running; an object that is running is
    // running, and authoring KeepActive must not make it immortal either.
    for (const PhysicsObjectDisableMode disableMode :
         {PhysicsObjectDisableMode::Remove, PhysicsObjectDisableMode::MakeStatic,
          PhysicsObjectDisableMode::KeepActive}) {
        const Scene::Entity entity = MakeObject(Scene::TickMode::Running, disableMode);
        EXPECT_EQ(ResolvePhysicsObjectParticipation(entityManager, entity, false),
                  PhysicsObjectParticipation::Active);
    }
}

TEST_F(PhysicsObjectParticipationTest, AStoppedObjectIsTakenOutKeptAsAStaticOrLeftRunning) {
    const Scene::Entity removed = MakeObject(Scene::TickMode::Never, PhysicsObjectDisableMode::Remove);
    EXPECT_EQ(ResolvePhysicsObjectParticipation(entityManager, removed, false),
              PhysicsObjectParticipation::Removed);
    EXPECT_FALSE(IsPhysicsObjectInWorld(entityManager, removed));
    EXPECT_FALSE(IsPhysicsObjectSimulated(entityManager, removed));

    // Kept in the world as an immovable obstacle: still found, still in the way, never moved.
    const Scene::Entity kept =
        MakeObject(Scene::TickMode::Never, PhysicsObjectDisableMode::MakeStatic);
    EXPECT_EQ(ResolvePhysicsObjectParticipation(entityManager, kept, false),
              PhysicsObjectParticipation::StaticOnly);
    EXPECT_TRUE(IsPhysicsObjectInWorld(entityManager, kept));
    EXPECT_FALSE(IsPhysicsObjectSimulated(entityManager, kept));

    // KeepActive is the author saying "this one keeps simulating while it is stopped" - the mode
    // exists so a scripted body can outlive its own schedule.
    const Scene::Entity active =
        MakeObject(Scene::TickMode::Never, PhysicsObjectDisableMode::KeepActive);
    EXPECT_EQ(ResolvePhysicsObjectParticipation(entityManager, active, false),
              PhysicsObjectParticipation::Active);
}

TEST_F(PhysicsObjectParticipationTest, ThePauseAnswerIsTheCallersAndTheFixedStepAsksWithFalse) {
    // A pause-only object is out of the world for the fixed step, which does not run while the
    // simulation is paused, and in it for anything asking on behalf of a paused world.
    const Scene::Entity entity =
        MakeObject(Scene::TickMode::PausedOnly, PhysicsObjectDisableMode::Remove);

    EXPECT_EQ(ResolvePhysicsObjectParticipation(entityManager, entity, /*simulationPaused=*/true),
              PhysicsObjectParticipation::Active);
    EXPECT_EQ(ResolvePhysicsObjectParticipation(entityManager, entity, /*simulationPaused=*/false),
              PhysicsObjectParticipation::Removed);

    // The two convenience questions always speak for the physics step, so a paused body is not
    // simulated by it either way.
    EXPECT_FALSE(IsPhysicsObjectInWorld(entityManager, entity));
    EXPECT_FALSE(IsPhysicsObjectSimulated(entityManager, entity));
}

TEST_F(PhysicsObjectParticipationTest, CollisionPriorityDefaultsToOneAndZeroMeansNeverYields) {
    const Scene::Entity plain = entityManager.CreateEntity();
    // No component at all is the neutral answer, not an error: everything that is not a physics
    // object weighs exactly as much as one authored with the defaults.
    EXPECT_FLOAT_EQ(GetPhysicsObjectCollisionPriority(entityManager, plain), 1.0F);
    EXPECT_FLOAT_EQ(GetPhysicsObjectYieldWeight(entityManager, plain, 0.5F), 0.5F);

    const Scene::Entity heavy = MakeObject(Scene::TickMode::Running, PhysicsObjectDisableMode::Remove);
    entityManager.GetComponent<PhysicsObjectComponent>(heavy).collisionPriority = 2.0F;
    // Twice the priority yields half as much of the same overlap.
    EXPECT_FLOAT_EQ(GetPhysicsObjectCollisionPriority(entityManager, heavy), 2.0F);
    EXPECT_FLOAT_EQ(GetPhysicsObjectYieldWeight(entityManager, heavy, 0.5F), 0.25F);

    // Zero is the authored "pinned in place": legal, and worth exactly no correction.
    entityManager.GetComponent<PhysicsObjectComponent>(heavy).collisionPriority = 0.0F;
    EXPECT_FLOAT_EQ(GetPhysicsObjectYieldWeight(entityManager, heavy, 0.5F), 0.0F);

    // A nonsense priority falls back to the neutral one rather than to something the solver would
    // have to special-case, and a body with no mass to give way with yields nothing whatever its
    // priority says.
    entityManager.GetComponent<PhysicsObjectComponent>(heavy).collisionPriority = -4.0F;
    EXPECT_FLOAT_EQ(GetPhysicsObjectCollisionPriority(entityManager, heavy), 1.0F);
    EXPECT_FLOAT_EQ(GetPhysicsObjectYieldWeight(entityManager, heavy, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(GetPhysicsObjectYieldWeight(entityManager, heavy, -1.0F), 0.0F);
}

TEST_F(PhysicsObjectParticipationTest, TheAbstractObjectDefinitionsStillAttachTheirComponents) {
    // The recipes and the rule live in one file, and the rule must not have replaced the recipes:
    // SolidBody3D is still the recipe that gives a body its physics object base, and an entity
    // built from it is Active until something says otherwise.
    const Scene::Entity entity = entityManager.CreateEntity();
    ApplySolidBody3DBase(entityManager, entity, "SolidBody3D");
    EXPECT_TRUE(entityManager.HasComponent<PhysicsObjectComponent>(entity));
    EXPECT_TRUE(entityManager.HasComponent<SolidBodyComponent>(entity));
    EXPECT_EQ(ResolvePhysicsObjectParticipation(entityManager, entity, false),
              PhysicsObjectParticipation::Active);
}

TEST(ObjectDefinitions3DTest, PrimitiveCollidersMatchTheKindTheyBelongTo) {
    // The editor converts a primitive in place (Cube -> Plane and so on) and refreshes the
    // collider to match the new kind. It reads these same definitions to do it, so this pins the
    // pairing the conversion depends on: each primitive kind's authored collider must be the one
    // its own definition declares, and a flat primitive must not inherit a cube's depth.
    EXPECT_EQ(BoxMesh3DObjectDefinition{}.mesh.kind, PrimitiveMeshKind::Cube);
    EXPECT_EQ(SphereMesh3DObjectDefinition{}.mesh.kind, PrimitiveMeshKind::UVSphere);
    EXPECT_EQ(PlaneMesh3DObjectDefinition{}.mesh.kind, PrimitiveMeshKind::Plane);

    // The plane is the one that actually differs, and the one a duplicated constant would get
    // wrong: it is thin on Y where the volumetric primitives are not.
    const Math::Vector3 planeExtents = PlaneMesh3DObjectDefinition{}.collider.halfExtents;
    const Math::Vector3 boxExtents = BoxMesh3DObjectDefinition{}.collider.halfExtents;
    EXPECT_LT(planeExtents.y, boxExtents.y) << "a plane's collider must be flatter than a cube's";
    EXPECT_FLOAT_EQ(planeExtents.x, boxExtents.x);
    EXPECT_FLOAT_EQ(planeExtents.z, boxExtents.z);

    // Every primitive definition must carry a collider that passes validation - an unauthored or
    // zeroed one would make the created entity fail IsColliderComponentValid downstream.
    EXPECT_TRUE(IsBoxMesh3DObjectDefinitionValid(BoxMesh3DObjectDefinition{}));
    EXPECT_TRUE(IsSphereMesh3DObjectDefinitionValid(SphereMesh3DObjectDefinition{}));
    EXPECT_TRUE(IsPlaneMesh3DObjectDefinitionValid(PlaneMesh3DObjectDefinition{}));
}

TEST(ObjectDefinitions3DTest, ComponentDomainHelpersCoverDisabledLightRigidAreaAndFollow) {
    DisabledComponent tag{};
    EXPECT_FALSE(ShouldSimulateEntity(&tag));
    EXPECT_TRUE(ShouldSimulateEntity(nullptr));
    EXPECT_TRUE(ShouldSkipEntitySimulation(&tag));
    EXPECT_TRUE(ShouldPropagateTransformWhenDisabled(&tag));

    LightComponent light{};
    light.type = LightType::Point;
    light.color = Math::Color{2.0F, 1.0F, 0.5F};
    light.intensity = 3.0F;
    light.range = 10.0F;
    const Math::Color radiant = EvaluateLightRadiantColor(light);
    EXPECT_FLOAT_EQ(radiant.r, 6.0F);
    EXPECT_GT(EvaluateLightDistanceAttenuation(light, 5.0F), 0.0F);
    EXPECT_FLOAT_EQ(EvaluateLightDistanceAttenuation(light, 12.0F), 0.0F);

    LightEmitterComponent emitter{};
    emitter.shadowEnabled = true;
    emitter.distanceFadeEnabled = true;
    emitter.distanceFadeBegin = 20.0F;
    emitter.distanceFadeLength = 10.0F;
    EXPECT_TRUE(DoesLightEmitterCastActiveShadow(emitter));
    EXPECT_TRUE(DoesLightEmitterAffectSurfaceLayerMask(emitter, 1U));
    EXPECT_FLOAT_EQ(EvaluateLightEmitterDistanceFade(emitter, 25.0F), 0.5F);

    Rigid3DComponent body{};
    body.mass = 2.0F;
    EXPECT_FLOAT_EQ(ResolveRigid3DInverseMass(body), 0.5F);
    ApplyRigid3DLinearImpulse(body, Math::Vector3{4.0F, 0.0F, 0.0F});
    EXPECT_FLOAT_EQ(body.velocity.x, 2.0F);

    AreaComponent area{};
    area.gravityDirection = Math::Vector3{0.0F, -1.0F, 0.0F};
    area.gravityMagnitude = 9.81F;
    const Math::Vector3 g = EvaluateAreaGravityVector(
        area, Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 2.0F, 3.0F});
    EXPECT_FLOAT_EQ(g.y, -9.81F);
}

TEST_F(Object3DDefinitionsTest, AudioListener3DRecipeAndActiveResolutionOverrideCameraPose) {
    const auto setWorldTransform = [this](const Entity entity,
                                          const WorldTransformComponent& world) {
        if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
            entityManager.RemoveComponent<WorldTransformComponent>(entity);
        }
        entityManager.AddComponent<WorldTransformComponent>(entity, world);
    };

    const Entity camera = CreateEntity();
    ApplyCamera3DObjectDefinition(entityManager, camera, Camera3DObjectDefinition{});
    setWorldTransform(camera, WorldTransformComponent{Math::Vector3{1.0F, 2.0F, 3.0F}});

    const Entity listenerA = CreateEntity();
    AudioListener3DObjectDefinition defA{};
    defA.listener.current = true;
    defA.listener.positionOffset = Math::Vector3{0.0F, 1.5F, 0.0F};
    ApplyAudioListener3DObjectDefinition(entityManager, listenerA, defA);
    ExpectObject3DBaseline(entityManager, listenerA, "AudioListener3D");
    setWorldTransform(listenerA,
                      WorldTransformComponent{Math::Vector3{10.0F, 20.0F, 30.0F}});

    const Entity listenerB = CreateEntity();
    AudioListener3DObjectDefinition defB{};
    defB.listener.current = true;
    defB.listener.ignoreRotation = true;
    ApplyAudioListener3DObjectDefinition(entityManager, listenerB, defB);
    setWorldTransform(listenerB,
                      WorldTransformComponent{
                          Math::Vector3{-4.0F, 5.0F, -6.0F},
                          Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F}});

    // Applying listenerB with current=true clears listenerA.current.
    EXPECT_FALSE(entityManager.GetComponent<AudioListener3DComponent>(listenerA).current);
    EXPECT_TRUE(entityManager.GetComponent<AudioListener3DComponent>(listenerB).current);

    ResolvedAudioListener3DPose pose{};
    ASSERT_TRUE(AudioListener3D::TryResolveActiveListenerPose(entityManager, camera, pose));
    EXPECT_EQ(pose.entity, listenerB);
    EXPECT_FLOAT_EQ(pose.position.x, -4.0F);
    EXPECT_FLOAT_EQ(pose.position.y, 5.0F);
    EXPECT_FLOAT_EQ(pose.position.z, -6.0F);
    // ignoreRotation keeps world-aligned forward/up.
    EXPECT_FLOAT_EQ(pose.forward.z, -1.0F);
    EXPECT_FLOAT_EQ(pose.up.y, 1.0F);

    AudioListener3D::MakeCurrent(entityManager, listenerA);
    ASSERT_TRUE(AudioListener3D::TryResolveActiveListenerPose(entityManager, camera, pose));
    EXPECT_EQ(pose.entity, listenerA);
    EXPECT_FLOAT_EQ(pose.position.y, 21.5F);
}

TEST_F(Object3DDefinitionsTest, RemoteTransform3DPushesSelectedChannelsToTargetEntity) {
    const Entity source = CreateEntity();
    const Entity target = CreateEntity();
    ApplyObject3DObjectDefinition(entityManager, target, Object3DObjectDefinition{});
    entityManager.GetComponent<TransformComponent>(target).localScale =
        Math::Vector3{2.0F, 2.0F, 2.0F};

    RemoteTransform3DObjectDefinition def{};
    def.remoteTransform.remoteTarget = target;
    def.remoteTransform.useGlobalCoordinates = true;
    def.remoteTransform.updatePosition = true;
    def.remoteTransform.updateRotation = true;
    def.remoteTransform.updateScale = false;
    ApplyRemoteTransform3DObjectDefinition(entityManager, source, def);
    ExpectObject3DBaseline(entityManager, source, "RemoteTransform3D");

    entityManager.RemoveComponent<WorldTransformComponent>(source);
    entityManager.AddComponent<WorldTransformComponent>(
        source, WorldTransformComponent{Math::Vector3{7.0F, 8.0F, 9.0F},
                                           Math::Quaternion{},
                                           Math::Vector3{5.0F, 5.0F, 5.0F}});

    EXPECT_EQ(UpdateRemoteTransform3DObjects(entityManager), 1U);
    const TransformComponent& targetLocal =
        entityManager.GetComponent<TransformComponent>(target);
    EXPECT_FLOAT_EQ(targetLocal.localPosition.x, 7.0F);
    EXPECT_FLOAT_EQ(targetLocal.localPosition.y, 8.0F);
    EXPECT_FLOAT_EQ(targetLocal.localPosition.z, 9.0F);
    // Scale channel was disabled so target keeps its authored scale.
    EXPECT_FLOAT_EQ(targetLocal.localScale.x, 2.0F);
}

TEST_F(Object3DDefinitionsTest, VisibleOnScreenNotifier3DTracksCameraFrustumTransitions) {
    const auto setWorldPosition = [this](const Entity entity, const Math::Vector3& pos) {
        if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
            entityManager.RemoveComponent<WorldTransformComponent>(entity);
        }
        entityManager.AddComponent<WorldTransformComponent>(
            entity, WorldTransformComponent{pos});
    };

    const Entity camera = CreateEntity();
    ApplyCamera3DObjectDefinition(entityManager, camera, Camera3DObjectDefinition{});
    const Math::Frustum frustum = Math::Frustum::FromViewProjection(
        Camera3D::BuildViewProjectionMatrix(
            entityManager.GetComponent<CameraComponent>(camera),
            entityManager.GetComponent<WorldTransformComponent>(camera)));

    const Entity notifierEntity = CreateEntity();
    VisibleOnScreenNotifier3DObjectDefinition def{};
    def.notifier.aabbHalfExtents = Math::Vector3{0.5F, 0.5F, 0.5F};
    ApplyVisibleOnScreenNotifier3DObjectDefinition(entityManager, notifierEntity, def);
    ExpectObject3DBaseline(entityManager, notifierEntity, "VisibleOnScreenNotifier3D");

    // Place in front of default camera (looking down -Z).
    setWorldPosition(notifierEntity, Math::Vector3{0.0F, 0.0F, -5.0F});
    EXPECT_EQ(UpdateVisibleOnScreenNotifier3DObjects(entityManager, &frustum), 1U);
    const auto& state1 =
        entityManager.GetComponent<VisibleOnScreenNotifier3DComponent>(notifierEntity);
    EXPECT_TRUE(state1.isOnScreen);
    EXPECT_TRUE(state1.enteredScreenThisFrame);
    EXPECT_FALSE(state1.exitedScreenThisFrame);

    // Second frame still visible: entered pulse clears.
    EXPECT_EQ(UpdateVisibleOnScreenNotifier3DObjects(entityManager, &frustum), 1U);
    EXPECT_TRUE(state1.isOnScreen);
    EXPECT_FALSE(state1.enteredScreenThisFrame);
    EXPECT_FALSE(state1.exitedScreenThisFrame);

    // Move behind the camera (+Z): exits frustum.
    setWorldPosition(notifierEntity, Math::Vector3{0.0F, 0.0F, 20.0F});
    EXPECT_EQ(UpdateVisibleOnScreenNotifier3DObjects(entityManager, &frustum), 0U);
    EXPECT_FALSE(state1.isOnScreen);
    EXPECT_FALSE(state1.enteredScreenThisFrame);
    EXPECT_TRUE(state1.exitedScreenThisFrame);
}

TEST_F(Object3DDefinitionsTest, Path3DAndPathFollow3DSampleAndStepFollowerTransform) {
    const Entity pathEntity = CreateEntity();
    Path3DObjectDefinition pathDef{};
    pathDef.path.points = {Math::Vector3{0.0F, 0.0F, 0.0F},
                           Math::Vector3{0.0F, 0.0F, -10.0F},
                           Math::Vector3{10.0F, 0.0F, -10.0F}};
    pathDef.path.closed = false;
    ApplyPath3DObjectDefinition(entityManager, pathEntity, pathDef);
    ExpectObject3DBaseline(entityManager, pathEntity, "Path3D");
    EXPECT_FLOAT_EQ(
        Path3D::ComputeTotalLength(entityManager.GetComponent<Path3DComponent>(pathEntity)),
        20.0F);

    const Entity followerEntity = CreateEntity();
    PathFollow3DObjectDefinition followDef{};
    followDef.follower.pathEntity = pathEntity;
    followDef.follower.speedMetresPerSecond = 5.0F;
    followDef.follower.hOffset = 1.0F;
    followDef.follower.loop = false;
    ApplyPathFollow3DObjectDefinition(entityManager, followerEntity, followDef);
    ExpectObject3DBaseline(entityManager, followerEntity, "PathFollow3D");

    EXPECT_EQ(UpdatePathFollow3DObjects(entityManager, 1.0F), 1U);
    const auto& follower = entityManager.GetComponent<PathFollow3DComponent>(followerEntity);
    EXPECT_FLOAT_EQ(follower.progressMetres, 5.0F);
    EXPECT_FLOAT_EQ(follower.progressRatio, 0.25F);
    const auto& local = entityManager.GetComponent<TransformComponent>(followerEntity);
    EXPECT_NEAR(local.localPosition.x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(local.localPosition.z, -5.0F, 1.0e-4F);
}

TEST_F(Object3DDefinitionsTest, ShapeCast3DSweepsSphereAgainstColliderAndRecordsHit) {
    const auto setWorldPosition = [this](const Entity entity, const Math::Vector3& pos) {
        if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
            entityManager.RemoveComponent<WorldTransformComponent>(entity);
        }
        entityManager.AddComponent<WorldTransformComponent>(
            entity, WorldTransformComponent{pos});
    };

    const Entity floorEntity = CreateEntity();
    ApplyStatic3DObjectDefinition(entityManager, floorEntity, Static3DObjectDefinition{});
    entityManager.GetComponent<ColliderComponent>(floorEntity).shapeType =
        ColliderShapeType::Sphere;
    entityManager.GetComponent<ColliderComponent>(floorEntity).radius = 1.0F;
    setWorldPosition(floorEntity, Math::Vector3{0.0F, -4.0F, 0.0F});

    const Entity casterEntity = CreateEntity();
    ShapeCast3DObjectDefinition castDef{};
    castDef.shapeCast.shapeKind = ShapeCast3DShapeKind::Sphere;
    castDef.shapeCast.direction = Math::Vector3{0.0F, -1.0F, 0.0F};
    castDef.shapeCast.radius = 0.5F;
    castDef.shapeCast.maxDistance = 10.0F;
    ApplyShapeCast3DObjectDefinition(entityManager, casterEntity, castDef);
    ExpectObject3DBaseline(entityManager, casterEntity, "ShapeCast3D");
    setWorldPosition(casterEntity, Math::Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_EQ(UpdateShapeCast3DObjects(entityManager), 1U);
    const auto& castState = entityManager.GetComponent<ShapeCast3DComponent>(casterEntity);
    EXPECT_TRUE(castState.hit);
    EXPECT_EQ(castState.hitEntity, floorEntity);
    EXPECT_NEAR(castState.hitDistance, 2.5F, 1.0e-4F);
}

namespace {

// A vertical chain: bone 0 at the origin, every child 0.25 m below its parent.
Entity MakeSpringChainSkeleton(EntityManager& entityManager, const std::size_t boneCount) {
    const Entity entity = entityManager.CreateEntity();
    Skeleton3DComponent skeleton;
    skeleton.bones.reserve(boneCount);
    for (std::size_t index = 0U; index < boneCount; ++index) {
        SkeletonBone bone;
        bone.name = "Hair" + std::to_string(index);
        bone.parentIndex = (index == 0U) ? -1 : static_cast<std::int32_t>(index - 1U);
        bone.localPosition = (index == 0U) ? Math::Vector3{0.0F, 0.0F, 0.0F}
                                           : Math::Vector3{0.0F, -0.25F, 0.0F};
        skeleton.bones.push_back(std::move(bone));
    }
    entityManager.AddComponent<Skeleton3DComponent>(entity, std::move(skeleton));
    entityManager.AddComponent<WorldTransformComponent>(entity, WorldTransformComponent{});
    return entity;
}

SpringBoneSimulator3DObjectDefinition MakeChainSimulator(const Entity skeleton,
                                                              const std::size_t chainLength) {
    SpringBoneSimulator3DObjectDefinition definition{};
    definition.simulator.skeleton = skeleton;
    definition.simulator.boneName = "Hair0";
    definition.simulator.chainLength = static_cast<std::uint32_t>(chainLength);
    definition.simulator.tipOffsetLocal = Math::Vector3{0.0F, -0.25F, 0.0F};
    definition.simulator.gravityDirection = Math::Vector3{1.0F, 0.0F, 0.0F};
    definition.simulator.gravityScale = 6.0F;
    definition.simulator.stiffness = 20.0F;
    definition.simulator.damping = 0.3F;
    definition.simulator.limitAngleDegrees = 45.0F;
    return definition;
}

} // namespace

TEST_F(Object3DDefinitionsTest, SpringBoneChainDrivesEverySegmentAndWritesThePose) {
    const Entity skeletonEntity = MakeSpringChainSkeleton(entityManager, 4U);
    const Entity simEntity = CreateEntity();
    ApplySpringBoneSimulator3DObjectDefinition(entityManager, simEntity,
                                                  MakeChainSimulator(skeletonEntity, 4U));
    ExpectObject3DBaseline(entityManager, simEntity, "SpringBoneSimulator3D");
    EXPECT_TRUE(entityManager.HasComponent<BoneModifierComponent>(simEntity));

    // One second at 60 fps; the chain hangs straight down and gravity swings it toward +X.
    for (int frame = 0; frame < 60; ++frame) {
        EXPECT_EQ(UpdateSpringBoneSimulator3DObjects(entityManager, 1.0F / 60.0F), 1U);
    }

    const auto& simulator = entityManager.GetComponent<SpringBoneSimulator3DComponent>(simEntity);
    EXPECT_TRUE(simulator.initialized);
    EXPECT_EQ(simulator.resolvedBoneCount, 4U);
    ASSERT_EQ(simulator.tips.size(), 4U);
    EXPECT_GT(simulator.tips.back().position.x, 0.05F);

    // Every link still spans exactly its authored length: the chain stays connected, root to tip.
    Math::Vector3 pivot = Math::Vector3{0.0F, 0.0F, 0.0F};
    for (const SpringBoneTipState& tip : simulator.tips) {
        EXPECT_NEAR(Math::Length(tip.position - pivot), 0.25F, 1.0e-3F);
        pivot = tip.position;
    }

    // And the result reached the skeleton: the pose now bends, bone by bone.
    const auto& skeleton = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity);
    ASSERT_EQ(skeleton.pose.size(), 4U);
    for (const SkeletonBonePose& bonePose : skeleton.pose) {
        EXPECT_GT(std::fabs(bonePose.rotation.z), 1.0e-3F);
    }
}

TEST_F(Object3DDefinitionsTest, SpringBoneChainIsFrameRateIndependent) {
    const Entity slowSkeleton = MakeSpringChainSkeleton(entityManager, 3U);
    const Entity slowEntity = CreateEntity();
    ApplySpringBoneSimulator3DObjectDefinition(entityManager, slowEntity,
                                                  MakeChainSimulator(slowSkeleton, 3U));
    const Entity fastSkeleton = MakeSpringChainSkeleton(entityManager, 3U);
    const Entity fastEntity = CreateEntity();
    ApplySpringBoneSimulator3DObjectDefinition(entityManager, fastEntity,
                                                  MakeChainSimulator(fastSkeleton, 3U));

    // The same second of simulation, delivered as 30 frames and as 144 frames.
    for (int frame = 0; frame < 30; ++frame) {
        UpdateSpringBoneSimulator3DObjects(entityManager, 1.0F / 30.0F);
    }
    for (int frame = 0; frame < 144; ++frame) {
        UpdateSpringBoneSimulator3DObjects(entityManager, 1.0F / 144.0F);
    }

    const auto& slow = entityManager.GetComponent<SpringBoneSimulator3DComponent>(slowEntity);
    const auto& fast = entityManager.GetComponent<SpringBoneSimulator3DComponent>(fastEntity);
    ASSERT_EQ(slow.tips.size(), 3U);
    ASSERT_EQ(fast.tips.size(), 3U);
    for (std::size_t segment = 0U; segment < 3U; ++segment) {
        EXPECT_NEAR(slow.tips[segment].position.x, fast.tips[segment].position.x, 5.0e-3F);
        EXPECT_NEAR(slow.tips[segment].position.y, fast.tips[segment].position.y, 5.0e-3F);
        EXPECT_NEAR(slow.tips[segment].position.z, fast.tips[segment].position.z, 5.0e-3F);
    }
}

TEST_F(Object3DDefinitionsTest, SpringBoneChainStaysInsideItsAngularLimit) {
    const Entity skeletonEntity = MakeSpringChainSkeleton(entityManager, 2U);
    const Entity simEntity = CreateEntity();
    SpringBoneSimulator3DObjectDefinition definition = MakeChainSimulator(skeletonEntity, 2U);
    definition.simulator.limitAngleDegrees = 15.0F;
    definition.simulator.gravityScale = 40.0F;
    ApplySpringBoneSimulator3DObjectDefinition(entityManager, simEntity, definition);

    for (int frame = 0; frame < 120; ++frame) {
        UpdateSpringBoneSimulator3DObjects(entityManager, 1.0F / 60.0F);
    }

    const auto& simulator = entityManager.GetComponent<SpringBoneSimulator3DComponent>(simEntity);
    ASSERT_EQ(simulator.tips.size(), 2U);
    // Gravity is 40x, so without the cone the chain would lie flat. The limit is PER JOINT, so it
    // is the angle between consecutive links that is bounded - a two-bone chain legitimately
    // reaches 30 degrees of total world bend while no single joint exceeds 15.
    constexpr float kCosineLimit = 0.9658F; // a hair under cos(15 degrees) for float slack
    Math::Vector3 pivot = Math::Vector3{0.0F, 0.0F, 0.0F};
    Math::Vector3 previousDirection = Math::Vector3{0.0F, -1.0F, 0.0F};
    for (const SpringBoneTipState& tip : simulator.tips) {
        const Math::Vector3 direction = Math::Normalize(tip.position - pivot);
        EXPECT_GE(Math::Dot(direction, previousDirection), kCosineLimit);
        previousDirection = direction;
        pivot = tip.position;
    }
}

TEST_F(Object3DDefinitionsTest, SpringBoneChainPushesItsTipOutOfACollisionSphere) {
    const Entity skeletonEntity = MakeSpringChainSkeleton(entityManager, 1U);
    const Entity simEntity = CreateEntity();
    SpringBoneSimulator3DObjectDefinition definition = MakeChainSimulator(skeletonEntity, 1U);
    definition.simulator.tipOffsetLocal = Math::Vector3{0.0F, -1.0F, 0.0F};
    definition.simulator.gravityScale = 0.0F;
    definition.simulator.hitRadius = 0.1F;
    ApplySpringBoneSimulator3DObjectDefinition(entityManager, simEntity, definition);

    const Entity collisionEntity = CreateEntity();
    SpringBoneCollision3DObjectDefinition collision{};
    collision.collision.centerOffset = Math::Vector3{0.0F, -1.0F, 0.0F};
    collision.collision.radius = 0.4F;
    ApplySpringBoneCollision3DObjectDefinition(entityManager, collisionEntity, collision);
    ExpectObject3DBaseline(entityManager, collisionEntity, "SpringBoneCollision3D");

    for (int frame = 0; frame < 30; ++frame) {
        EXPECT_EQ(UpdateSpringBoneSimulator3DObjects(entityManager, 1.0F / 60.0F), 1U);
    }
    const auto& simulator = entityManager.GetComponent<SpringBoneSimulator3DComponent>(simEntity);
    ASSERT_EQ(simulator.tips.size(), 1U);
    const Math::Vector3 deltaFromSphere =
        simulator.tips[0].position - Math::Vector3{0.0F, -1.0F, 0.0F};
    EXPECT_GT(Math::Length(deltaFromSphere), 0.44F);
    // Pushed sideways, but still one link length from the bone it hangs off.
    EXPECT_NEAR(Math::Length(simulator.tips[0].position), 1.0F, 1.0e-3F);
}

TEST_F(Object3DDefinitionsTest, ASpringBoneSimulatorWithoutASkeletonIsNotStepped) {
    const Entity simEntity = CreateEntity();
    SpringBoneSimulator3DObjectDefinition definition{};
    definition.simulator.boneName = "Hair0";
    ApplySpringBoneSimulator3DObjectDefinition(entityManager, simEntity, definition);
    EXPECT_EQ(UpdateSpringBoneSimulator3DObjects(entityManager, 1.0F / 60.0F), 0U);
    const auto& simulator = entityManager.GetComponent<SpringBoneSimulator3DComponent>(simEntity);
    EXPECT_FALSE(simulator.initialized);
    EXPECT_TRUE(simulator.tips.empty());
}

TEST_F(Object3DDefinitionsTest, Joint3DDefinitionsSyncWithPhysicsConstraintSystemAndHingeMotorLimits) {
    const Entity bodyA = CreateEntity();
    const Entity bodyB = CreateEntity();
    ApplyRigid3DObjectDefinition(entityManager, bodyA, Rigid3DObjectDefinition{});
    ApplyRigid3DObjectDefinition(entityManager, bodyB, Rigid3DObjectDefinition{});

    const Entity pinEntity = CreateEntity();
    PinJoint3DObjectDefinition pinDef{};
    pinDef.joint.nodeA = bodyA;
    pinDef.joint.nodeB = bodyB;
    pinDef.joint.restLength = 2.0F;
    ApplyPinJoint3DObjectDefinition(entityManager, pinEntity, pinDef);
    ExpectObject3DBaseline(entityManager, pinEntity, "PinJoint3D");

    const Entity hingeEntity = CreateEntity();
    HingeJoint3DObjectDefinition hingeDef{};
    hingeDef.joint.nodeA = bodyA;
    hingeDef.joint.nodeB = bodyB;
    hingeDef.joint.motorEnabled = true;
    hingeDef.joint.motorTargetVelocity = 2.0F;
    hingeDef.joint.motorMaxImpulse = 10.0F;
    hingeDef.joint.limitsEnabled = true;
    hingeDef.joint.lowerLimit = -0.5F;
    hingeDef.joint.upperLimit = 0.5F;
    ApplyHingeJoint3DObjectDefinition(entityManager, hingeEntity, hingeDef);
    ExpectObject3DBaseline(entityManager, hingeEntity, "HingeJoint3D");

    const Entity sliderEntity = CreateEntity();
    ApplySliderJoint3DObjectDefinition(entityManager, sliderEntity,
                                          SliderJoint3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, sliderEntity, "SliderJoint3D");

    const Entity coneEntity = CreateEntity();
    ApplyConeTwistJoint3DObjectDefinition(entityManager, coneEntity,
                                             ConeTwistJoint3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, coneEntity, "ConeTwistJoint3D");

    const Entity dofEntity = CreateEntity();
    ApplyGeneric6DOFJoint3DObjectDefinition(entityManager, dofEntity,
                                               Generic6DOFJoint3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, dofEntity, "Generic6DOFJoint3D");

    EXPECT_EQ(UpdateJoint3DObjects(entityManager, sceneGraph, 0.1F), 2U);
    const auto& hingeState = entityManager.GetComponent<Joint3DComponent>(hingeEntity);
    EXPECT_TRUE(hingeState.solvedLastStep);
    EXPECT_GT(hingeState.lastAppliedAngularSpeedDeltaRadians, 0.0F);
}

TEST_F(Object3DDefinitionsTest, MultiMeshInstance3DSprite3DAnimatedSprite3DAndLabel3DExtendSurfaceInstanceBase) {
    const Entity multiMeshEntity = CreateEntity();
    MultiMeshInstance3DObjectDefinition mmDef{};
    mmDef.multiMesh.instances.push_back(MultiMeshInstanceTransform3D{
        Math::Vector3{-2.0F, 0.0F, 0.0F}, Math::Quaternion{},
        Math::Vector3{1.0F, 1.0F, 1.0F}});
    mmDef.multiMesh.instances.push_back(MultiMeshInstanceTransform3D{
        Math::Vector3{2.0F, 0.0F, 0.0F}, Math::Quaternion{},
        Math::Vector3{1.0F, 1.0F, 1.0F}});
    ApplyMultiMeshInstance3DObjectDefinition(entityManager, multiMeshEntity, mmDef);
    ExpectObject3DBaseline(entityManager, multiMeshEntity, "MultiMeshInstance3D");
    EXPECT_TRUE(entityManager.HasComponent<SurfaceInstanceComponent>(multiMeshEntity));
    const auto& mmComp =
        entityManager.GetComponent<MultiMeshInstance3DComponent>(multiMeshEntity);
    EXPECT_EQ(MultiMeshInstance3D::ResolveActiveInstanceCount(mmComp), 2U);
    const Math::Aabb bounds = MultiMeshInstance3D::ComputeCombinedWorldBounds(
        mmComp, entityManager.GetComponent<WorldTransformComponent>(multiMeshEntity),
        Math::Vector3{0.5F, 0.5F, 0.5F});
    EXPECT_FLOAT_EQ(bounds.min.x, -2.5F);
    EXPECT_FLOAT_EQ(bounds.max.x, 2.5F);

    const Entity animSpriteEntity = CreateEntity();
    AnimatedSprite3DObjectDefinition animDef{};
    animDef.sprite.hframes = 4U;
    animDef.sprite.vframes = 2U;
    animDef.sprite.framesPerSecond = 10.0F;
    ApplyAnimatedSprite3DObjectDefinition(entityManager, animSpriteEntity, animDef);
    ExpectObject3DBaseline(entityManager, animSpriteEntity, "AnimatedSprite3D");
    EXPECT_TRUE(entityManager.HasComponent<SurfaceInstanceComponent>(animSpriteEntity));
    EXPECT_EQ(UpdateSprite3DObjects(entityManager, 0.25F), 1U);
    const auto& spriteComp =
        entityManager.GetComponent<Sprite3DComponent>(animSpriteEntity);
    EXPECT_EQ(spriteComp.frame, 2U);
    const Math::Rect uvRect = Sprite3D::ResolveFrameUVRect(spriteComp);
    EXPECT_FLOAT_EQ(uvRect.position.x, 0.5F);
    EXPECT_FLOAT_EQ(uvRect.size.x, 0.25F);

    const Entity labelEntity = CreateEntity();
    Label3DObjectDefinition labelDef{};
    labelDef.label.text = "Hello\nWorld";
    ApplyLabel3DObjectDefinition(entityManager, labelEntity, labelDef);
    ExpectObject3DBaseline(entityManager, labelEntity, "Label3D");
    EXPECT_TRUE(entityManager.HasComponent<SurfaceInstanceComponent>(labelEntity));
    const Math::Vector2 labelSize = Label3D::EstimateWorldSizeMetres(
        entityManager.GetComponent<Label3DComponent>(labelEntity));
    EXPECT_GT(labelSize.x, 0.5F);
    EXPECT_GT(labelSize.y, 0.5F);
}

TEST_F(Object3DDefinitionsTest, NavigationObstacle3DNavigationLink3DSmartObject3DAndWorldQuery3DContracts) {
    const auto setWorldPos = [&](const Entity entity, const Math::Vector3& pos) {
        WorldTransformComponent world =
            entityManager.GetComponent<WorldTransformComponent>(entity);
        world.worldPosition = pos;
        world.dirty = false;
        entityManager.RemoveComponent<WorldTransformComponent>(entity);
        entityManager.AddComponent<WorldTransformComponent>(entity, world);
    };

    const Entity obsEntity = CreateEntity();
    NavigationObstacle3DObjectDefinition obsDef{};
    obsDef.obstacle.radius = 2.0F;
    ApplyNavigationObstacle3DObjectDefinition(entityManager, obsEntity, obsDef);
    ExpectObject3DBaseline(entityManager, obsEntity, "NavigationObstacle3D");
    setWorldPos(obsEntity, Math::Vector3{0.0F, 0.0F, 0.0F});

    const Entity seekerEntity = CreateEntity();
    NavSeeker3DObjectDefinition seekerDef{};
    seekerDef.seeker.avoidanceEnabled = true;
    seekerDef.seeker.avoidanceRadius = 1.0F;
    seekerDef.seeker.maxSpeed = 5.0F;
    ApplyNavSeeker3DObjectDefinition(entityManager, seekerEntity, seekerDef);
    setWorldPos(seekerEntity, Math::Vector3{1.5F, 0.0F, 0.0F});

    EXPECT_EQ(ApplyNavigationObstacles3DToAgents(entityManager), 1U);
    EXPECT_GT(
        entityManager.GetComponent<NavSeeker3DComponent>(seekerEntity).desiredVelocity.x,
        0.0F);

    const Entity linkEntity = CreateEntity();
    NavigationLink3DObjectDefinition linkDef{};
    linkDef.link.startOffset = Math::Vector3{0.0F, 0.0F, 0.0F};
    linkDef.link.endOffset = Math::Vector3{0.0F, 0.0F, -4.0F};
    linkDef.link.travelCostMultiplier = 1.5F;
    ApplyNavigationLink3DObjectDefinition(entityManager, linkEntity, linkDef);
    ExpectObject3DBaseline(entityManager, linkEntity, "NavigationLink3D");
    Math::Vector3 wStart{};
    Math::Vector3 wEnd{};
    const float cost = ResolveNavigationLink3DWorldEndpointsAndCost(
        entityManager.GetComponent<NavigationLink3DComponent>(linkEntity),
        entityManager.GetComponent<WorldTransformComponent>(linkEntity), wStart, wEnd);
    EXPECT_FLOAT_EQ(cost, 6.0F);

    const Entity smartEntity = CreateEntity();
    SmartObject3DObjectDefinition smartDef{};
    smartDef.smartObject.interactionTag = "Cover";
    smartDef.smartObject.interactionOffset = Math::Vector3{0.0F, 0.0F, 0.0F};
    ApplySmartObject3DObjectDefinition(entityManager, smartEntity, smartDef);
    ExpectObject3DBaseline(entityManager, smartEntity, "SmartObject3D");
    setWorldPos(smartEntity, Math::Vector3{5.0F, 0.0F, 0.0F});

    const Entity queryEntity = CreateEntity();
    WorldQuery3DObjectDefinition queryDef{};
    queryDef.query.requiredTag = "Cover";
    queryDef.query.optimalDistanceMetres = 5.0F;
    queryDef.query.searchRadiusMetres = 20.0F;
    ApplyWorldQuery3DObjectDefinition(entityManager, queryEntity, queryDef);
    ExpectObject3DBaseline(entityManager, queryEntity, "WorldQuery3D");
    setWorldPos(queryEntity, Math::Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_EQ(UpdateSmartObjectAndWorldQuery3D(entityManager, 0.016F), 1U);
    const auto& qState = entityManager.GetComponent<WorldQuery3DComponent>(queryEntity);
    EXPECT_EQ(qState.bestEntity, smartEntity);
    EXPECT_NEAR(qState.bestScore, 1.0F, 1.0e-4F);
}

TEST_F(Object3DDefinitionsTest, Vehicle3DPhysicalBone3DSoftBody3DAndCollisionPolygon3DContracts) {
    const Entity vehicleEntity = CreateEntity();
    VehicleBody3DObjectDefinition vehDef{};
    vehDef.vehicle.mass = 1000.0F;
    vehDef.vehicle.engineForce = 2000.0F;
    ApplyVehicleBody3DObjectDefinition(entityManager, vehicleEntity, vehDef);
    ExpectObject3DBaseline(entityManager, vehicleEntity, "VehicleBody3D");

    const Entity wheelEntity = CreateEntity();
    VehicleWheel3DObjectDefinition wheelDef{};
    wheelDef.wheel.vehicleBodyEntity = vehicleEntity;
    wheelDef.wheel.currentSuspensionLength = 0.2F;
    ApplyVehicleWheel3DObjectDefinition(entityManager, wheelEntity, wheelDef);
    ExpectObject3DBaseline(entityManager, wheelEntity, "VehicleWheel3D");

    EXPECT_EQ(UpdateVehicle3DObjects(entityManager, 0.5F), 1U);
    EXPECT_GT(
        entityManager.GetComponent<VehicleBody3DComponent>(vehicleEntity)
            .currentSpeedMetresPerSecond,
        0.5F);
    EXPECT_GT(
        entityManager.GetComponent<VehicleWheel3DComponent>(wheelEntity).suspensionForce,
        0.0F);

    const Entity skelEntity = CreateEntity();
    ApplySkeleton3DObjectDefinition(entityManager, skelEntity, Skeleton3DObjectDefinition{});
    SkeletonBone rootBone{};
    rootBone.name = "Root";
    rootBone.localPosition = Math::Vector3{1.0F, 2.0F, 3.0F};
    entityManager.GetComponent<Skeleton3DComponent>(skelEntity).bones.push_back(rootBone);

    const Entity pbEntity = CreateEntity();
    PhysicalBone3DObjectDefinition pbDef{};
    pbDef.physicalBone.skeletonEntity = skelEntity;
    pbDef.physicalBone.boneIndex = 0;
    pbDef.physicalBone.simulatePhysics = false;
    ApplyPhysicalBone3DObjectDefinition(entityManager, pbEntity, pbDef);
    ExpectObject3DBaseline(entityManager, pbEntity, "PhysicalBone3D");
    EXPECT_EQ(UpdatePhysicalBone3DObjects(entityManager, Math::Vector3{0.0F, -9.81F, 0.0F},
                                             0.1F),
              1U);
    EXPECT_FLOAT_EQ(
        entityManager.GetComponent<TransformComponent>(pbEntity).localPosition.y, 2.0F);

    const Entity softEntity = CreateEntity();
    SoftBody3DObjectDefinition sbDef{};
    sbDef.softBody.points = {SoftBody3DPoint{Math::Vector3{0.0F, 0.0F, 0.0F}, {}, true},
                             SoftBody3DPoint{Math::Vector3{0.0F, -1.0F, 0.0F}, {}, false}};
    sbDef.softBody.restSegmentLength = 0.5F;
    ApplySoftBody3DObjectDefinition(entityManager, softEntity, sbDef);
    ExpectObject3DBaseline(entityManager, softEntity, "SoftBody3D");
    EXPECT_EQ(
        UpdateSoftBody3DObjects(entityManager, Math::Vector3{0.0F, -9.81F, 0.0F}, 0.016F),
        1U);

    const Entity colPolyEntity = CreateEntity();
    CollisionPolygon3DObjectDefinition colPolyDef{};
    colPolyDef.polygon.vertices2D = {Math::Vector2{-1.0F, -1.0F}, Math::Vector2{1.0F, -1.0F},
                                     Math::Vector2{1.0F, 1.0F}, Math::Vector2{-1.0F, 1.0F}};
    colPolyDef.polygon.depthMetres = 2.0F;
    ApplyCollisionPolygon3DObjectDefinition(entityManager, colPolyEntity, colPolyDef);
    ExpectObject3DBaseline(entityManager, colPolyEntity, "CollisionPolygon3D");
    const auto& colPoly =
        entityManager.GetComponent<CollisionPolygon3DComponent>(colPolyEntity);
    EXPECT_TRUE(CollisionPolygon3D::ContainsLocalPoint(colPoly,
                                                             Math::Vector3{0.0F, 0.0F, 0.5F}));
    EXPECT_FALSE(CollisionPolygon3D::ContainsLocalPoint(colPoly,
                                                              Math::Vector3{0.0F, 0.0F, 2.5F}));
}

TEST_F(Object3DDefinitionsTest, CSGShapesGridMap3DAndStateTree3DContracts) {
    const Entity csgBoxEntity = CreateEntity();
    ApplyCSGBox3DObjectDefinition(entityManager, csgBoxEntity, CSGBox3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, csgBoxEntity, "CSGBox3D");
    const auto& boxCsg = entityManager.GetComponent<CSGShape3DComponent>(csgBoxEntity);
    EXPECT_LT(
        CSGShape3D::EvaluateLocalSignedDistance(boxCsg, Math::Vector3{0.0F, 0.0F, 0.0F}),
        0.0F);
    EXPECT_GT(
        CSGShape3D::EvaluateLocalSignedDistance(boxCsg, Math::Vector3{2.0F, 0.0F, 0.0F}),
        0.0F);
    EXPECT_FLOAT_EQ(
        CSGShape3D::CombineSignedDistance(CSGOperation::Subtraction, -0.5F, -0.2F), 0.2F);

    const Entity gridEntity = CreateEntity();
    ApplyGridMap3DObjectDefinition(entityManager, gridEntity, GridMap3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, gridEntity, "GridMap");
    auto& gridComp = entityManager.GetComponent<GridMap3DComponent>(gridEntity);
    GridMap3D::SetCellItem(gridComp, Math::Vector3i{1, 2, 3}, 7);
    EXPECT_EQ(GridMap3D::GetCellItem(gridComp, Math::Vector3i{1, 2, 3}), 7);
    const Math::Vector3 cellCenter =
        GridMap3D::MapToLocalPosition(gridComp, Math::Vector3i{1, 2, 3});
    EXPECT_EQ(GridMap3D::LocalPositionToMap(gridComp, cellCenter),
              (Math::Vector3i{1, 2, 3}));

    const Entity treeEntity = CreateEntity();
    StateTree3DObjectDefinition treeDef{};
    treeDef.stateTree.initialStateId = "Idle";
    treeDef.stateTree.states = {"Idle", "Alert"};
    treeDef.stateTree.transitions = {
        StateTreeTransition3D{"Idle", "Alert", "alertLevel", 0.5F, 0.1F}};
    ApplyStateTree3DObjectDefinition(entityManager, treeEntity, treeDef);
    ExpectObject3DBaseline(entityManager, treeEntity, "StateTree3D");
    auto& treeComp = entityManager.GetComponent<StateTree3DComponent>(treeEntity);
    StateTree3D::SetParameter(treeComp, "alertLevel", 0.8F);
    EXPECT_EQ(UpdateStateTree3DObjects(entityManager, 0.2F), 1U);
    EXPECT_EQ(treeComp.activeStateId, "Alert");
}

TEST_F(Object3DDefinitionsTest,
       AppliesBatch5Particles3DGIProbes3DAndXRObjects3D) {
    const Entity gpuParticles = CreateEntity();
    ApplyGPUParticles3DObjectDefinition(entityManager, gpuParticles,
                                           GPUParticles3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, gpuParticles, "GPUParticles3D");
    EXPECT_EQ(entityManager.GetComponent<ParticleBackendTagComponent>(gpuParticles).backend,
              ParticleSimulationBackend3D::GPU);

    const Entity cpuParticles = CreateEntity();
    ApplyCPUParticles3DObjectDefinition(entityManager, cpuParticles,
                                           CPUParticles3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, cpuParticles, "CPUParticles3D");
    EXPECT_EQ(entityManager.GetComponent<ParticleBackendTagComponent>(cpuParticles).backend,
              ParticleSimulationBackend3D::CPU);

    const Entity voxelEntity = CreateEntity();
    ApplyVoxelGI3DObjectDefinition(entityManager, voxelEntity, VoxelGI3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, voxelEntity, "VoxelGI");
    const Entity probeEntity = CreateEntity();
    ApplyLightmapProbe3DObjectDefinition(entityManager, probeEntity,
                                            LightmapProbe3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, probeEntity, "LightmapProbe");
    const Math::Color irradiance =
        EvaluateIndirectGI3DIrradianceAtPoint(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});
    EXPECT_GT(irradiance.r, 0.0F);

    const Entity xrOrigin = CreateEntity();
    ApplyXROrigin3DObjectDefinition(entityManager, xrOrigin, XROrigin3DObjectDefinition{});
    ExpectObject3DBaseline(entityManager, xrOrigin, "XROrigin3D");
    const Entity xrCam = CreateEntity();
    XRCamera3DObjectDefinition camDef{};
    camDef.tracker.trackedLocalPosition = Math::Vector3{0.0F, 1.6F, 0.0F};
    ApplyXRCamera3DObjectDefinition(entityManager, xrCam, camDef);
    ExpectObject3DBaseline(entityManager, xrCam, "XRCamera3D");
    EXPECT_GE(UpdateXRObjects3D(entityManager), 1U);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<TransformComponent>(xrCam).localPosition.y,
                    1.6F);
}

} // namespace UVE::Scene::Tests
