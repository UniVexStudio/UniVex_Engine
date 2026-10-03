// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/objects/scene_object_type_uve.h"

#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/animation_graph_component_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/all_objects_3d_uve.h"
#include "uve/scene/objects/scene_folder_uve.h"
#include "uve/scene/objects/scene_root_uve.h"

namespace UVE::Scene {
namespace {

using Kind = Objects::SceneObjectKindUVE;

/// The kinds whose own object component says exactly what they are. Checked before the shared
/// components below, since several of these objects also carry one (an Kinematic3D has a
/// collider and a body).
template <typename Component>
struct OwnComponentUVE final {
    Kind kind;
};

template <typename... Components>
[[nodiscard]] bool TryOwnComponentUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity, Kind& out,
                                      const OwnComponentUVE<Components>&... candidates) {
    return ((entityManager.HasComponentUVE<Components>(entity) ? (out = candidates.kind, true) : false) || ...);
}

} // namespace

bool IsSceneObjectTypeComponentValidUVE(const SceneObjectTypeComponentUVE& value) noexcept {
    return Objects::FindSceneObjectDescriptorUVE(value.kind) != nullptr;
}

Objects::SceneObjectKindUVE ResolveSceneObjectKindUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity)) {
        return Kind::Object3D;
    }
    if (entityManager.HasComponentUVE<SceneObjectTypeComponentUVE>(entity)) {
        const SceneObjectTypeComponentUVE& type = entityManager.GetComponentUVE<SceneObjectTypeComponentUVE>(entity);
        if (IsSceneObjectTypeComponentValidUVE(type)) {
            return type.kind;
        }
    }
    return InferSceneObjectKindUVE(entityManager, entity);
}

