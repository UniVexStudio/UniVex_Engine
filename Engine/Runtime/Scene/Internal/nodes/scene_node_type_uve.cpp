// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/nodes/scene_node_type_uve.h"

#include "uve/component/animation_player_component_uve.h"
#include "uve/component/animation_tree_component_uve.h"
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
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/scene/nodes/scene_folder_uve.h"
#include "uve/scene/nodes/scene_root_uve.h"

namespace UVE::Scene {
namespace {

using Kind = Nodes::SceneNodeKindUVE;

/// The kinds whose own node component says exactly what they are. Checked before the shared
/// components below, since several of these nodes also carry one (an AnimatableBody3D has a
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

bool IsSceneNodeTypeComponentValidUVE(const SceneNodeTypeComponentUVE& value) noexcept {
    return Nodes::FindSceneNodeDescriptorUVE(value.kind) != nullptr;
}

Nodes::SceneNodeKindUVE ResolveSceneNodeKindUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity)) {
        return Kind::Node3D;
    }
    if (entityManager.HasComponentUVE<SceneNodeTypeComponentUVE>(entity)) {
        const SceneNodeTypeComponentUVE& type = entityManager.GetComponentUVE<SceneNodeTypeComponentUVE>(entity);
        if (IsSceneNodeTypeComponentValidUVE(type)) {
            return type.kind;
        }
    }
    return InferSceneNodeKindUVE(entityManager, entity);
}

Nodes::SceneNodeKindUVE InferSceneNodeKindUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity)) {
        return Kind::Node3D;
    }
    if (entityManager.HasComponentUVE<SceneRootComponentUVE>(entity)) {
        return Kind::SceneRoot;
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

    Kind kind = Kind::Node3D;
    if (TryOwnComponentUVE(entityManager, entity, kind,
                           OwnComponentUVE<AnimatableBody3DNodeComponentUVE>{Kind::AnimatableBody3D},
                           OwnComponentUVE<RayCast3DNodeComponentUVE>{Kind::RayCast3D},
                           OwnComponentUVE<NavigationRegion3DNodeComponentUVE>{Kind::NavigationRegion3D},
                           OwnComponentUVE<NavigationAgent3DNodeComponentUVE>{Kind::NavigationAgent3D},
                           OwnComponentUVE<Skeleton3DNodeComponentUVE>{Kind::Skeleton3D},
                           OwnComponentUVE<BoneAttachment3DNodeComponentUVE>{Kind::BoneAttachment3D},
                           OwnComponentUVE<SpringArm3DNodeComponentUVE>{Kind::SpringArm3D},
                           OwnComponentUVE<Marker3DNodeComponentUVE>{Kind::Marker3D},
                           OwnComponentUVE<Hitbox3DNodeComponentUVE>{Kind::Hitbox3D},
                           OwnComponentUVE<Hurtbox3DNodeComponentUVE>{Kind::Hurtbox3D},
                           OwnComponentUVE<Projectile3DNodeComponentUVE>{Kind::Projectile3D},
                           OwnComponentUVE<InteractionArea3DNodeComponentUVE>{Kind::InteractionArea3D},
                           OwnComponentUVE<WorldEnvironment3DNodeComponentUVE>{Kind::WorldEnvironment3D},
                           OwnComponentUVE<ReflectionProbe3DNodeComponentUVE>{Kind::ReflectionProbe3D},
                           OwnComponentUVE<Decal3DNodeComponentUVE>{Kind::Decal3D},
                           OwnComponentUVE<FogVolume3DNodeComponentUVE>{Kind::FogVolume3D},
                           OwnComponentUVE<LodGroup3DNodeComponentUVE>{Kind::LODGroup3D},
                           OwnComponentUVE<Occluder3DNodeComponentUVE>{Kind::Occluder3D},
                           OwnComponentUVE<VisibilityRegion3DNodeComponentUVE>{Kind::VisibilityRegion3D},
                           OwnComponentUVE<SpawnPoint3DNodeComponentUVE>{Kind::SpawnPoint3D},
                           OwnComponentUVE<LevelStreamer3DNodeComponentUVE>{Kind::LevelStreamer3D},
                           OwnComponentUVE<WorldPartition3DNodeComponentUVE>{Kind::WorldPartition3D},
                           OwnComponentUVE<CameraComponentUVE>{Kind::Camera3D},
                           OwnComponentUVE<LightComponentUVE>{Kind::Light3D},
                           OwnComponentUVE<MeshComponentUVE>{Kind::MeshInstance3D},
                           OwnComponentUVE<AudioSourceComponentUVE>{Kind::AudioSource3D},
                           OwnComponentUVE<ParticleEmitterComponentUVE>{Kind::ParticleEmitter3D},
                           OwnComponentUVE<AnimationPlayerComponentUVE>{Kind::AnimationPlayer},
                           OwnComponentUVE<AnimationTreeComponentUVE>{Kind::AnimationTree},
                           OwnComponentUVE<CanvasComponentUVE>{Kind::Canvas},
                           OwnComponentUVE<UITextComponentUVE>{Kind::UIText},
                           OwnComponentUVE<UIImageComponentUVE>{Kind::UIImage},
                           OwnComponentUVE<UIButtonComponentUVE>{Kind::UIButton},
                           OwnComponentUVE<AreaComponentUVE>{Kind::Area3D},
                           OwnComponentUVE<CharacterControllerComponentUVE>{Kind::CharacterBody3D})) {
        return kind;
    }

    // The bodies are told apart by what they combine. A collider with a body is how a
    // CharacterBody3D is built; a collider alone is either a Collider3D or a StaticBody3D, which are
    // built identically, and reads as Collider3D, the older of the two.
    const bool collider = entityManager.HasComponentUVE<ColliderComponentUVE>(entity);
    const bool body = entityManager.HasComponentUVE<RigidBodyComponentUVE>(entity);
    if (collider && body) {
        return Kind::CharacterBody3D;
    }
    if (body) {
        return Kind::RigidBody3D;
    }
    if (collider) {
        return Kind::Collider3D;
    }
    // Last: a script can sit on any node, so it names the node only when nothing else does.
    if (entityManager.HasComponentUVE<ScriptComponentUVE>(entity)) {
        return Kind::Script;
    }
    return Kind::Node3D;
}

void SetSceneNodeKindUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Nodes::SceneNodeKindUVE kind) {
    if (entityManager.HasComponentUVE<SceneNodeTypeComponentUVE>(entity)) {
        entityManager.GetComponentUVE<SceneNodeTypeComponentUVE>(entity).kind = kind;
        return;
    }
    entityManager.AddComponentUVE<SceneNodeTypeComponentUVE>(entity, SceneNodeTypeComponentUVE{kind});
}

} // namespace UVE::Scene
