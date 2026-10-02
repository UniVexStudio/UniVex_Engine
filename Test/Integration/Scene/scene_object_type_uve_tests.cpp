// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/objects/scene_object_type_uve.h"

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
#include "uve/objects/3d/all_objects_3d_uve.h"
#include "uve/scene/objects/scene_root_uve.h"
#include "uve/scene/scene_serializer_uve.h"

namespace UVE::Scene::Tests {
namespace {

using Kind = Objects::SceneObjectKindUVE;

class SceneObjectTypeUVETest : public ::testing::Test {
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

TEST_F(SceneObjectTypeUVETest, InferenceReadsEveryKindThatHasComponentsOfItsOwn) {
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeUVE()), Kind::Object3D);
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(SceneRootComponentUVE{})), Kind::SceneRoot);
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(CameraComponentUVE{})), Kind::Camera3D);
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(MeshComponentUVE{})), Kind::MeshInstance3D);
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(Marker3DComponentUVE{})), Kind::Marker3D);
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(AreaComponentUVE{})), Kind::Area3D);

    // A primitive carries a collider too, and still reads as its shape.
    for (const auto& [primitive, kind] : {std::pair{PrimitiveMeshKindUVE::Cube, Kind::BoxMesh3D},
                                         std::pair{PrimitiveMeshKindUVE::UVSphere, Kind::SphereMesh3D},
                                         std::pair{PrimitiveMeshKindUVE::Plane, Kind::PlaneMesh3D}}) {
        PrimitiveMeshComponentUVE mesh;
        mesh.kind = primitive;
        EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(mesh, ColliderComponentUVE{})), kind);
    }

    // Bodies are read from what they combine; an Kinematic3D's own component outranks both.
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(ColliderComponentUVE{}, RigidBodyComponentUVE{})),
              Kind::Character3D);
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(RigidBodyComponentUVE{})), Kind::Rigid3D);
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(ColliderComponentUVE{})), Kind::Collider3D);
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(ColliderComponentUVE{}, RigidBodyComponentUVE{},
                                                               AnimatableBody3DComponentUVE{})),
              Kind::Kinematic3D);

    // A script names the object only when nothing else does.
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(ScriptComponentUVE{})), Kind::Script);
    EXPECT_EQ(InferSceneObjectKindUVE(entityManager, MakeWithUVE(MeshComponentUVE{}, ScriptComponentUVE{})),
              Kind::MeshInstance3D);
}

TEST_F(SceneObjectTypeUVETest, AStoredTypeWinsOverWhatTheComponentsSuggest) {
    // A Static3D is built exactly like a Collider3D; only the stored type tells them apart.
    const EntityUVE body = MakeWithUVE(ColliderComponentUVE{});
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, body), Kind::Collider3D);
    SetSceneObjectKindUVE(entityManager, body, Kind::Static3D);
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, body), Kind::Static3D);
    SetSceneObjectKindUVE(entityManager, body, Kind::Collider3D);
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, body), Kind::Collider3D);

    // A stored value no descriptor knows is ignored rather than trusted.
    const EntityUVE corrupt = MakeWithUVE(CameraComponentUVE{});
    SetSceneObjectKindUVE(entityManager, corrupt, static_cast<Kind>(250));
    EXPECT_FALSE(IsSceneObjectTypeComponentValidUVE(entityManager.GetComponentUVE<SceneObjectTypeComponentUVE>(corrupt)));
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, corrupt), Kind::Camera3D);

    const EntityUVE dead = MakeUVE();
    entityManager.DestroyEntityUVE(dead);
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, dead), Kind::Object3D);
}

TEST_F(SceneObjectTypeUVETest, TheTypeSurvivesASaveByItsStableId) {
    const EntityUVE body = MakeWithUVE(ColliderComponentUVE{});
    SetSceneObjectKindUVE(entityManager, body, Kind::Static3D);
    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {body}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::string text(reinterpret_cast<const char*>(snapshot->bytes.data()), snapshot->bytes.size());
    EXPECT_NE(text.find("\"static_3d\""), std::string::npos);
    EXPECT_EQ(text.find("\"static_body_3d\""), std::string::npos);

    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, restored.front()), Kind::Static3D);
}

TEST_F(SceneObjectTypeUVETest, UnknownAndLegacyIdsLoadWithoutFailingTheScene) {
    const auto restoreWithType = [this](const std::string_view type) {
        const std::string payload =
            std::string(R"({"entities":[{"localId":0,"components":{"NameComponentUVE":{"name":"Object"},)") +
            R"("SceneObjectTypeComponentUVE":{"type":")" + std::string(type) + R"("}}}]})";
        return serializer.RestoreUVE(entityManager, SnapshotFromPayloadUVE(payload));
    };

    // A type from a newer build: the object loads, untyped, and reads from its components.
    const std::vector<EntityUVE> future = restoreWithType("hover_car_3d");
    ASSERT_EQ(future.size(), 1U);
    EXPECT_FALSE(entityManager.HasComponentUVE<SceneObjectTypeComponentUVE>(future.front()));
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, future.front()), Kind::Object3D);

    // The id Object3D had before it was renamed still names it.
    const std::vector<EntityUVE> legacy = restoreWithType("empty");
    ASSERT_EQ(legacy.size(), 1U);
    ASSERT_TRUE(entityManager.HasComponentUVE<SceneObjectTypeComponentUVE>(legacy.front()));
    EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, legacy.front()), Kind::Object3D);

    // Scenes saved before the rename keep naming the four 3D body kinds and the two animation kinds
    // by their old ids: the alias has to land on the same kind, not on an untyped object.
    const std::pair<std::string_view, Kind> renamed[] = {
        {"static_body_3d", Kind::Static3D},
        {"rigid_body_3d", Kind::Rigid3D},
        {"character_body_3d", Kind::Character3D},
        {"animatable_body_3d", Kind::Kinematic3D},
        {"animation_player", Kind::AnimationSequencer},
        {"animation_tree", Kind::AnimationGraph},
    };
    for (const auto& [oldId, expected] : renamed) {
        const std::vector<EntityUVE> aliased = restoreWithType(oldId);
        ASSERT_EQ(aliased.size(), 1U) << oldId;
        ASSERT_TRUE(entityManager.HasComponentUVE<SceneObjectTypeComponentUVE>(aliased.front())) << oldId;
        EXPECT_EQ(ResolveSceneObjectKindUVE(entityManager, aliased.front()), expected) << oldId;
    }
}

} // namespace
} // namespace UVE::Scene::Tests
