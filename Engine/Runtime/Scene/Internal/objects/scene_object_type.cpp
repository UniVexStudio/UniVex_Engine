// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "scene_object_type.h"

#include "animation_sequencer_component.h"
#include "montage_sequencer_component.h"
#include "animation_graph_component.h"
#include "area_component.h"
#include "audio_source_component.h"
#include "camera_component.h"
#include "canvas_component.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "light_component.h"
#include "mesh_component.h"
#include "particle_emitter_component.h"
#include "primitive_mesh_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_text_component.h"
#include "i_entity_manager.h"
#include "all_objects_3d.h"
#include "scene_folder.h"
#include "scene_root.h"

namespace UVE::Scene {
namespace {

using Kind = Objects::SceneObjectKind;

/// The kinds whose own object component says exactly what they are. Checked before the shared
/// components below, since several of these objects also carry one (an Kinematic3D has a
/// collider and a body).
template <typename Component>
struct OwnComponent final {
    Kind kind;
};

template <typename... Components>
[[nodiscard]] bool TryOwnComponent(const IEntityManager& entityManager, const Entity entity, Kind& out,
                                      const OwnComponent<Components>&... candidates) {
    return ((entityManager.HasComponent<Components>(entity) ? (out = candidates.kind, true) : false) || ...);
}

} // namespace

bool IsSceneObjectTypeComponentValid(const SceneObjectTypeComponent& value) noexcept {
    return Objects::FindSceneObjectDescriptor(value.kind) != nullptr;
}

Objects::SceneObjectKind ResolveSceneObjectKind(const IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity)) {
        return Kind::Object3D;
    }
    if (entityManager.HasComponent<SceneObjectTypeComponent>(entity)) {
        const SceneObjectTypeComponent& type = entityManager.GetComponent<SceneObjectTypeComponent>(entity);
        if (IsSceneObjectTypeComponentValid(type)) {
            return type.kind;
        }
    }
    return InferSceneObjectKind(entityManager, entity);
}

