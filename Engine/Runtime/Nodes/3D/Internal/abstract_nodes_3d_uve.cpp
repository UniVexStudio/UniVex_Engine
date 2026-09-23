// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/abstract_nodes_3d_uve.h"

#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/physics_object_component_uve.h"
#include "uve/component/render_instance_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {
namespace {

/// The Node3D recipe under the child's name, then the base component.
template <typename BaseComponentT>
void ApplyBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const std::string_view nameFallback) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    EnsureNode3DBaselineUVE(entityManager, entity, nameFallback);
    if (!entityManager.HasComponentUVE<VisibilityComponentUVE>(entity)) {
        entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});
    }
    EnsureCommonNodeSectionUVE(entityManager, entity);
    if (!entityManager.HasComponentUVE<BaseComponentT>(entity)) {
        entityManager.AddComponentUVE<BaseComponentT>(entity, BaseComponentT{});
    }
}

} // namespace

void ApplyBoneModifier3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                const std::string_view nameFallback) {
    ApplyBaseUVE<BoneModifierComponentUVE>(entityManager, entity, nameFallback);
}

void ApplyPhysicsObject3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                 const std::string_view nameFallback) {
    ApplyBaseUVE<PhysicsObjectComponentUVE>(entityManager, entity, nameFallback);
}

void ApplyRenderInstance3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                  const std::string_view nameFallback) {
    ApplyBaseUVE<RenderInstanceComponentUVE>(entityManager, entity, nameFallback);
}

} // namespace UVE::Scene
