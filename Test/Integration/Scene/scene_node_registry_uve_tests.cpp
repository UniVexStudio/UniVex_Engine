// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include <gtest/gtest.h>

#include "uve/scene/nodes/scene_node_uve.h"

namespace UVE::Scene::Nodes::Tests {
namespace {

TEST(SceneNodeRegistryUVETest, BuiltInDescriptorsUVE_AreStableUniqueAndRuntimeBound) {
    const std::span<const SceneNodeDescriptorUVE> descriptors = GetSceneNodeDescriptorsUVE();
    ASSERT_EQ(descriptors.size(), 38U);

    std::unordered_set<std::string_view> ids;
    for (const SceneNodeDescriptorUVE& descriptor : descriptors) {
        EXPECT_TRUE(ids.insert(descriptor.typeId).second);
        EXPECT_FALSE(descriptor.typeId.empty());
        EXPECT_FALSE(descriptor.displayName.empty());
        EXPECT_FALSE(descriptor.category.empty());
        EXPECT_FALSE(descriptor.runtimeOwner.empty());
        EXPECT_LE(descriptor.authoredContracts.size(), 8U);
        if (descriptor.kind == SceneNodeKindUVE::AnimationTree) {
            EXPECT_FALSE(descriptor.libraryCreatable);
        } else {
            EXPECT_TRUE(descriptor.libraryCreatable);
        }
        EXPECT_EQ(FindSceneNodeDescriptorUVE(descriptor.kind), &descriptor);
        EXPECT_EQ(FindSceneNodeDescriptorUVE(descriptor.typeId), &descriptor);
        EXPECT_EQ(GetSceneNodeTypeIdUVE(descriptor.kind), descriptor.typeId);
    }
}

// Every one of the 38 registered SceneNodeKindUVE values has a real backing type, reachable
// directly from scene_node_uve.h's one aggregate include - no compatibility-alias facade layer
// exists anymore (it was removed once confirmed nothing in the codebase used the alias names;
// every real consumer already reaches for the component/type name directly). This test just
// confirms every real type is a genuine, complete class - i.e. actually defined, not merely
// forward-declared - matching what scene_node_registry_uve.cpp's own runtimeOwner field claims.
TEST(SceneNodeRegistryUVETest, RealNodeTypesUVE_AreReachableFromTheAggregateHeader) {
    static_assert(std::is_class_v<EntityUVE>);                                 // Empty
    static_assert(std::is_class_v<AreaComponentUVE>);                          // Area3D
    static_assert(std::is_class_v<RayCast3DNodeComponentUVE>);                 // RayCast3D
    static_assert(std::is_class_v<ColliderComponentUVE>);                      // StaticBody3D, Collider3D
    static_assert(std::is_class_v<AnimatableBody3DNodeComponentUVE>);          // AnimatableBody3D
    static_assert(std::is_class_v<NavigationRegion3DNodeComponentUVE>);        // NavigationRegion3D
    static_assert(std::is_class_v<NavigationAgent3DNodeComponentUVE>);         // NavigationAgent3D
    static_assert(std::is_class_v<Skeleton3DNodeComponentUVE>);                // Skeleton3D
    static_assert(std::is_class_v<BoneAttachment3DNodeComponentUVE>);          // BoneAttachment3D
    static_assert(std::is_class_v<SpringArm3DNodeComponentUVE>);               // SpringArm3D
    static_assert(std::is_class_v<Marker3DNodeComponentUVE>);                  // Marker3D
    static_assert(std::is_class_v<Hitbox3DNodeComponentUVE>);                  // Hitbox3D
    static_assert(std::is_class_v<Hurtbox3DNodeComponentUVE>);                 // Hurtbox3D
    static_assert(std::is_class_v<Projectile3DNodeComponentUVE>);              // Projectile3D
    static_assert(std::is_class_v<InteractionArea3DNodeComponentUVE>);         // InteractionArea3D
    static_assert(std::is_class_v<WorldEnvironment3DNodeComponentUVE>);        // WorldEnvironment3D
    static_assert(std::is_class_v<ReflectionProbe3DNodeComponentUVE>);         // ReflectionProbe3D
    static_assert(std::is_class_v<Decal3DNodeComponentUVE>);                   // Decal3D
    static_assert(std::is_class_v<LodGroup3DNodeComponentUVE>);                // LODGroup3D
    static_assert(std::is_class_v<Occluder3DNodeComponentUVE>);                // Occluder3D
    static_assert(std::is_class_v<VisibilityRegion3DNodeComponentUVE>);        // VisibilityRegion3D
    static_assert(std::is_class_v<SpawnPoint3DNodeComponentUVE>);              // SpawnPoint3D
    static_assert(std::is_class_v<LevelStreamer3DNodeComponentUVE>);           // LevelStreamer3D
    static_assert(std::is_class_v<WorldPartition3DNodeComponentUVE>);          // WorldPartition3D
    static_assert(std::is_class_v<Core::AnimationTreeUVE>);                    // AnimationTree
    static_assert(std::is_class_v<AnimationPlayerComponentUVE>);               // AnimationPlayer
    static_assert(std::is_class_v<Physics::CharacterControllerInputUVE>);      // CharacterBody3D
    static_assert(std::is_class_v<CameraComponentUVE>);                       // Camera3D
    static_assert(std::is_class_v<MeshComponentUVE>);                         // MeshInstance3D
    static_assert(std::is_class_v<PrimitiveMeshComponentUVE>);                // BoxMesh3D, SphereMesh3D, PlaneMesh3D
    static_assert(std::is_class_v<LightComponentUVE>);                        // Light3D
    static_assert(std::is_class_v<RigidBodyComponentUVE>);                    // RigidBody3D
    static_assert(std::is_class_v<AudioSourceComponentUVE>);                  // AudioSource3D
    static_assert(std::is_class_v<ParticleEmitterComponentUVE>);              // ParticleEmitter3D
    static_assert(std::is_class_v<ScriptComponentUVE>);                       // Script
    SUCCEED();
}

TEST(SceneNodeRegistryUVETest, UnknownLookupUVE_ReturnsEmptyOrNull) {
    EXPECT_EQ(FindSceneNodeDescriptorUVE(std::string_view{"missing_node"}), nullptr);
    EXPECT_EQ(GetSceneNodeTypeIdUVE(static_cast<SceneNodeKindUVE>(255U)), std::string_view{});
}

} // namespace
} // namespace UVE::Scene::Nodes::Tests
