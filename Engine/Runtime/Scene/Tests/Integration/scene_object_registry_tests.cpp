// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include <gtest/gtest.h>

#include "scene_object.h"

namespace UVE::Scene::Objects::Tests {
namespace {

TEST(SceneObjectRegistryTest, BuiltInDescriptors_AreStableUniqueAndRuntimeBound) {
    const std::span<const SceneObjectDescriptor> descriptors = GetSceneObjectDescriptors();
    // 53 kinds: the 51 an object list offers plus the document's own two structural rows.
    ASSERT_EQ(descriptors.size(), 53U);

    std::unordered_set<std::string_view> ids;
    for (const SceneObjectDescriptor& descriptor : descriptors) {
        EXPECT_TRUE(ids.insert(descriptor.typeId).second);
        EXPECT_FALSE(descriptor.typeId.empty());
        EXPECT_FALSE(descriptor.displayName.empty());
        EXPECT_FALSE(descriptor.category.empty());
        EXPECT_FALSE(descriptor.runtimeOwner.empty());
        EXPECT_LE(descriptor.authoredContracts.size(), 8U);
        // Structure the document creates itself; never offered in an object list.
        if (descriptor.kind == SceneObjectKind::SceneRoot || descriptor.kind == SceneObjectKind::Viewport3D) {
            EXPECT_FALSE(descriptor.libraryCreatable);
        } else {
            EXPECT_TRUE(descriptor.libraryCreatable);
        }
        EXPECT_EQ(FindSceneObjectDescriptor(descriptor.kind), &descriptor);
        EXPECT_EQ(FindSceneObjectDescriptor(descriptor.typeId), &descriptor);
        EXPECT_EQ(GetSceneObjectTypeId(descriptor.kind), descriptor.typeId);
    }
}

// Every registered SceneObjectKind value has a real backing type, reachable
// directly from scene_object_uve.h's one aggregate include - no compatibility-alias facade layer
// exists anymore (it was removed once confirmed nothing in the codebase used the alias names;
// every real consumer already reaches for the component/type name directly). This test just
// confirms every real type is a genuine, complete class - i.e. actually defined, not merely
// forward-declared - matching what scene_object_registry_uve.cpp's own runtimeOwner field claims.
TEST(SceneObjectRegistryTest, RealObjectTypes_AreReachableFromTheAggregateHeader) {
    static_assert(std::is_class_v<Entity>);                                 // Empty
    static_assert(std::is_class_v<AreaComponent>);                          // Area3D
    static_assert(std::is_class_v<RayCast3DComponent>);                 // RayCast3D
    static_assert(std::is_class_v<ColliderComponent>);                      // Static3D, Collider3D
    static_assert(std::is_class_v<Kinematic3DComponent>);          // Kinematic3D
    static_assert(std::is_class_v<NavMeshVolume3DComponent>);        // NavMeshVolume3D
    static_assert(std::is_class_v<NavSeeker3DComponent>);         // NavSeeker3D
    static_assert(std::is_class_v<Skeleton3DComponent>);                // Skeleton3D
    static_assert(std::is_class_v<BoneAttachment3DComponent>);          // BoneAttachment3D
    static_assert(std::is_class_v<TwoBoneIK3DComponent>);              // TwoBoneIK3D
    static_assert(std::is_class_v<FullBodyIK3DComponent>);            // FullBodyIK3D
    static_assert(std::is_class_v<Ragdoll3DComponent>);               // Ragdoll3D
    static_assert(std::is_class_v<SpringArm3DComponent>);               // SpringArm3D
    static_assert(std::is_class_v<Marker3DComponent>);                  // Marker3D
    static_assert(std::is_class_v<Hitbox3DComponent>);                  // Hitbox3D
    static_assert(std::is_class_v<Hurtbox3DComponent>);                 // Hurtbox3D
    static_assert(std::is_class_v<Projectile3DComponent>);              // Projectile3D
    static_assert(std::is_class_v<InteractionArea3DComponent>);         // InteractionArea3D
    static_assert(std::is_class_v<WorldEnvironment3DComponent>);        // WorldEnvironment3D
    static_assert(std::is_class_v<ReflectionProbe3DComponent>);         // ReflectionProbe3D
    static_assert(std::is_class_v<Decal3DComponent>);                   // Decal3D
    static_assert(std::is_class_v<LodGroup3DComponent>);                // LODGroup3D
    static_assert(std::is_class_v<Occluder3DComponent>);                // Occluder3D
    static_assert(std::is_class_v<VisibilityRegion3DComponent>);        // VisibilityRegion3D
    static_assert(std::is_class_v<SpawnPoint3DComponent>);              // SpawnPoint3D
    static_assert(std::is_class_v<PlayerComponent>);                    // Player3D
    static_assert(std::is_class_v<LevelStreamer3DComponent>);           // LevelStreamer3D
    static_assert(std::is_class_v<WorldPartition3DComponent>);          // WorldPartition3D
    // Was Core::AnimationGraph, which is not what backs this kind: the registry's own runtimeOwner
    // for AnimationGraph is "Scene/AnimationGraphComponent". The old assert passed only because the
    // aggregate header happened to include an unrelated animation module that nothing in the engine
    // calls, so the wrong type was reachable and the right one was not.
    static_assert(std::is_class_v<AnimationGraphComponent>);              // AnimationGraph
    static_assert(std::is_class_v<AnimationSequencerComponent>);               // AnimationSequencer
    static_assert(std::is_class_v<Physics::CharacterControllerInput>);      // Character3D
    static_assert(std::is_class_v<CameraComponent>);                       // Camera3D
    static_assert(std::is_class_v<MeshComponent>);                         // MeshInstance3D
    static_assert(std::is_class_v<PrimitiveMeshComponent>);                // BoxMesh3D, SphereMesh3D, PlaneMesh3D
    static_assert(std::is_class_v<LightComponent>);                        // Light3D
    static_assert(std::is_class_v<Rigid3DComponent>);                    // Rigid3D
    static_assert(std::is_class_v<AudioSourceComponent>);                  // AudioSource3D
    static_assert(std::is_class_v<ParticleEmitterComponent>);              // ParticleEmitter3D
    static_assert(std::is_class_v<ScriptComponent>);                       // Script
    SUCCEED();
}

TEST(SceneObjectRegistryTest, UnknownLookup_ReturnsEmptyOrNull) {
    EXPECT_EQ(FindSceneObjectDescriptor(std::string_view{"missing_object"}), nullptr);
    EXPECT_EQ(GetSceneObjectTypeId(static_cast<SceneObjectKind>(255U)), std::string_view{});
}

} // namespace
TEST(SceneObjectRegistryTest, PlacementSeparatesTheLevelFromEntityParts) {
    EXPECT_EQ(GetSceneObjectPlacement(SceneObjectKind::Light3D), SceneObjectPlacement::World);
    EXPECT_EQ(GetSceneObjectPlacement(SceneObjectKind::BoxMesh3D), SceneObjectPlacement::World);
    EXPECT_EQ(GetSceneObjectPlacement(SceneObjectKind::WorldEnvironment3D), SceneObjectPlacement::World);
    EXPECT_EQ(GetSceneObjectPlacement(SceneObjectKind::Character3D), SceneObjectPlacement::Entity);
    EXPECT_EQ(GetSceneObjectPlacement(SceneObjectKind::Player3D), SceneObjectPlacement::Entity);
    EXPECT_EQ(GetSceneObjectPlacement(SceneObjectKind::AnimationGraph), SceneObjectPlacement::Entity);
    EXPECT_EQ(GetSceneObjectPlacement(SceneObjectKind::Hitbox3D), SceneObjectPlacement::Entity);
    EXPECT_EQ(GetSceneObjectPlacement(SceneObjectKind::Ragdoll3D), SceneObjectPlacement::Entity);
}

} // namespace UVE::Scene::Objects::Tests
