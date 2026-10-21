// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include <gtest/gtest.h>

#include "uve/scene/objects/scene_object_uve.h"

namespace UVE::Scene::Objects::Tests {
namespace {

TEST(SceneObjectRegistryUVETest, BuiltInDescriptorsUVE_AreStableUniqueAndRuntimeBound) {
    const std::span<const SceneObjectDescriptorUVE> descriptors = GetSceneObjectDescriptorsUVE();
    ASSERT_EQ(descriptors.size(), 52U);

    std::unordered_set<std::string_view> ids;
    for (const SceneObjectDescriptorUVE& descriptor : descriptors) {
        EXPECT_TRUE(ids.insert(descriptor.typeId).second);
        EXPECT_FALSE(descriptor.typeId.empty());
        EXPECT_FALSE(descriptor.displayName.empty());
        EXPECT_FALSE(descriptor.category.empty());
        EXPECT_FALSE(descriptor.runtimeOwner.empty());
        EXPECT_LE(descriptor.authoredContracts.size(), 8U);
        // Structure the document creates itself; never offered in an object list.
        if (descriptor.kind == SceneObjectKindUVE::SceneRoot || descriptor.kind == SceneObjectKindUVE::Viewport3D) {
            EXPECT_FALSE(descriptor.libraryCreatable);
        } else {
            EXPECT_TRUE(descriptor.libraryCreatable);
        }
        EXPECT_EQ(FindSceneObjectDescriptorUVE(descriptor.kind), &descriptor);
        EXPECT_EQ(FindSceneObjectDescriptorUVE(descriptor.typeId), &descriptor);
        EXPECT_EQ(GetSceneObjectTypeIdUVE(descriptor.kind), descriptor.typeId);
    }
}

// Every one of the 51 registered SceneObjectKindUVE values has a real backing type, reachable
// directly from scene_object_uve.h's one aggregate include - no compatibility-alias facade layer
// exists anymore (it was removed once confirmed nothing in the codebase used the alias names;
// every real consumer already reaches for the component/type name directly). This test just
// confirms every real type is a genuine, complete class - i.e. actually defined, not merely
// forward-declared - matching what scene_object_registry_uve.cpp's own runtimeOwner field claims.
TEST(SceneObjectRegistryUVETest, RealObjectTypesUVE_AreReachableFromTheAggregateHeader) {
    static_assert(std::is_class_v<EntityUVE>);                                 // Empty
    static_assert(std::is_class_v<AreaComponentUVE>);                          // Area3D
    static_assert(std::is_class_v<RayCast3DComponentUVE>);                 // RayCast3D
    static_assert(std::is_class_v<ColliderComponentUVE>);                      // Static3D, Collider3D
    static_assert(std::is_class_v<Kinematic3DComponentUVE>);          // Kinematic3D
    static_assert(std::is_class_v<NavMeshVolume3DComponentUVE>);        // NavMeshVolume3D
    static_assert(std::is_class_v<NavSeeker3DComponentUVE>);         // NavSeeker3D
    static_assert(std::is_class_v<Skeleton3DComponentUVE>);                // Skeleton3D
    static_assert(std::is_class_v<BoneAttachment3DComponentUVE>);          // BoneAttachment3D
    static_assert(std::is_class_v<TwoBoneIK3DComponentUVE>);              // TwoBoneIK3D
    static_assert(std::is_class_v<FullBodyIK3DComponentUVE>);            // FullBodyIK3D
    static_assert(std::is_class_v<SpringArm3DComponentUVE>);               // SpringArm3D
    static_assert(std::is_class_v<Marker3DComponentUVE>);                  // Marker3D
    static_assert(std::is_class_v<Hitbox3DComponentUVE>);                  // Hitbox3D
    static_assert(std::is_class_v<Hurtbox3DComponentUVE>);                 // Hurtbox3D
    static_assert(std::is_class_v<Projectile3DComponentUVE>);              // Projectile3D
    static_assert(std::is_class_v<InteractionArea3DComponentUVE>);         // InteractionArea3D
    static_assert(std::is_class_v<WorldEnvironment3DComponentUVE>);        // WorldEnvironment3D
    static_assert(std::is_class_v<ReflectionProbe3DComponentUVE>);         // ReflectionProbe3D
    static_assert(std::is_class_v<Decal3DComponentUVE>);                   // Decal3D
    static_assert(std::is_class_v<LodGroup3DComponentUVE>);                // LODGroup3D
    static_assert(std::is_class_v<Occluder3DComponentUVE>);                // Occluder3D
    static_assert(std::is_class_v<VisibilityRegion3DComponentUVE>);        // VisibilityRegion3D
    static_assert(std::is_class_v<SpawnPoint3DComponentUVE>);              // SpawnPoint3D
    static_assert(std::is_class_v<PlayerComponentUVE>);                    // Player3D
    static_assert(std::is_class_v<LevelStreamer3DComponentUVE>);           // LevelStreamer3D
    static_assert(std::is_class_v<WorldPartition3DComponentUVE>);          // WorldPartition3D
    // Was Core::AnimationGraphUVE, which is not what backs this kind: the registry's own runtimeOwner
    // for AnimationGraph is "Scene/AnimationGraphComponentUVE". The old assert passed only because the
    // aggregate header happened to include an unrelated animation module that nothing in the engine
    // calls, so the wrong type was reachable and the right one was not.
    static_assert(std::is_class_v<AnimationGraphComponentUVE>);              // AnimationGraph
    static_assert(std::is_class_v<AnimationSequencerComponentUVE>);               // AnimationSequencer
    static_assert(std::is_class_v<Physics::CharacterControllerInputUVE>);      // Character3D
    static_assert(std::is_class_v<CameraComponentUVE>);                       // Camera3D
    static_assert(std::is_class_v<MeshComponentUVE>);                         // MeshInstance3D
    static_assert(std::is_class_v<PrimitiveMeshComponentUVE>);                // BoxMesh3D, SphereMesh3D, PlaneMesh3D
    static_assert(std::is_class_v<LightComponentUVE>);                        // Light3D
    static_assert(std::is_class_v<Rigid3DComponentUVE>);                    // Rigid3D
    static_assert(std::is_class_v<AudioSourceComponentUVE>);                  // AudioSource3D
    static_assert(std::is_class_v<ParticleEmitterComponentUVE>);              // ParticleEmitter3D
    static_assert(std::is_class_v<ScriptComponentUVE>);                       // Script
    static_assert(std::is_class_v<Sprite2DComponentUVE>);                  // Sprite2D
    static_assert(std::is_class_v<Sprite2DObjectDefinitionUVE>);          // Sprite2D
    static_assert(std::is_class_v<Camera2DComponentUVE>);                  // Camera2D
    static_assert(std::is_class_v<Camera2DObjectDefinitionUVE>);          // Camera2D
    SUCCEED();
}

TEST(SceneObjectRegistryUVETest, Sprite2DDescriptorUVE_OpensThe2DCategory) {
    const SceneObjectDescriptorUVE* const byKind =
        FindSceneObjectDescriptorUVE(SceneObjectKindUVE::Sprite2D);
    ASSERT_NE(byKind, nullptr);
    EXPECT_EQ(byKind->typeId, "sprite_2d");
    EXPECT_EQ(byKind->displayName, "Sprite2D");
    EXPECT_EQ(byKind->category, "2D");
    EXPECT_TRUE(byKind->libraryCreatable);
    ASSERT_EQ(byKind->authoredContracts.size(), 1U);
    EXPECT_EQ(byKind->authoredContracts[0], "Sprite2DComponentUVE");
    EXPECT_EQ(FindSceneObjectDescriptorUVE(std::string_view{"sprite_2d"}), byKind);
    EXPECT_EQ(GetSceneObjectTypeIdUVE(SceneObjectKindUVE::Sprite2D), "sprite_2d");
}

TEST(SceneObjectRegistryUVETest, Camera2DDescriptorUVE_JoinsTheRenderingCategory) {
    const SceneObjectDescriptorUVE* const byKind =
        FindSceneObjectDescriptorUVE(SceneObjectKindUVE::Camera2D);
    ASSERT_NE(byKind, nullptr);
    EXPECT_EQ(byKind->typeId, "camera_2d");
    EXPECT_EQ(byKind->displayName, "Camera2D");
    EXPECT_EQ(byKind->category, "Rendering");
    EXPECT_TRUE(byKind->libraryCreatable);
    ASSERT_EQ(byKind->authoredContracts.size(), 1U);
    EXPECT_EQ(byKind->authoredContracts[0], "Camera2DComponentUVE");
    EXPECT_EQ(FindSceneObjectDescriptorUVE(std::string_view{"camera_2d"}), byKind);
    EXPECT_EQ(GetSceneObjectTypeIdUVE(SceneObjectKindUVE::Camera2D), "camera_2d");
}

TEST(SceneObjectRegistryUVETest, UnknownLookupUVE_ReturnsEmptyOrNull) {
    EXPECT_EQ(FindSceneObjectDescriptorUVE(std::string_view{"missing_object"}), nullptr);
    EXPECT_EQ(GetSceneObjectTypeIdUVE(static_cast<SceneObjectKindUVE>(255U)), std::string_view{});
}

} // namespace
TEST(SceneObjectRegistryUVETest, PlacementSeparatesTheLevelFromEntityParts) {
    EXPECT_EQ(GetSceneObjectPlacementUVE(SceneObjectKindUVE::Light3D), SceneObjectPlacementUVE::World);
    EXPECT_EQ(GetSceneObjectPlacementUVE(SceneObjectKindUVE::BoxMesh3D), SceneObjectPlacementUVE::World);
    EXPECT_EQ(GetSceneObjectPlacementUVE(SceneObjectKindUVE::WorldEnvironment3D), SceneObjectPlacementUVE::World);
    EXPECT_EQ(GetSceneObjectPlacementUVE(SceneObjectKindUVE::Character3D), SceneObjectPlacementUVE::Entity);
    EXPECT_EQ(GetSceneObjectPlacementUVE(SceneObjectKindUVE::Player3D), SceneObjectPlacementUVE::Entity);
    EXPECT_EQ(GetSceneObjectPlacementUVE(SceneObjectKindUVE::AnimationGraph), SceneObjectPlacementUVE::Entity);
    EXPECT_EQ(GetSceneObjectPlacementUVE(SceneObjectKindUVE::Hitbox3D), SceneObjectPlacementUVE::Entity);
}

} // namespace UVE::Scene::Objects::Tests
