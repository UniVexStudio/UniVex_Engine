// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "scene_object_registry.h"

namespace UVE::Scene::Objects {
namespace {

constexpr std::array<std::string_view, 0U> kNoContracts{};
constexpr std::array<std::string_view, 1U> kAnimationGraphContracts{"AnimationGraphComponent"};
constexpr std::array<std::string_view, 1U> kAnimationSequencerContracts{"AnimationSequencerComponent"};
constexpr std::array<std::string_view, 1U> kMontageSequencerContracts{"MontageSequencerComponent"};
constexpr std::array<std::string_view, 2U> kCharacterContracts{
    "TransformComponent", "ColliderComponent"};
constexpr std::array<std::string_view, 3U> kPlayerContracts{
    "TransformComponent", "ColliderComponent", "PlayerComponent"};
constexpr std::array<std::string_view, 1U> kCameraContracts{"CameraComponent"};
constexpr std::array<std::string_view, 1U> kMeshContracts{"MeshComponent"};
constexpr std::array<std::string_view, 1U> kLightContracts{"LightComponent"};
constexpr std::array<std::string_view, 1U> kColliderContracts{"ColliderComponent"};
constexpr std::array<std::string_view, 1U> kAreaContracts{"AreaComponent"};
constexpr std::array<std::string_view, 1U> kRayCastContracts{"RayCast3DComponent"};
constexpr std::array<std::string_view, 3U> kKinematicContracts{
    "Kinematic3DComponent", "Rigid3DComponent", "ColliderComponent"};
constexpr std::array<std::string_view, 1U> kNavMeshVolumeContracts{"NavMeshVolume3DComponent"};
constexpr std::array<std::string_view, 1U> kNavSeekerContracts{"NavSeeker3DComponent"};
constexpr std::array<std::string_view, 1U> kSkeletonContracts{"Skeleton3DComponent"};
constexpr std::array<std::string_view, 1U> kTwoBoneIKContracts{"TwoBoneIK3DComponent"};
constexpr std::array<std::string_view, 1U> kFullBodyIKContracts{"FullBodyIK3DComponent"};
constexpr std::array<std::string_view, 1U> kBoneAttachmentContracts{"BoneAttachment3DComponent"};
constexpr std::array<std::string_view, 1U> kSpringArmContracts{"SpringArm3DComponent"};
constexpr std::array<std::string_view, 1U> kMarkerContracts{"Marker3DComponent"};
constexpr std::array<std::string_view, 1U> kHitboxContracts{"Hitbox3DComponent"};
constexpr std::array<std::string_view, 1U> kHurtboxContracts{"Hurtbox3DComponent"};
constexpr std::array<std::string_view, 1U> kProjectileContracts{"Projectile3DComponent"};
constexpr std::array<std::string_view, 1U> kInteractionAreaContracts{"InteractionArea3DComponent"};
constexpr std::array<std::string_view, 1U> kEnvironmentContracts{"WorldEnvironment3DComponent"};
constexpr std::array<std::string_view, 1U> kReflectionProbeContracts{"ReflectionProbe3DComponent"};
constexpr std::array<std::string_view, 1U> kDecalContracts{"Decal3DComponent"};
constexpr std::array<std::string_view, 2U> kDirectionalLightContracts{"DirectionalLight3DComponent", "LightEmitterComponent"};
constexpr std::array<std::string_view, 1U> kFogVolumeContracts{"FogVolume3DComponent"};
constexpr std::array<std::string_view, 1U> kLodContracts{"LodGroup3DComponent"};
constexpr std::array<std::string_view, 1U> kOccluderContracts{"Occluder3DComponent"};
constexpr std::array<std::string_view, 1U> kVisibilityContracts{"VisibilityRegion3DComponent"};
constexpr std::array<std::string_view, 1U> kSpawnContracts{"SpawnPoint3DComponent"};
constexpr std::array<std::string_view, 1U> kStreamerContracts{"LevelStreamer3DComponent"};
constexpr std::array<std::string_view, 1U> kPartitionContracts{"WorldPartition3DComponent"};
constexpr std::array<std::string_view, 2U> kRigid3DContracts{"Rigid3DComponent", "ColliderComponent"};
constexpr std::array<std::string_view, 1U> kAudioContracts{"AudioSourceComponent"};
constexpr std::array<std::string_view, 1U> kParticleContracts{"ParticleEmitterComponent"};
constexpr std::array<std::string_view, 1U> kScriptContracts{"ScriptComponent"};
constexpr std::array<std::string_view, 1U> kCanvasContracts{"CanvasComponent"};
constexpr std::array<std::string_view, 1U> kUITextContracts{"UITextComponent"};
constexpr std::array<std::string_view, 1U> kUIImageContracts{"UIImageComponent"};
constexpr std::array<std::string_view, 1U> kUIButtonContracts{"UIButtonComponent"};
constexpr std::array<std::string_view, 1U> kSkyContracts{"Sky3DComponent"};
constexpr std::array<std::string_view, 1U> kRagdoll3DContracts{"Ragdoll3DComponent"};

constexpr std::array<SceneObjectDescriptor, 53U> kDescriptors{
    // The document's structural root: created by the document lifecycle (new document,
    // load-time migration), never through the Add-Object library - libraryCreatable is false.
    SceneObjectDescriptor{SceneObjectKind::SceneRoot, "scene_root", "SceneRoot", "Scene", "Scene/SceneRootObjectDefinition", kNoContracts, false},
    // The transform-only base object, type id "object_3d". Documents and layouts written while the
    // kind was called "node_3d", or "empty" before that, keep loading: FindSceneObjectDescriptor
    // (typeId) resolves both legacy ids to this same row, so neither rename touches a saved file.
    SceneObjectDescriptor{SceneObjectKind::Object3D, "object_3d", "Object3D", "Scene", "Scene/ECS", kNoContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Area3D, "area_3d", "Area3D", "Physics", "Physics/AreaOverlapSystem", kAreaContracts, true},
    SceneObjectDescriptor{SceneObjectKind::RayCast3D, "ray_cast_3d", "RayCast3D", "Physics", "Physics/RaycastSystem", kRayCastContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Static3D, "static_3d", "Static3D", "Physics", "Physics/CollisionSystem", kColliderContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Kinematic3D, "kinematic_3d", "Kinematic3D", "Physics", "Physics/KinematicBody", kKinematicContracts, true},
    SceneObjectDescriptor{SceneObjectKind::NavMeshVolume3D, "nav_mesh_volume_3d", "NavMeshVolume3D", "Navigation", "Scene/NavMeshVolume3DComponent", kNavMeshVolumeContracts, true},
    SceneObjectDescriptor{SceneObjectKind::NavSeeker3D, "nav_seeker_3d", "NavSeeker3D", "Navigation", "Scene/NavSeeker3DComponent", kNavSeekerContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Skeleton3D, "skeleton_3d", "Skeleton3D", "Animation", "Scene/Skeleton3DComponent", kSkeletonContracts, true},
    SceneObjectDescriptor{SceneObjectKind::BoneAttachment3D, "bone_attachment_3d", "BoneAttachment3D", "Animation", "Scene/BoneAttachment3DComponent", kBoneAttachmentContracts, true},
    SceneObjectDescriptor{SceneObjectKind::TwoBoneIK3D, "two_bone_ik_3d", "TwoBoneIK3D", "Animation", "Scene/TwoBoneIK3DComponent", kTwoBoneIKContracts, true},
    SceneObjectDescriptor{SceneObjectKind::FullBodyIK3D, "full_body_ik_3d", "FullBodyIK3D", "Animation", "Scene/FullBodyIK3DComponent", kFullBodyIKContracts, true},
    SceneObjectDescriptor{SceneObjectKind::SpringArm3D, "spring_arm_3d", "SpringArm3D", "Camera", "Physics/SpringArm", kSpringArmContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Marker3D, "marker_3d", "Marker3D", "Scene", "Scene/Marker3DComponent", kMarkerContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Hitbox3D, "hitbox_3d", "Hitbox3D", "Combat", "Physics/Hitbox3DComponent", kHitboxContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Hurtbox3D, "hurtbox_3d", "Hurtbox3D", "Combat", "Physics/Hurtbox3DComponent", kHurtboxContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Projectile3D, "projectile_3d", "Projectile3D", "Combat", "Physics/Projectile3DComponent", kProjectileContracts, true},
    SceneObjectDescriptor{SceneObjectKind::InteractionArea3D, "interaction_area_3d", "InteractionArea3D", "Gameplay", "Physics/AreaOverlapSystem", kInteractionAreaContracts, true},
    SceneObjectDescriptor{SceneObjectKind::WorldEnvironment3D, "world_environment_3d", "WorldEnvironment", "Rendering", "Render/WorldEnvironment3DComponent", kEnvironmentContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Sky, "sky", "Sky", "Rendering", "Render/Sky3DComponent", kSkyContracts, true},
    SceneObjectDescriptor{SceneObjectKind::ReflectionProbe3D, "reflection_probe_3d", "ReflectionProbe3D", "Rendering", "Render/ReflectionProbe3DComponent", kReflectionProbeContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Decal3D, "decal_3d", "Decal3D", "Rendering", "Render/Decal3DComponent", kDecalContracts, true},
    SceneObjectDescriptor{SceneObjectKind::FogVolume3D, "fog_volume_3d", "FogVolume3D", "Rendering", "Render/FogVolume3DComponent", kFogVolumeContracts, true},
    SceneObjectDescriptor{SceneObjectKind::LODGroup3D, "lod_group_3d", "LODGroup3D", "Optimization", "Render/LodGroup3DComponent", kLodContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Occluder3D, "occluder_3d", "Occluder3D", "Optimization", "Render/Occluder3DComponent", kOccluderContracts, true},
    SceneObjectDescriptor{SceneObjectKind::VisibilityRegion3D, "visibility_region_3d", "VisibilityRegion3D", "Optimization", "Render/VisibilityRegion3DComponent", kVisibilityContracts, true},
    SceneObjectDescriptor{SceneObjectKind::SpawnPoint3D, "spawn_point_3d", "SpawnPoint3D", "Gameplay", "Scene/SpawnPointQuery", kSpawnContracts, true},
    SceneObjectDescriptor{SceneObjectKind::LevelStreamer3D, "level_streamer_3d", "LevelStreamer3D", "World", "Scene/LevelStreamer3DComponent", kStreamerContracts, true},
    SceneObjectDescriptor{SceneObjectKind::WorldPartition3D, "world_partition_3d", "WorldPartition3D", "World", "Scene/WorldPartition3DComponent", kPartitionContracts, true},
    SceneObjectDescriptor{SceneObjectKind::AnimationGraph, "animation_graph", "AnimationGraph", "Animation", "Scene/AnimationGraphComponent", kAnimationGraphContracts, true},
    SceneObjectDescriptor{SceneObjectKind::AnimationSequencer, "animation_sequencer", "AnimationSequencer", "Animation", "Scene/AnimationSequencerComponent", kAnimationSequencerContracts, true},
    SceneObjectDescriptor{SceneObjectKind::MontageSequencer, "montage_sequencer", "MontageSequencer", "Animation", "Scene/MontageSequencerComponent", kMontageSequencerContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Character3D, "character_3d", "Character3D", "Physics", "Physics/CharacterController", kCharacterContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Camera3D, "camera_3d", "Camera3D", "Rendering", "Render/CameraSystem", kCameraContracts, true},
    SceneObjectDescriptor{SceneObjectKind::MeshInstance3D, "mesh_instance_3d", "MeshInstance3D", "Rendering", "Render/MeshRenderer", kMeshContracts, true},
    SceneObjectDescriptor{SceneObjectKind::BoxMesh3D, "box_mesh_3d", "BoxMesh3D", "Rendering", "Render/PrimitiveMesh", kNoContracts, true},
    SceneObjectDescriptor{SceneObjectKind::SphereMesh3D, "sphere_mesh_3d", "SphereMesh3D", "Rendering", "Render/PrimitiveMesh", kNoContracts, true},
    SceneObjectDescriptor{SceneObjectKind::PlaneMesh3D, "plane_mesh_3d", "PlaneMesh3D", "Rendering", "Render/PrimitiveMesh", kNoContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Light3D, "light_3d", "Light3D", "Rendering", "Render/LightSystem", kLightContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Collider3D, "collider_3d", "Collider3D", "Physics", "Physics/CollisionSystem", kColliderContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Rigid3D, "rigid_3d", "Rigid3D", "Physics", "Physics/PhysicsSystem", kRigid3DContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Ragdoll3D, "ragdoll_3d", "Ragdoll3D", "Physics", "Scene/Ragdoll3DComponent", kRagdoll3DContracts, true},
    SceneObjectDescriptor{SceneObjectKind::AudioSource3D, "audio_source_3d", "AudioSource3D", "Audio", "Audio/AudioSourceSystem", kAudioContracts, true},
    SceneObjectDescriptor{SceneObjectKind::ParticleEmitter3D, "particle_emitter_3d", "ParticleEmitter3D", "VFX", "Scene/ParticleRuntime", kParticleContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Script, "script", "Script", "Logic", "Scripting/ScriptRuntime", kScriptContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Canvas, "canvas", "Canvas", "UI", "UI/UIRuntime", kCanvasContracts, true},
    SceneObjectDescriptor{SceneObjectKind::UIText, "ui_text", "UI Text", "UI", "UI/UIRuntime", kUITextContracts, true},
    SceneObjectDescriptor{SceneObjectKind::UIImage, "ui_image", "UI Image", "UI", "UI/UIRuntime", kUIImageContracts, true},
    SceneObjectDescriptor{SceneObjectKind::UIButton, "ui_button", "UI Button", "UI", "UI/UIRuntime", kUIButtonContracts, true},
    SceneObjectDescriptor{SceneObjectKind::Folder, "folder", "Folder", "Scene", "Scene/Editor", kNoContracts, true},
    SceneObjectDescriptor{SceneObjectKind::DirectionalLight3D, "directional_light_3d", "DirectionalLight3D", "Rendering", "Render/LightSystem", kDirectionalLightContracts, true},
    // Called "viewport" before being renamed Viewport3D: FindSceneObjectDescriptor
    // (typeId) resolves the legacy id to this same row, so the rename touches no saved file.
    SceneObjectDescriptor{SceneObjectKind::Viewport3D, "viewport_3d", "Viewport3D", "Scene", "Scene/Editor", kNoContracts, false},
    SceneObjectDescriptor{SceneObjectKind::Player3D, "player_3d", "Player3D", "Gameplay", "Scene/Player3DObjectDefinition", kPlayerContracts, true},
};

} // namespace

std::span<const SceneObjectDescriptor> GetSceneObjectDescriptors() noexcept {
    return kDescriptors;
}

const SceneObjectDescriptor* FindSceneObjectDescriptor(const SceneObjectKind kind) noexcept {
    for (const SceneObjectDescriptor& descriptor : kDescriptors) {
        if (descriptor.kind == kind) {
            return &descriptor;
        }
    }
    return nullptr;
}

const SceneObjectDescriptor* FindSceneObjectDescriptor(const std::string_view typeId) noexcept {
    // Legacy ids accepted on load: a saved document or layout carrying an older string must keep
    // resolving to the same object, so every rename leaves its previous id readable here forever.
    if (typeId == "empty" || typeId == "node_3d") {
        return FindSceneObjectDescriptor(SceneObjectKind::Object3D);
    }
    if (typeId == "static_body_3d") {
        return FindSceneObjectDescriptor(SceneObjectKind::Static3D);
    }
    if (typeId == "rigid_body_3d") {
        return FindSceneObjectDescriptor(SceneObjectKind::Rigid3D);
    }
    if (typeId == "character_body_3d") {
        return FindSceneObjectDescriptor(SceneObjectKind::Character3D);
    }
    if (typeId == "animatable_body_3d") {
        return FindSceneObjectDescriptor(SceneObjectKind::Kinematic3D);
    }
    if (typeId == "animation_player") {
        return FindSceneObjectDescriptor(SceneObjectKind::AnimationSequencer);
    }
    if (typeId == "montage_player") {
        return FindSceneObjectDescriptor(SceneObjectKind::MontageSequencer);
    }
    if (typeId == "animation_tree") {
        return FindSceneObjectDescriptor(SceneObjectKind::AnimationGraph);
    }
    if (typeId == "navigation_region_3d") {
        return FindSceneObjectDescriptor(SceneObjectKind::NavMeshVolume3D);
    }
    if (typeId == "navigation_agent_3d") {
        return FindSceneObjectDescriptor(SceneObjectKind::NavSeeker3D);
    }
    if (typeId == "viewport") {
        return FindSceneObjectDescriptor(SceneObjectKind::Viewport3D);
    }
    for (const SceneObjectDescriptor& descriptor : kDescriptors) {
        if (descriptor.typeId == typeId) {
            return &descriptor;
        }
    }
    return nullptr;
}

std::string_view GetSceneObjectTypeId(const SceneObjectKind kind) noexcept {
    const SceneObjectDescriptor* descriptor = FindSceneObjectDescriptor(kind);
    return descriptor == nullptr ? std::string_view{} : descriptor->typeId;
}

SceneObjectPlacement GetSceneObjectPlacement(const SceneObjectKind kind) noexcept {
    switch (kind) {
        case SceneObjectKind::Character3D:
        case SceneObjectKind::Player3D:
        case SceneObjectKind::AnimationSequencer:
        case SceneObjectKind::MontageSequencer:
        case SceneObjectKind::AnimationGraph:
        case SceneObjectKind::Skeleton3D:
        case SceneObjectKind::TwoBoneIK3D:
        case SceneObjectKind::FullBodyIK3D:
        case SceneObjectKind::BoneAttachment3D:
        case SceneObjectKind::Ragdoll3D:
        case SceneObjectKind::NavSeeker3D:
        case SceneObjectKind::SpringArm3D:
        case SceneObjectKind::Hitbox3D:
        case SceneObjectKind::Hurtbox3D:
        case SceneObjectKind::Projectile3D:
            return SceneObjectPlacement::Entity;
        default:
            return SceneObjectPlacement::World;
    }
}

} // namespace UVE::Scene::Objects
