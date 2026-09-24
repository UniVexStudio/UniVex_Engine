// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/nodes/scene_node_type_uve.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/scene/nodes/scene_root_uve.h"
#include "uve/scene/scene_serializer_uve.h"

namespace UVE::Scene::Tests {
namespace {

using Kind = Nodes::SceneNodeKindUVE;

class SceneNodeTypeUVETest : public ::testing::Test {
protected:
    [[nodiscard]] EntityUVE MakeUVE() {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<NameComponentUVE>(entity, NameComponentUVE{"Node"});
        return entity;
    }

    template <typename... Components>
    [[nodiscard]] EntityUVE MakeWithUVE(Components... components) {
        const EntityUVE entity = MakeUVE();
        (entityManager.AddComponentUVE<Components>(entity, components), ...);
        return entity;
    }

    [[nodiscard]] SceneSnapshotUVE SnapshotFromPayloadUVE(const std::string& payload) const {
        const auto* const bytes = reinterpret_cast<const std::byte*>(payload.data());
        return SceneSnapshotUVE{Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                                                std::vector<std::byte>{bytes, bytes + payload.size()}),
                                SceneAssetTypeUVE::Scene};
    }

    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneSerializerUVE serializer;
};

TEST_F(SceneNodeTypeUVETest, InferenceReadsEveryKindThatHasComponentsOfItsOwn) {
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeUVE()), Kind::Node3D);
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(SceneRootComponentUVE{})), Kind::SceneRoot);
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(CameraComponentUVE{})), Kind::Camera3D);
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(MeshComponentUVE{})), Kind::MeshInstance3D);
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(Marker3DNodeComponentUVE{})), Kind::Marker3D);
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(AreaComponentUVE{})), Kind::Area3D);

    // A primitive carries a collider too, and still reads as its shape.
    for (const auto& [primitive, kind] : {std::pair{PrimitiveMeshKindUVE::Cube, Kind::BoxMesh3D},
                                         std::pair{PrimitiveMeshKindUVE::UVSphere, Kind::SphereMesh3D},
                                         std::pair{PrimitiveMeshKindUVE::Plane, Kind::PlaneMesh3D}}) {
        PrimitiveMeshComponentUVE mesh;
        mesh.kind = primitive;
        EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(mesh, ColliderComponentUVE{})), kind);
    }

    // Bodies are read from what they combine; an AnimatableBody3D's own component outranks both.
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(ColliderComponentUVE{}, RigidBodyComponentUVE{})),
              Kind::CharacterBody3D);
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(RigidBodyComponentUVE{})), Kind::RigidBody3D);
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(ColliderComponentUVE{})), Kind::Collider3D);
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(ColliderComponentUVE{}, RigidBodyComponentUVE{},
                                                               AnimatableBody3DNodeComponentUVE{})),
              Kind::AnimatableBody3D);

    // A script names the node only when nothing else does.
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(ScriptComponentUVE{})), Kind::Script);
    EXPECT_EQ(InferSceneNodeKindUVE(entityManager, MakeWithUVE(MeshComponentUVE{}, ScriptComponentUVE{})),
              Kind::MeshInstance3D);
}

TEST_F(SceneNodeTypeUVETest, AStoredTypeWinsOverWhatTheComponentsSuggest) {
    // A StaticBody3D is built exactly like a Collider3D; only the stored type tells them apart.
    const EntityUVE body = MakeWithUVE(ColliderComponentUVE{});
    EXPECT_EQ(ResolveSceneNodeKindUVE(entityManager, body), Kind::Collider3D);
    SetSceneNodeKindUVE(entityManager, body, Kind::StaticBody3D);
    EXPECT_EQ(ResolveSceneNodeKindUVE(entityManager, body), Kind::StaticBody3D);
    SetSceneNodeKindUVE(entityManager, body, Kind::Collider3D);
    EXPECT_EQ(ResolveSceneNodeKindUVE(entityManager, body), Kind::Collider3D);

    // A stored value no descriptor knows is ignored rather than trusted.
    const EntityUVE corrupt = MakeWithUVE(CameraComponentUVE{});
    SetSceneNodeKindUVE(entityManager, corrupt, static_cast<Kind>(250));
    EXPECT_FALSE(IsSceneNodeTypeComponentValidUVE(entityManager.GetComponentUVE<SceneNodeTypeComponentUVE>(corrupt)));
    EXPECT_EQ(ResolveSceneNodeKindUVE(entityManager, corrupt), Kind::Camera3D);

    const EntityUVE dead = MakeUVE();
    entityManager.DestroyEntityUVE(dead);
    EXPECT_EQ(ResolveSceneNodeKindUVE(entityManager, dead), Kind::Node3D);
}

TEST_F(SceneNodeTypeUVETest, TheTypeSurvivesASaveByItsStableId) {
    const EntityUVE body = MakeWithUVE(ColliderComponentUVE{});
    SetSceneNodeKindUVE(entityManager, body, Kind::StaticBody3D);
    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {body}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::string text(reinterpret_cast<const char*>(snapshot->bytes.data()), snapshot->bytes.size());
    EXPECT_NE(text.find("\"static_body_3d\""), std::string::npos);

    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    EXPECT_EQ(ResolveSceneNodeKindUVE(entityManager, restored.front()), Kind::StaticBody3D);
}

TEST_F(SceneNodeTypeUVETest, UnknownAndLegacyIdsLoadWithoutFailingTheScene) {
    const auto restoreWithType = [this](const std::string_view type) {
        const std::string payload =
            std::string(R"({"entities":[{"localId":0,"components":{"NameComponentUVE":{"name":"Node"},)") +
            R"("SceneNodeTypeComponentUVE":{"type":")" + std::string(type) + R"("}}}]})";
        return serializer.RestoreUVE(entityManager, SnapshotFromPayloadUVE(payload));
    };

    // A type from a newer build: the node loads, untyped, and reads from its components.
    const std::vector<EntityUVE> future = restoreWithType("hover_car_3d");
    ASSERT_EQ(future.size(), 1U);
    EXPECT_FALSE(entityManager.HasComponentUVE<SceneNodeTypeComponentUVE>(future.front()));
    EXPECT_EQ(ResolveSceneNodeKindUVE(entityManager, future.front()), Kind::Node3D);

    // The id Node3D had before it was renamed still names it.
    const std::vector<EntityUVE> legacy = restoreWithType("empty");
    ASSERT_EQ(legacy.size(), 1U);
    ASSERT_TRUE(entityManager.HasComponentUVE<SceneNodeTypeComponentUVE>(legacy.front()));
    EXPECT_EQ(ResolveSceneNodeKindUVE(entityManager, legacy.front()), Kind::Node3D);
}

} // namespace
} // namespace UVE::Scene::Tests
