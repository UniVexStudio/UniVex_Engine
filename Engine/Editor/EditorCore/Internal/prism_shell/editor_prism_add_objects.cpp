// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_add_objects.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace UVE::Editor {
namespace {

using Kind = Scene::Objects::SceneObjectKindUVE;

/// The groups, in menu order, and the kinds in each (in order). A kind not listed here is not offered.
struct GroupUVE final {
    std::string_view title;
    std::array<Kind, 12> kinds;
    std::size_t count;
};

constexpr std::array<GroupUVE, 13> kGroupsUVE{{
    {"Basic", {Kind::Folder, Kind::Object3D, Kind::Marker3D, Kind::Script}, 4},
    {"Meshes", {Kind::MeshInstance3D, Kind::BoxMesh3D, Kind::SphereMesh3D, Kind::PlaneMesh3D, Kind::LODGroup3D}, 5},
    {"Lights", {Kind::DirectionalLight3D, Kind::Light3D}, 2},
    {"Environment", {Kind::WorldEnvironment3D, Kind::Sky, Kind::ReflectionProbe3D, Kind::FogVolume3D, Kind::Decal3D}, 5},
    {"Camera", {Kind::Camera3D, Kind::SpringArm3D}, 2},
    {"Physics", {Kind::Static3D, Kind::Rigid3D, Kind::Kinematic3D, Kind::Collider3D, Kind::Area3D, Kind::RayCast3D}, 6},
    {"Animation",
     {Kind::Skeleton3D, Kind::AnimationSequencer, Kind::AnimationGraph, Kind::MontagePlayer, Kind::BoneAttachment3D,
      Kind::TwoBoneIK3D, Kind::FullBodyIK3D},
     7},
    {"Gameplay", {Kind::SpawnPoint3D, Kind::InteractionArea3D, Kind::Hitbox3D, Kind::Hurtbox3D, Kind::Projectile3D}, 5},
    {"Navigation", {Kind::NavMeshVolume3D, Kind::NavSeeker3D}, 2},
    {"Audio & VFX", {Kind::AudioSource3D, Kind::ParticleEmitter3D}, 2},
    {"World", {Kind::LevelStreamer3D, Kind::WorldPartition3D, Kind::Occluder3D, Kind::VisibilityRegion3D}, 4},
    {"UI", {Kind::Canvas, Kind::UIText, Kind::UIImage, Kind::UIButton}, 4},
    {"2D", {Kind::Sprite2D, Kind::Camera2D}, 2},
}};

/// The level's own: one per level, or a volume of the world. Never part of an entity.
[[nodiscard]] bool IsLevelOnlyUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::WorldEnvironment3D:
        case Kind::Sky:
        case Kind::DirectionalLight3D:
        case Kind::ReflectionProbe3D:
        case Kind::FogVolume3D:
        case Kind::LevelStreamer3D:
        case Kind::WorldPartition3D:
        case Kind::Occluder3D:
        case Kind::VisibilityRegion3D:
        case Kind::NavMeshVolume3D:
        case Kind::SpawnPoint3D:
        case Kind::LODGroup3D:
        case Kind::Canvas:
        case Kind::UIText:
        case Kind::UIImage:
        case Kind::UIButton:
        case Kind::Sprite2D:
        case Kind::Camera2D:
        case Kind::Folder:
            return true;
        default:
            return false;
    }
}

} // namespace

bool IsObjectOfferedUVE(const Kind kind, const AddObjectContextUVE context) noexcept {
    if (context == AddObjectContextUVE::Entity) {
        return !IsLevelOnlyUVE(kind);
    }
    // A level takes everything of the world; a character's parts are built inside an Entity.
    return Scene::Objects::GetSceneObjectPlacementUVE(kind) == Scene::Objects::SceneObjectPlacementUVE::World;
}

prism::Icon ObjectKindGlyphUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Folder: return prism::Icon::Folder;
        case Kind::Object3D: return prism::Icon::Object3D;
        case Kind::Marker3D: return prism::Icon::Marker;
        case Kind::Script: return prism::Icon::Script;
        case Kind::MeshInstance3D: return prism::Icon::MeshInstance;
        case Kind::BoxMesh3D: return prism::Icon::Cube;
        case Kind::SphereMesh3D: return prism::Icon::Sphere;
        case Kind::PlaneMesh3D: return prism::Icon::Plane;
        case Kind::DirectionalLight3D: return prism::Icon::DirectionalLight;
        case Kind::Light3D: return prism::Icon::Light;
        case Kind::WorldEnvironment3D: return prism::Icon::Environment;
        case Kind::Camera3D: return prism::Icon::Camera;
        case Kind::Static3D: return prism::Icon::Static3D;
        case Kind::Rigid3D: return prism::Icon::Rigid3D;
        case Kind::Collider3D: return prism::Icon::Collider;
        case Kind::Skeleton3D: return prism::Icon::Skeleton;
        case Kind::AnimationSequencer: return prism::Icon::AnimSequencer;
        case Kind::AnimationGraph: return prism::Icon::AnimGraph;
        case Kind::MontagePlayer: return prism::Icon::MontagePlayer;
        case Kind::BoneAttachment3D: return prism::Icon::BoneAttachment;
        case Kind::TwoBoneIK3D: return prism::Icon::TwoBoneIK;
        default: return prism::Icon::None;
    }
}

std::vector<prism::MenuItem> BuildAddObjectMenuUVE(const AddObjectContextUVE context, EditorPrismIconsUVE& icons,
                                                   const std::function<void(Kind)>& create,
                                                   const std::vector<Kind>& favorites) {
    const auto itemFor = [&](const Kind kind) {
        const Scene::Objects::SceneObjectDescriptorUVE* const descriptor = Scene::Objects::FindSceneObjectDescriptorUVE(kind);
        std::string label = descriptor != nullptr ? std::string(descriptor->displayName) : std::string("Object");
        if (kind == Kind::WorldEnvironment3D) {
            label = "WorldEnvironment3D";
        }
        prism::MenuItem item = prism::MenuItem::Item(label, [create, kind] { create(kind); }, "", ObjectKindGlyphUVE(kind));
        if (const EditorPrismIconsUVE::ImageUVE image = icons.ObjectUVE(kind, 16); image) {
            item.withImage(image.pixels, image.size);
        }
        return item;
    };
    std::vector<prism::MenuItem> items;
    for (const Kind kind : favorites) {
        if (IsObjectOfferedUVE(kind, context)) {
            items.push_back(itemFor(kind));
        }
    }
    if (!items.empty()) {
        items.push_back(prism::MenuItem::Sep());
    }
    for (const GroupUVE& group : kGroupsUVE) {
        std::vector<prism::MenuItem> sub;
        for (std::size_t i = 0U; i < group.count; ++i) {
            const Kind kind = group.kinds[i];
            const Scene::Objects::SceneObjectDescriptorUVE* const descriptor = Scene::Objects::FindSceneObjectDescriptorUVE(kind);
            if (descriptor == nullptr || !descriptor->libraryCreatable || !IsObjectOfferedUVE(kind, context)) {
                continue;
            }
            sub.push_back(itemFor(kind));
        }
        if (!sub.empty()) {
            items.push_back(prism::MenuItem::Sub(std::string(group.title), std::move(sub)));
        }
    }
    return items;
}

} // namespace UVE::Editor
