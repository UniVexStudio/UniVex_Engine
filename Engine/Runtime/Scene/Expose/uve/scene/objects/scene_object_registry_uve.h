// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace UVE::Scene::Objects {

enum class SceneObjectKindUVE : std::uint8_t {
    // The transform-only base object. Formerly spelled `Empty`; value 0 is unchanged so anything
    // that stored the raw enumerator still decodes.
    Object3D = 0,
    Area3D,
    RayCast3D,
    Static3D,
    Kinematic3D,
    NavMeshVolume3D,
    NavSeeker3D,
    Skeleton3D,
    BoneAttachment3D,
    SpringArm3D,
    Marker3D,
    Hitbox3D,
    Hurtbox3D,
    Projectile3D,
    InteractionArea3D,
    WorldEnvironment3D,
    ReflectionProbe3D,
    Decal3D,
    LODGroup3D,
    Occluder3D,
    VisibilityRegion3D,
    SpawnPoint3D,
    LevelStreamer3D,
    WorldPartition3D,
    AnimationGraph,
    AnimationSequencer,
    Character3D,
    Camera3D,
    MeshInstance3D,
    BoxMesh3D,
    SphereMesh3D,
    PlaneMesh3D,
    Light3D,
    Collider3D,
    Rigid3D,
    AudioSource3D,
    ParticleEmitter3D,
    Script,
    Canvas,
    UIText,
    UIImage,
    UIButton,
    SceneRoot,
    /// Appended rather than placed beside Decal3D so no existing kind's value moves.
    FogVolume3D,
    /// Groups objects in the Scene panel; no transform, no effect on the running scene.
    Folder,
    /// The sun: LightEmitter3D's directional child.
    DirectionalLight3D,
    /// The level at the top of the Outliner; its folders hold the level's objects.
    Viewport,
    /// A limb solved back from a target rather than forward from its joints.
    TwoBoneIK3D,
    /// Character3D marked as the possessed player. Appended so no existing kind's value moves.
    Player3D,
};

struct SceneObjectDescriptorUVE final {
    SceneObjectKindUVE kind = SceneObjectKindUVE::Object3D;
    std::string_view typeId;
    std::string_view displayName;
    std::string_view category;
    std::string_view runtimeOwner;
    std::span<const std::string_view> authoredContracts;
    bool libraryCreatable = false;
};

inline constexpr std::size_t kMaximumSceneObjectDescriptorsUVE = 64U;

/// Where an object kind belongs. The world holds the level itself - meshes, lights, cameras,
/// environment, volumes - and is what the Scene panel's "+" offers first. An Entity object is part of
/// something that lives in the world: a character's body, its animation, its hitboxes. Those are
/// built inside an Entity asset and brought into the level whole.
enum class SceneObjectPlacementUVE : std::uint8_t {
    World = 0,
    Entity,
};

[[nodiscard]] SceneObjectPlacementUVE GetSceneObjectPlacementUVE(SceneObjectKindUVE kind) noexcept;

[[nodiscard]] std::span<const SceneObjectDescriptorUVE> GetSceneObjectDescriptorsUVE() noexcept;
[[nodiscard]] const SceneObjectDescriptorUVE* FindSceneObjectDescriptorUVE(
    SceneObjectKindUVE kind) noexcept;
[[nodiscard]] const SceneObjectDescriptorUVE* FindSceneObjectDescriptorUVE(
    std::string_view typeId) noexcept;
[[nodiscard]] std::string_view GetSceneObjectTypeIdUVE(SceneObjectKindUVE kind) noexcept;

} // namespace UVE::Scene::Objects
