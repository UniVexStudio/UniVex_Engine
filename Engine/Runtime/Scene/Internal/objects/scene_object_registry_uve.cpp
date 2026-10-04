// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/objects/scene_object_registry_uve.h"

namespace UVE::Scene::Objects {
namespace {

constexpr std::array<std::string_view, 0U> kNoContracts{};
constexpr std::array<std::string_view, 1U> kAnimationGraphContracts{"AnimationGraphComponentUVE"};
constexpr std::array<std::string_view, 1U> kAnimationSequencerContracts{"AnimationSequencerComponentUVE"};
constexpr std::array<std::string_view, 2U> kCharacterContracts{
    "TransformComponentUVE", "ColliderComponentUVE"};
constexpr std::array<std::string_view, 1U> kCameraContracts{"CameraComponentUVE"};
constexpr std::array<std::string_view, 1U> kMeshContracts{"MeshComponentUVE"};
constexpr std::array<std::string_view, 1U> kLightContracts{"LightComponentUVE"};
constexpr std::array<std::string_view, 1U> kColliderContracts{"ColliderComponentUVE"};
constexpr std::array<std::string_view, 1U> kAreaContracts{"AreaComponentUVE"};
constexpr std::array<std::string_view, 1U> kRayCastContracts{"RayCast3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kKinematicContracts{"Kinematic3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kNavMeshVolumeContracts{"NavMeshVolume3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kNavSeekerContracts{"NavSeeker3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kSkeletonContracts{"Skeleton3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kBoneAttachmentContracts{"BoneAttachment3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kSpringArmContracts{"SpringArm3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kMarkerContracts{"Marker3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kHitboxContracts{"Hitbox3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kHurtboxContracts{"Hurtbox3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kProjectileContracts{"Projectile3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kInteractionAreaContracts{"InteractionArea3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kEnvironmentContracts{"WorldEnvironment3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kReflectionProbeContracts{"ReflectionProbe3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kDecalContracts{"Decal3DComponentUVE"};
constexpr std::array<std::string_view, 2U> kDirectionalLightContracts{"DirectionalLight3DComponentUVE", "LightEmitterComponentUVE"};
constexpr std::array<std::string_view, 1U> kFogVolumeContracts{"FogVolume3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kLodContracts{"LodGroup3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kOccluderContracts{"Occluder3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kVisibilityContracts{"VisibilityRegion3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kSpawnContracts{"SpawnPoint3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kStreamerContracts{"LevelStreamer3DComponentUVE"};
constexpr std::array<std::string_view, 1U> kPartitionContracts{"WorldPartition3DComponentUVE"};
constexpr std::array<std::string_view, 2U> kRigid3DContracts{"Rigid3DComponentUVE", "ColliderComponentUVE"};
constexpr std::array<std::string_view, 1U> kAudioContracts{"AudioSourceComponentUVE"};
constexpr std::array<std::string_view, 1U> kParticleContracts{"ParticleEmitterComponentUVE"};
constexpr std::array<std::string_view, 1U> kScriptContracts{"ScriptComponentUVE"};
constexpr std::array<std::string_view, 1U> kCanvasContracts{"CanvasComponentUVE"};
constexpr std::array<std::string_view, 1U> kUITextContracts{"UITextComponentUVE"};
constexpr std::array<std::string_view, 1U> kUIImageContracts{"UIImageComponentUVE"};
constexpr std::array<std::string_view, 1U> kUIButtonContracts{"UIButtonComponentUVE"};

constexpr std::array<SceneObjectDescriptorUVE, 47U> kDescriptors{
    // The document's structural root: created by the document lifecycle (new document,
    // load-time migration), never through the Add-Object library - libraryCreatable is false.
    SceneObjectDescriptorUVE{SceneObjectKindUVE::SceneRoot, "scene_root", "SceneRoot", "Scene", "Scene/SceneRootObjectDefinitionUVE", kNoContracts, false},
    // The transform-only base object, type id "object_3d". Documents and layouts written while the
    // kind was called "node_3d", or "empty" before that, keep loading: FindSceneObjectDescriptorUVE
    // (typeId) resolves both legacy ids to this same row, so neither rename touches a saved file.
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Object3D, "object_3d", "Object3D", "Scene", "Scene/ECS", kNoContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Area3D, "area_3d", "Area3D", "Physics", "Physics/AreaOverlapSystemUVE", kAreaContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::RayCast3D, "ray_cast_3d", "RayCast3D", "Physics", "Physics/RaycastSystemUVE", kRayCastContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Static3D, "static_3d", "Static3D", "Physics", "Physics/CollisionSystemUVE", kColliderContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Kinematic3D, "kinematic_3d", "Kinematic3D", "Physics", "Scene/Kinematic3DComponentUVE", kKinematicContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::NavMeshVolume3D, "nav_mesh_volume_3d", "NavMeshVolume3D", "Navigation", "Scene/NavMeshVolume3DComponentUVE", kNavMeshVolumeContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::NavSeeker3D, "nav_seeker_3d", "NavSeeker3D", "Navigation", "Scene/NavSeeker3DComponentUVE", kNavSeekerContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Skeleton3D, "skeleton_3d", "Skeleton3D", "Animation", "Scene/Skeleton3DComponentUVE", kSkeletonContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::BoneAttachment3D, "bone_attachment_3d", "BoneAttachment3D", "Animation", "Scene/BoneAttachment3DComponentUVE", kBoneAttachmentContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::SpringArm3D, "spring_arm_3d", "SpringArm3D", "Camera", "Physics/RaycastSystemUVE", kSpringArmContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Marker3D, "marker_3d", "Marker3D", "Scene", "Scene/Marker3DComponentUVE", kMarkerContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Hitbox3D, "hitbox_3d", "Hitbox3D", "Combat", "Physics/Hitbox3DComponentUVE", kHitboxContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Hurtbox3D, "hurtbox_3d", "Hurtbox3D", "Combat", "Physics/Hurtbox3DComponentUVE", kHurtboxContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Projectile3D, "projectile_3d", "Projectile3D", "Combat", "Physics/Projectile3DComponentUVE", kProjectileContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::InteractionArea3D, "interaction_area_3d", "InteractionArea3D", "Gameplay", "Physics/AreaOverlapSystemUVE", kInteractionAreaContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::WorldEnvironment3D, "world_environment_3d", "WorldEnvironment", "Rendering", "Render/WorldEnvironment3DComponentUVE", kEnvironmentContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::ReflectionProbe3D, "reflection_probe_3d", "ReflectionProbe3D", "Rendering", "Render/ReflectionProbe3DComponentUVE", kReflectionProbeContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Decal3D, "decal_3d", "Decal3D", "Rendering", "Render/Decal3DComponentUVE", kDecalContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::FogVolume3D, "fog_volume_3d", "FogVolume3D", "Rendering", "Render/FogVolume3DComponentUVE", kFogVolumeContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::LODGroup3D, "lod_group_3d", "LODGroup3D", "Optimization", "Render/LodGroup3DComponentUVE", kLodContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Occluder3D, "occluder_3d", "Occluder3D", "Optimization", "Render/Occluder3DComponentUVE", kOccluderContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::VisibilityRegion3D, "visibility_region_3d", "VisibilityRegion3D", "Optimization", "Render/VisibilityRegion3DComponentUVE", kVisibilityContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::SpawnPoint3D, "spawn_point_3d", "SpawnPoint3D", "Gameplay", "Scene/SpawnPoint3DComponentUVE", kSpawnContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::LevelStreamer3D, "level_streamer_3d", "LevelStreamer3D", "World", "Scene/LevelStreamer3DComponentUVE", kStreamerContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::WorldPartition3D, "world_partition_3d", "WorldPartition3D", "World", "Scene/WorldPartition3DComponentUVE", kPartitionContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::AnimationGraph, "animation_graph", "AnimationGraph", "Animation", "Scene/AnimationGraphComponentUVE", kAnimationGraphContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::AnimationSequencer, "animation_sequencer", "AnimationSequencer", "Animation", "Scene/AnimationSequencerComponentUVE", kAnimationSequencerContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Character3D, "character_3d", "Character3D", "Physics", "Physics/CharacterControllerUVE", kCharacterContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Camera3D, "camera_3d", "Camera3D", "Rendering", "Render/CameraSystemUVE", kCameraContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::MeshInstance3D, "mesh_instance_3d", "MeshInstance3D", "Rendering", "Render/MeshRendererUVE", kMeshContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::BoxMesh3D, "box_mesh_3d", "BoxMesh3D", "Rendering", "Render/PrimitiveMesh", kNoContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::SphereMesh3D, "sphere_mesh_3d", "SphereMesh3D", "Rendering", "Render/PrimitiveMesh", kNoContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::PlaneMesh3D, "plane_mesh_3d", "PlaneMesh3D", "Rendering", "Render/PrimitiveMesh", kNoContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Light3D, "light_3d", "Light3D", "Rendering", "Render/LightSystemUVE", kLightContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Collider3D, "collider_3d", "Collider3D", "Physics", "Physics/CollisionSystemUVE", kColliderContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Rigid3D, "rigid_3d", "Rigid3D", "Physics", "Physics/PhysicsSystemUVE", kRigid3DContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::AudioSource3D, "audio_source_3d", "AudioSource3D", "Audio", "Audio/AudioSourceSystemUVE", kAudioContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::ParticleEmitter3D, "particle_emitter_3d", "ParticleEmitter3D", "VFX", "Scene/ParticleRuntimeUVE", kParticleContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Script, "script", "Script", "Logic", "Scripting/ScriptRuntimeUVE", kScriptContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Canvas, "canvas", "Canvas", "UI", "UI/UIRuntimeUVE", kCanvasContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::UIText, "ui_text", "UI Text", "UI", "UI/UIRuntimeUVE", kUITextContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::UIImage, "ui_image", "UI Image", "UI", "UI/UIRuntimeUVE", kUIImageContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::UIButton, "ui_button", "UI Button", "UI", "UI/UIRuntimeUVE", kUIButtonContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Folder, "folder", "Folder", "Scene", "Scene/Editor", kNoContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::DirectionalLight3D, "directional_light_3d", "DirectionalLight3D", "Rendering", "Render/LightSystemUVE", kDirectionalLightContracts, true},
    SceneObjectDescriptorUVE{SceneObjectKindUVE::Viewport, "viewport", "Viewport", "Scene", "Scene/Editor", kNoContracts, false},
};

} // namespace

std::span<const SceneObjectDescriptorUVE> GetSceneObjectDescriptorsUVE() noexcept {
    return kDescriptors;
}

const SceneObjectDescriptorUVE* FindSceneObjectDescriptorUVE(const SceneObjectKindUVE kind) noexcept {
    for (const SceneObjectDescriptorUVE& descriptor : kDescriptors) {
        if (descriptor.kind == kind) {
            return &descriptor;
        }
    }
    return nullptr;
}

const SceneObjectDescriptorUVE* FindSceneObjectDescriptorUVE(const std::string_view typeId) noexcept {
    // Legacy ids accepted on load: a saved document or layout carrying an older string must keep
    // resolving to the same object, so every rename leaves its previous id readable here forever.
    if (typeId == "empty" || typeId == "node_3d") {
        return FindSceneObjectDescriptorUVE(SceneObjectKindUVE::Object3D);
    }
    if (typeId == "static_body_3d") {
        return FindSceneObjectDescriptorUVE(SceneObjectKindUVE::Static3D);
    }
    if (typeId == "rigid_body_3d") {
        return FindSceneObjectDescriptorUVE(SceneObjectKindUVE::Rigid3D);
    }
    if (typeId == "character_body_3d") {
        return FindSceneObjectDescriptorUVE(SceneObjectKindUVE::Character3D);
    }
    if (typeId == "animatable_body_3d") {
        return FindSceneObjectDescriptorUVE(SceneObjectKindUVE::Kinematic3D);
    }
    if (typeId == "animation_player") {
        return FindSceneObjectDescriptorUVE(SceneObjectKindUVE::AnimationSequencer);
    }
    if (typeId == "animation_tree") {
        return FindSceneObjectDescriptorUVE(SceneObjectKindUVE::AnimationGraph);
    }
    if (typeId == "navigation_region_3d") {
        return FindSceneObjectDescriptorUVE(SceneObjectKindUVE::NavMeshVolume3D);
    }
    if (typeId == "navigation_agent_3d") {
        return FindSceneObjectDescriptorUVE(SceneObjectKindUVE::NavSeeker3D);
    }
    for (const SceneObjectDescriptorUVE& descriptor : kDescriptors) {
        if (descriptor.typeId == typeId) {
            return &descriptor;
        }
    }
    return nullptr;
}

std::string_view GetSceneObjectTypeIdUVE(const SceneObjectKindUVE kind) noexcept {
    const SceneObjectDescriptorUVE* descriptor = FindSceneObjectDescriptorUVE(kind);
    return descriptor == nullptr ? std::string_view{} : descriptor->typeId;
}

SceneObjectPlacementUVE GetSceneObjectPlacementUVE(const SceneObjectKindUVE kind) noexcept {
    switch (kind) {
        case SceneObjectKindUVE::Character3D:
        case SceneObjectKindUVE::AnimationSequencer:
        case SceneObjectKindUVE::AnimationGraph:
        case SceneObjectKindUVE::Skeleton3D:
        case SceneObjectKindUVE::BoneAttachment3D:
        case SceneObjectKindUVE::NavSeeker3D:
        case SceneObjectKindUVE::SpringArm3D:
        case SceneObjectKindUVE::Hitbox3D:
        case SceneObjectKindUVE::Hurtbox3D:
        case SceneObjectKindUVE::Projectile3D:
            return SceneObjectPlacementUVE::Entity;
        default:
            return SceneObjectPlacementUVE::World;
    }
}

} // namespace UVE::Scene::Objects