Objects::SceneObjectKindUVE InferSceneObjectKindUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity)) {
        return Kind::Object3D;
    }
    if (entityManager.HasComponentUVE<SceneRootComponentUVE>(entity)) {
        return Kind::SceneRoot;
    }
    if (entityManager.HasComponentUVE<OutlinerViewportComponentUVE>(entity)) {
        return Kind::Viewport;
    }
    if (entityManager.HasComponentUVE<FolderComponentUVE>(entity)) {
        return Kind::Folder;
    }
    // A primitive also carries a collider, so it is read before the physics kinds.
    if (entityManager.HasComponentUVE<PrimitiveMeshComponentUVE>(entity)) {
        switch (entityManager.GetComponentUVE<PrimitiveMeshComponentUVE>(entity).kind) {
            case PrimitiveMeshKindUVE::Cube:
                return Kind::BoxMesh3D;
            case PrimitiveMeshKindUVE::UVSphere:
                return Kind::SphereMesh3D;
            case PrimitiveMeshKindUVE::Plane:
                return Kind::PlaneMesh3D;
        }
    }

    Kind kind = Kind::Object3D;
    if (TryOwnComponentUVE(entityManager, entity, kind,
                           OwnComponentUVE<Kinematic3DComponentUVE>{Kind::Kinematic3D},
                           OwnComponentUVE<RayCast3DComponentUVE>{Kind::RayCast3D},
                           OwnComponentUVE<NavigationRegion3DComponentUVE>{Kind::NavigationRegion3D},
                           OwnComponentUVE<NavigationAgent3DComponentUVE>{Kind::NavigationAgent3D},
                           OwnComponentUVE<Skeleton3DComponentUVE>{Kind::Skeleton3D},
                           OwnComponentUVE<BoneAttachment3DComponentUVE>{Kind::BoneAttachment3D},
                           OwnComponentUVE<SpringArm3DComponentUVE>{Kind::SpringArm3D},
                           OwnComponentUVE<Marker3DComponentUVE>{Kind::Marker3D},
                           OwnComponentUVE<Hitbox3DComponentUVE>{Kind::Hitbox3D},
                           OwnComponentUVE<Hurtbox3DComponentUVE>{Kind::Hurtbox3D},
                           OwnComponentUVE<Projectile3DComponentUVE>{Kind::Projectile3D},
                           OwnComponentUVE<InteractionArea3DComponentUVE>{Kind::InteractionArea3D},
                           OwnComponentUVE<WorldEnvironment3DComponentUVE>{Kind::WorldEnvironment3D},
                           OwnComponentUVE<ReflectionProbe3DComponentUVE>{Kind::ReflectionProbe3D},
                           OwnComponentUVE<Decal3DComponentUVE>{Kind::Decal3D},
                           OwnComponentUVE<DirectionalLight3DComponentUVE>{Kind::DirectionalLight3D},
                           OwnComponentUVE<FogVolume3DComponentUVE>{Kind::FogVolume3D},
                           OwnComponentUVE<LodGroup3DComponentUVE>{Kind::LODGroup3D},
                           OwnComponentUVE<Occluder3DComponentUVE>{Kind::Occluder3D},
                           OwnComponentUVE<VisibilityRegion3DComponentUVE>{Kind::VisibilityRegion3D},
                           OwnComponentUVE<SpawnPoint3DComponentUVE>{Kind::SpawnPoint3D},
                           OwnComponentUVE<LevelStreamer3DComponentUVE>{Kind::LevelStreamer3D},
                           OwnComponentUVE<WorldPartition3DComponentUVE>{Kind::WorldPartition3D},
                           OwnComponentUVE<CameraComponentUVE>{Kind::Camera3D},
                           OwnComponentUVE<LightComponentUVE>{Kind::Light3D},
                           OwnComponentUVE<MeshComponentUVE>{Kind::MeshInstance3D},
                           OwnComponentUVE<AudioSourceComponentUVE>{Kind::AudioSource3D},
                           OwnComponentUVE<ParticleEmitterComponentUVE>{Kind::ParticleEmitter3D},
                           OwnComponentUVE<AnimationSequencerComponentUVE>{Kind::AnimationSequencer},
                           OwnComponentUVE<AnimationGraphComponentUVE>{Kind::AnimationGraph},
                           OwnComponentUVE<CanvasComponentUVE>{Kind::Canvas},
                           OwnComponentUVE<UITextComponentUVE>{Kind::UIText},
                           OwnComponentUVE<UIImageComponentUVE>{Kind::UIImage},
                           OwnComponentUVE<UIButtonComponentUVE>{Kind::UIButton},
                           OwnComponentUVE<AreaComponentUVE>{Kind::Area3D},
                           OwnComponentUVE<CharacterControllerComponentUVE>{Kind::Character3D})) {
        return kind;
    }

    // The bodies are told apart by what they combine. A collider with a body is how a
    // Character3D is built; a collider alone is either a Collider3D or a Static3D, which are
    // built identically, and reads as Collider3D, the older of the two.
    const bool collider = entityManager.HasComponentUVE<ColliderComponentUVE>(entity);
    const bool body = entityManager.HasComponentUVE<Rigid3DComponentUVE>(entity);
    if (collider && body) {
        return Kind::Character3D;
    }
    if (body) {
        return Kind::Rigid3D;
    }
    if (collider) {
        return Kind::Collider3D;
    }
    // Last: a script can sit on any object, so it names the object only when nothing else does.
    if (entityManager.HasComponentUVE<ScriptComponentUVE>(entity)) {
        return Kind::Script;
    }
    return Kind::Object3D;
}

void SetSceneObjectKindUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Objects::SceneObjectKindUVE kind) {
    if (entityManager.HasComponentUVE<SceneObjectTypeComponentUVE>(entity)) {
        entityManager.GetComponentUVE<SceneObjectTypeComponentUVE>(entity).kind = kind;
        return;
    }
    entityManager.AddComponentUVE<SceneObjectTypeComponentUVE>(entity, SceneObjectTypeComponentUVE{kind});
}

} // namespace UVE::Scene
