// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "scene_object_type.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve_file_envelope.h"
#include "area_component.h"
#include "camera_component.h"
#include "collider_component.h"
#include "mesh_component.h"
#include "name_component.h"
#include "primitive_mesh_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "all_objects_3d.h"
#include "scene_root.h"
#include "scene_serializer.h"

namespace UVE::Scene::Tests {
namespace {

using Kind = Objects::SceneObjectKind;

class SceneObjectTypeTest : public ::testing::Test {
protected:
    [[nodiscard]] Entity Make() {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<NameComponent>(entity, NameComponent{"Object"});
        return entity;
    }

    template <typename... Components>
    [[nodiscard]] Entity MakeWith(Components... components) {
        const Entity entity = Make();
        (entityManager.AddComponent<Components>(entity, components), ...);
        return entity;
    }

    [[nodiscard]] SceneSnapshot SnapshotFromPayload(const std::string& payload) const {
        const auto* const bytes = reinterpret_cast<const std::byte*>(payload.data());
        return SceneSnapshot{Asset::EncodeUveFileEnvelope(SceneAssetType::Scene,
                                                                std::vector<std::byte>{bytes, bytes + payload.size()}),
                                SceneAssetType::Scene};
    }

    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneSerializer serializer;
};

TEST_F(SceneObjectTypeTest, InferenceReadsEveryKindThatHasComponentsOfItsOwn) {
    EXPECT_EQ(InferSceneObjectKind(entityManager, Make()), Kind::Object3D);
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(SceneRootComponent{})), Kind::SceneRoot);
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(CameraComponent{})), Kind::Camera3D);
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(MeshComponent{})), Kind::MeshInstance3D);
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(Marker3DComponent{})), Kind::Marker3D);
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(AreaComponent{})), Kind::Area3D);

    // A primitive carries a collider too, and still reads as its shape.
    for (const auto& [primitive, kind] : {std::pair{PrimitiveMeshKind::Cube, Kind::BoxMesh3D},
                                         std::pair{PrimitiveMeshKind::UVSphere, Kind::SphereMesh3D},
                                         std::pair{PrimitiveMeshKind::Plane, Kind::PlaneMesh3D}}) {
        PrimitiveMeshComponent mesh;
        mesh.kind = primitive;
        EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(mesh, ColliderComponent{})), kind);
    }

    // Bodies are read from what they combine; an Kinematic3D's own component outranks both. A
    // collider on a body is a shape on a Rigid3D - what makes a character a character is its
    // CharacterControllerComponent, which is one of the own-components checked above.
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(ColliderComponent{}, Rigid3DComponent{})),
              Kind::Rigid3D);
    EXPECT_EQ(InferSceneObjectKind(entityManager,
                                      MakeWith(ColliderComponent{}, Rigid3DComponent{},
                                                  CharacterControllerComponent{})),
              Kind::Character3D);
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(PlayerComponent{}, CharacterControllerComponent{})),
              Kind::Player3D);
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(Rigid3DComponent{})), Kind::Rigid3D);
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(ColliderComponent{})), Kind::Collider3D);
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(ColliderComponent{}, Rigid3DComponent{},
                                                               Kinematic3DComponent{})),
              Kind::Kinematic3D);

    // A script names the object only when nothing else does.
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(ScriptComponent{})), Kind::Script);
    EXPECT_EQ(InferSceneObjectKind(entityManager, MakeWith(MeshComponent{}, ScriptComponent{})),
              Kind::MeshInstance3D);
}

TEST_F(SceneObjectTypeTest, AStoredTypeWinsOverWhatTheComponentsSuggest) {
    // A Static3D is built exactly like a Collider3D; only the stored type tells them apart.
    const Entity body = MakeWith(ColliderComponent{});
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, body), Kind::Collider3D);
    SetSceneObjectKind(entityManager, body, Kind::Static3D);
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, body), Kind::Static3D);
    SetSceneObjectKind(entityManager, body, Kind::Collider3D);
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, body), Kind::Collider3D);

    // A stored value no descriptor knows is ignored rather than trusted.
    const Entity corrupt = MakeWith(CameraComponent{});
    SetSceneObjectKind(entityManager, corrupt, static_cast<Kind>(250));
    EXPECT_FALSE(IsSceneObjectTypeComponentValid(entityManager.GetComponent<SceneObjectTypeComponent>(corrupt)));
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, corrupt), Kind::Camera3D);

    const Entity dead = Make();
    entityManager.DestroyEntity(dead);
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, dead), Kind::Object3D);
}

TEST_F(SceneObjectTypeTest, TheTypeSurvivesASaveByItsStableId) {
    const Entity body = MakeWith(ColliderComponent{});
    SetSceneObjectKind(entityManager, body, Kind::Static3D);
    const std::optional<SceneSnapshot> snapshot =
        serializer.Capture(entityManager, {body}, SceneAssetType::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::string text(reinterpret_cast<const char*>(snapshot->bytes.data()), snapshot->bytes.size());
    EXPECT_NE(text.find("\"static_3d\""), std::string::npos);
    EXPECT_EQ(text.find("\"static_body_3d\""), std::string::npos);

    const std::vector<Entity> restored = serializer.Restore(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, restored.front()), Kind::Static3D);
}

TEST_F(SceneObjectTypeTest, UnknownAndLegacyIdsLoadWithoutFailingTheScene) {
    const auto restoreWithType = [this](const std::string_view type) {
        const std::string payload =
            std::string(R"({"entities":[{"localId":0,"components":{"NameComponent":{"name":"Object"},)") +
            R"("SceneObjectTypeComponent":{"type":")" + std::string(type) + R"("}}}]})";
        return serializer.Restore(entityManager, SnapshotFromPayload(payload));
    };

    // A type from a newer build: the object loads, untyped, and reads from its components.
    const std::vector<Entity> future = restoreWithType("hover_car_3d");
    ASSERT_EQ(future.size(), 1U);
    EXPECT_FALSE(entityManager.HasComponent<SceneObjectTypeComponent>(future.front()));
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, future.front()), Kind::Object3D);

    // The id Object3D had before it was renamed still names it.
    const std::vector<Entity> legacy = restoreWithType("empty");
    ASSERT_EQ(legacy.size(), 1U);
    ASSERT_TRUE(entityManager.HasComponent<SceneObjectTypeComponent>(legacy.front()));
    EXPECT_EQ(ResolveSceneObjectKind(entityManager, legacy.front()), Kind::Object3D);

    // Scenes saved before a rename keep naming a kind by its old id: the alias has to land on the
    // same kind, not on an untyped object.
    const std::pair<std::string_view, Kind> renamed[] = {
        {"static_body_3d", Kind::Static3D},
        {"rigid_body_3d", Kind::Rigid3D},
        {"character_body_3d", Kind::Character3D},
        {"animatable_body_3d", Kind::Kinematic3D},
        {"animation_player", Kind::AnimationSequencer},
        {"animation_tree", Kind::AnimationGraph},
        {"navigation_region_3d", Kind::NavMeshVolume3D},
        {"navigation_agent_3d", Kind::NavSeeker3D},
        {"viewport", Kind::Viewport3D},
    };
    for (const auto& [oldId, expected] : renamed) {
        const std::vector<Entity> aliased = restoreWithType(oldId);
        ASSERT_EQ(aliased.size(), 1U) << oldId;
        ASSERT_TRUE(entityManager.HasComponent<SceneObjectTypeComponent>(aliased.front())) << oldId;
        EXPECT_EQ(ResolveSceneObjectKind(entityManager, aliased.front()), expected) << oldId;
    }
}

} // namespace
} // namespace UVE::Scene::Tests