Objects::SceneObjectKind InferSceneObjectKind(const IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity)) {
        return Kind::Object3D;
    }
    if (entityManager.HasComponent<SceneRootComponent>(entity)) {
        return Kind::SceneRoot;
    }
    if (entityManager.HasComponent<OutlinerViewport3DComponent>(entity)) {
        return Kind::Viewport3D;
    }
    if (entityManager.HasComponent<FolderComponent>(entity)) {
        return Kind::Folder;
    }
    // A primitive also carries a collider, so it is read before the physics kinds.
    if (entityManager.HasComponent<PrimitiveMeshComponent>(entity)) {
        switch (entityManager.GetComponent<PrimitiveMeshComponent>(entity).kind) {
            case PrimitiveMeshKind::Cube:
                return Kind::BoxMesh3D;
            case PrimitiveMeshKind::UVSphere:
                return Kind::SphereMesh3D;
            case PrimitiveMeshKind::Plane:
                return Kind::PlaneMesh3D;
            case PrimitiveMeshKind::Capsule:
            case PrimitiveMeshKind::Cylinder:
                return Kind::MeshInstance3D;
        }
    }

    Kind kind = Kind::Object3D;
    if (TryOwnComponent(entityManager, entity, kind,
                           OwnComponent<Kinematic3DComponent>{Kind::Kinematic3D},
                           OwnComponent<RayCast3DComponent>{Kind::RayCast3D},
                           OwnComponent<NavMeshVolume3DComponent>{Kind::NavMeshVolume3D},
                           OwnComponent<NavSeeker3DComponent>{Kind::NavSeeker3D},
                           OwnComponent<Skeleton3DComponent>{Kind::Skeleton3D},
                           OwnComponent<BoneAttachment3DComponent>{Kind::BoneAttachment3D},
                           OwnComponent<TwoBoneIK3DComponent>{Kind::TwoBoneIK3D},
                           OwnComponent<FullBodyIK3DComponent>{Kind::FullBodyIK3D},
                           OwnComponent<Ragdoll3DComponent>{Kind::Ragdoll3D},
                           OwnComponent<SpringArm3DComponent>{Kind::SpringArm3D},
                           OwnComponent<Marker3DComponent>{Kind::Marker3D},
                           OwnComponent<Hitbox3DComponent>{Kind::Hitbox3D},
                           OwnComponent<Hurtbox3DComponent>{Kind::Hurtbox3D},
                           OwnComponent<Projectile3DComponent>{Kind::Projectile3D},
                           OwnComponent<InteractionArea3DComponent>{Kind::InteractionArea3D},
                           OwnComponent<WorldEnvironment3DComponent>{Kind::WorldEnvironment3D},
                           OwnComponent<Sky3DComponent>{Kind::Sky},
                           OwnComponent<ReflectionProbe3DComponent>{Kind::ReflectionProbe3D},
                           OwnComponent<Decal3DComponent>{Kind::Decal3D},
                           OwnComponent<DirectionalLight3DComponent>{Kind::DirectionalLight3D},
                           OwnComponent<FogVolume3DComponent>{Kind::FogVolume3D},
                           OwnComponent<LodGroup3DComponent>{Kind::LODGroup3D},
                           OwnComponent<Occluder3DComponent>{Kind::Occluder3D},
                           OwnComponent<VisibilityRegion3DComponent>{Kind::VisibilityRegion3D},
                           OwnComponent<SpawnPoint3DComponent>{Kind::SpawnPoint3D},
                           OwnComponent<LevelStreamer3DComponent>{Kind::LevelStreamer3D},
                           OwnComponent<WorldPartition3DComponent>{Kind::WorldPartition3D},
                           OwnComponent<CameraComponent>{Kind::Camera3D},
                           OwnComponent<LightComponent>{Kind::Light3D},
                           OwnComponent<MeshComponent>{Kind::MeshInstance3D},
                           OwnComponent<AudioSourceComponent>{Kind::AudioSource3D},
                           OwnComponent<ParticleEmitterComponent>{Kind::ParticleEmitter3D},
                           OwnComponent<AnimationSequencerComponent>{Kind::AnimationSequencer},
                           OwnComponent<MontageSequencerComponent>{Kind::MontageSequencer},
                           OwnComponent<AnimationGraphComponent>{Kind::AnimationGraph},
                           OwnComponent<CanvasComponent>{Kind::Canvas},
                           OwnComponent<UITextComponent>{Kind::UIText},
                           OwnComponent<UIImageComponent>{Kind::UIImage},
                           OwnComponent<UIButtonComponent>{Kind::UIButton},
                           OwnComponent<AreaComponent>{Kind::Area3D},
                           OwnComponent<PlayerComponent>{Kind::Player3D},
                           OwnComponent<CharacterControllerComponent>{Kind::Character3D})) {
        return kind;
    }

    // The bodies are told apart by what they combine. What makes a character a character is its
    // controller, and that was already checked above - so a collider with a body and no
    // controller is a Rigid3D with a shape, which is what the Rigid3D recipe itself creates.
    // A collider alone is either a Collider3D or a Static3D, which are built identically and read
    // as Collider3D, the older of the two.
    const bool collider = entityManager.HasComponent<ColliderComponent>(entity);
    const bool body = entityManager.HasComponent<Rigid3DComponent>(entity);
    if (body) {
        return Kind::Rigid3D;
    }
    if (collider) {
        return Kind::Collider3D;
    }
    // Last: a script can sit on any object, so it names the object only when nothing else does.
    if (entityManager.HasComponent<ScriptComponent>(entity)) {
        return Kind::Script;
    }
    return Kind::Object3D;
}

void SetSceneObjectKind(IEntityManager& entityManager, const Entity entity, const Objects::SceneObjectKind kind) {
    if (entityManager.HasComponent<SceneObjectTypeComponent>(entity)) {
        entityManager.GetComponent<SceneObjectTypeComponent>(entity).kind = kind;
        return;
    }
    entityManager.AddComponent<SceneObjectTypeComponent>(entity, SceneObjectTypeComponent{kind});
}

} // namespace UVE::Scene
