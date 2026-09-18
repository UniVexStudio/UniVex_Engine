// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <type_traits>

#include <gtest/gtest.h>

#include "uve/component/camera_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"

namespace UVE::Scene::Tests {
namespace {

// Every one of the 17 component-backed node kinds' definition is reachable from the aggregate
// header, mirroring the registry test's guarantee for the 21 data-carrying node components:
// no node kind's home file can be silently dropped without breaking this compile.
static_assert(std::is_class_v<EmptyNodeDefinitionUVE>);            // Empty
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
    EXPECT_TRUE(IsEmptyNodeDefinitionValidUVE(EmptyNodeDefinitionUVE{}));
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
}

// The seven names the editor's legacy EditorEntityKindUVE path surfaces are locked at compile
// time: the editor now sources every default name from these definitions, so any drift here
// would silently rename what legacy creation produces.
static_assert(EmptyNodeDefinitionUVE::defaultName == "Empty");
static_assert(Camera3DNodeDefinitionUVE::defaultName == "Camera");
static_assert(Light3DNodeDefinitionUVE::defaultName == "Directional Light");
static_assert(Collider3DNodeDefinitionUVE::defaultName == "Collision Box");
static_assert(BoxMesh3DNodeDefinitionUVE::defaultName == "Cube");
static_assert(SphereMesh3DNodeDefinitionUVE::defaultName == "UV Sphere");
static_assert(PlaneMesh3DNodeDefinitionUVE::defaultName == "Plane");

TEST_F(Node3DDefinitionsUVETest, DefaultNamesAreAuthoredPerKindNotGeneric) {
    // The six kinds that previously lived behind legacy EditorEntityKindUVE values keep their
    // exact historical names; the kinds the editor used to name "Empty" now carry their own.
    EXPECT_EQ(EmptyNodeDefinitionUVE::defaultName, "Empty");
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
}

TEST_F(Node3DDefinitionsUVETest, ApplyAttachesEachKindsExactComponentRecipe) {
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyEmptyNodeDefinitionUVE(entityManager, entity, EmptyNodeDefinitionUVE{});
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

TEST_F(Node3DDefinitionsUVETest, LightRecipeDefaultsToDirectionalSunLight) {
    const EntityUVE entity = CreateEntityUVE();
    ApplyLight3DNodeDefinitionUVE(entityManager, entity, Light3DNodeDefinitionUVE{});
    ASSERT_TRUE(entityManager.HasComponentUVE<LightComponentUVE>(entity));
    EXPECT_EQ(entityManager.GetComponentUVE<LightComponentUVE>(entity).type, LightTypeUVE::Directional);
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
} // namespace UVE::Scene::Tests
